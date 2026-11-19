#include "directory_model.h"

#include <QDir>
#include <QModelIndex>

DirectoryModel::DirectoryModel(HermesCore *core, QObject *parent)
  : QAbstractListModel(parent), m_core(core)
{
}

DirectoryModel::~DirectoryModel()
{
  cancelScan();
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_entries.size();
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
    return {};

  const Entry &entry = m_entries.at(index.row());
  switch (role) {
  case NameRole: return entry.name;
  case PathRole: return entry.path;
  case FlagsRole: return static_cast<int>(entry.flags);
  case SizeRole: return entry.size;
  case ModifiedRole: return entry.modified;
  case IsDirRole: return (entry.flags & HERMES_ENTRY_DIR) != 0;
  default: return {};
  }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
  return {{NameRole, "name"}, {PathRole, "path"}, {FlagsRole, "flags"},
          {SizeRole, "size"}, {ModifiedRole, "modified"}, {IsDirRole, "isDir"}};
}

QString DirectoryModel::path() const
{
  return m_path;
}

bool DirectoryModel::navigate(const QString &path)
{
  const QString previous = m_path;
  cancelScan();
  const quint64 generation = ++m_generation;
  m_path = path;
  if (startScan(path, generation, previous))
    return true;

  if (m_generation == generation && m_path == path) {
    m_path = previous;
    emit navigateFailed(path);
  }
  return false;
}

void DirectoryModel::refresh()
{
  if (m_path.isEmpty())
    return;
  const QString path = m_path;
  cancelScan();
  const quint64 generation = ++m_generation;
  if (!startScan(path, generation, path)) {
    m_path = path;
    emit navigateFailed(path);
  }
}

void DirectoryModel::pollScan()
{
  if (!m_scan)
    return;

  HermesListing listing{};
  const int result = hermes_core_scan_poll(m_core, m_scan, &listing);
  if (result == 0)
    return;

  const QString path = m_scanPath;
  const QString previousPath = m_scanPreviousPath;
  const quint64 generation = m_scanGeneration;
  HermesScan *scan = m_scan;
  m_scan = nullptr;
  m_scanPath.clear();
  m_scanPreviousPath.clear();
  m_scanGeneration = 0;
  hermes_core_scan_free(scan);

  if (result < 0) {
    hermes_listing_free(&listing);
    if (generation == m_generation && path == m_path) {
      m_path = m_rowsPath.isEmpty() ? previousPath : m_rowsPath;
      emit navigateFailed(path);
    }
    return;
  }

  if (applyScan(path, generation, listing, previousPath))
    emit scanFinished();
  hermes_listing_free(&listing);
}

bool DirectoryModel::startScan(const QString &path, quint64 generation,
                               const QString &previousPath)
{
  const QByteArray utf8 = path.toUtf8();
  const HermesScanOptions options = {
    m_key, m_desc, m_showHidden, m_folderSizes
  };
  m_scan = hermes_core_scan_start(m_core, utf8.constData(), &options);
  if (!m_scan)
    return false;

  m_scanPath = path;
  m_scanPreviousPath = previousPath;
  m_scanGeneration = generation;
  return true;
}

void DirectoryModel::cancelScan()
{
  if (!m_scan)
    return;
  hermes_core_scan_cancel(m_scan);
  hermes_core_scan_free(m_scan);
  m_scan = nullptr;
  m_scanPath.clear();
  m_scanPreviousPath.clear();
  m_scanGeneration = 0;
}

bool DirectoryModel::applyScan(const QString &path, quint64 generation,
                               const HermesListing &listing,
                               const QString &previousPath)
{
  if (generation != m_generation || path != m_path) {
    return false;
  }

  QVector<Entry> entries;
  entries.reserve(static_cast<qsizetype>(listing.count));
  const QDir dir(path);
  for (size_t i = 0; i < listing.count; ++i) {
    const HermesEntry &item = listing.items[i];
    const QString name = QString::fromUtf8(item.name, static_cast<qsizetype>(item.name_len));
    entries.append({name, dir.filePath(name), item.flags, item.size, item.mtime});
  }

  beginResetModel();
  m_entries.swap(entries);
  m_path = path;
  m_rowsPath = path;
  endResetModel();
  if (previousPath != path)
    emit pathChanged(m_path);
  return true;
}

void DirectoryModel::setSort(HermesSortKey key, bool desc)
{
  m_key = key;
  m_desc = desc;
  refresh();
}
