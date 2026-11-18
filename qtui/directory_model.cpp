#include "directory_model.h"

#include <QDir>
#include <QModelIndex>

DirectoryModel::DirectoryModel(HermesCore *core, QObject *parent)
  : QAbstractListModel(parent), m_core(core)
{
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
  const QByteArray utf8 = path.toUtf8();
  HermesListing listing{};
  if (hermes_core_scan_sorted(m_core, utf8.constData(), m_key, m_desc, &listing) != 0) {
    hermes_listing_free(&listing);
    emit navigateFailed(path);
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
  hermes_listing_free(&listing);

  beginResetModel();
  m_entries.swap(entries);
  m_path = path;
  endResetModel();
  emit pathChanged(m_path);
  return true;
}

void DirectoryModel::refresh()
{
  if (!m_path.isEmpty())
    navigate(m_path);
}

void DirectoryModel::setSort(HermesSortKey key, bool desc)
{
  m_key = key;
  m_desc = desc;
  refresh();
}
