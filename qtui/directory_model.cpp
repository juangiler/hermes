#include "directory_model.h"

#include <QDir>
#include <QModelIndex>

namespace {
// Keep the QML-facing names in engine order next to the enum mapping.
const char *const kSortKeyNames[] = {"name", "date", "type", "size",
                                     "created", "ext", "perms", "owner"};
static_assert(HERMES_SORT_OWNER + 1 ==
              static_cast<int>(sizeof(kSortKeyNames) / sizeof(kSortKeyNames[0])),
              "sort key names must match HermesSortKey");
}

static bool has_pending_folder_sizes(const HermesListing &listing, bool show_hidden)
{
  for (size_t i = 0; i < listing.count; ++i) {
    const HermesEntry &entry = listing.items[i];
    if (!(entry.flags & HERMES_ENTRY_DIR) || (entry.flags & HERMES_ENTRY_LINK))
      continue;
    if (!show_hidden && (entry.flags & HERMES_ENTRY_HIDDEN))
      continue;
    if (entry.flags & HERMES_ENTRY_SIZE_UNKNOWN)
      return true;
  }
  return false;
}

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
  case SizeRole:
    return (entry.flags & HERMES_ENTRY_SIZE_UNKNOWN)
        ? QVariant::fromValue<qlonglong>(-1)
        : QVariant::fromValue(entry.size);
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

QStringList DirectoryModel::paths() const
{
  QStringList result;
  result.reserve(m_entries.size());
  for (const Entry &entry : m_entries)
    result.append(entry.path);
  return result;
}

DirectoryModel::RawPathResult DirectoryModel::rawPathFor(
    const QString &displayedPath, QByteArray *rawPath) const
{
  int matches = 0;
  QByteArray found;
  for (const Entry &entry : m_entries) {
    if (entry.path != displayedPath)
      continue;
    ++matches;
    found = entry.rawPath;
  }
  if (matches > 1)
    return RawPathResult::Ambiguous;
  if (matches == 0)
    return RawPathResult::NotListed;
  if (rawPath)
    *rawPath = found;
  return RawPathResult::Unique;
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

  if (result < 0) {
    HermesScan *scan = m_scan;
    m_scan = nullptr;
    m_scanPath.clear();
    m_scanPreviousPath.clear();
    m_scanGeneration = 0;
    m_scanApplied = false;
    m_scanFolderSizesActive = false;
    hermes_core_scan_free(scan);
    hermes_listing_free(&listing);
    if (generation == m_generation && path == m_path) {
      m_path = m_rowsPath.isEmpty() ? previousPath : m_rowsPath;
      emit navigateFailed(path);
    }
    return;
  }

  const bool firstResult = !m_scanApplied;
  if (firstResult && m_folderSizes)
    m_scanFolderSizesActive =
        hermes_core_scan_start_folder_sizes(m_core, m_scan) == 0;

  const bool applied = applyScan(path, generation, listing);
  if (applied && firstResult) {
    m_scanApplied = true;
    emit scanFinished();
  }

  const bool sizesPending = applied && m_scanFolderSizesActive
      && has_pending_folder_sizes(listing, m_showHidden);
  hermes_listing_free(&listing);
  if (sizesPending)
    return;

  HermesScan *scan = m_scan;
  m_scan = nullptr;
  m_scanPath.clear();
  m_scanPreviousPath.clear();
  m_scanGeneration = 0;
  m_scanApplied = false;
  m_scanFolderSizesActive = false;
  hermes_core_scan_free(scan);
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
  m_scanApplied = false;
  m_scanFolderSizesActive = false;
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
  m_scanApplied = false;
  m_scanFolderSizesActive = false;
}

bool DirectoryModel::applyScan(const QString &path, quint64 generation,
                               const HermesListing &listing)
{
  if (generation != m_generation || path != m_path) {
    return false;
  }

  const bool rowsPathChanged = m_rowsPath != path;
  QVector<Entry> entries;
  entries.reserve(static_cast<qsizetype>(listing.count));
  const QDir dir(path);
  const QByteArray dirBytes = path.toUtf8();
  for (size_t i = 0; i < listing.count; ++i) {
    const HermesEntry &item = listing.items[i];
    const QByteArray rawName(item.name, static_cast<qsizetype>(item.name_len));
    const QString name = QString::fromUtf8(rawName);
    // Display may replace malformed bytes with U+FFFD; operations still need
    // the exact bytes the engine scanned, so keep them alongside the path.
    const QByteArray rawPath = dirBytes + "/" + rawName;
    entries.append({name, dir.filePath(name), rawPath, item.flags, item.size, item.mtime});
  }

  // Match by raw bytes, never by the decoded display name: two names may
  // decode to the same string, and a size pass may return them in a new order.
  QVector<qsizetype> oldRows(entries.size(), -1);
  bool sameRows = m_rowsPath == path && m_rowsKey == m_key && m_rowsDesc == m_desc
      && m_entries.size() == entries.size();
  if (sameRows) {
    QHash<QByteArray, qsizetype> currentRows;
    for (qsizetype i = 0; i < m_entries.size(); ++i)
      currentRows.insert(m_entries.at(i).rawPath, i);
    QVector<bool> used(m_entries.size(), false);
    for (qsizetype i = 0; i < entries.size(); ++i) {
      const auto it = currentRows.constFind(entries.at(i).rawPath);
      if (it == currentRows.constEnd() || used.at(it.value())) {
        sameRows = false;
        break;
      }
      used[it.value()] = true;
      oldRows[i] = it.value();
    }
  }

  if (sameRows) {
    for (qsizetype i = 0; i < entries.size(); ++i) {
      const Entry &updated = entries.at(i);
      Entry &current = m_entries[oldRows.at(i)];
      if (current.flags == updated.flags && current.size == updated.size
          && current.modified == updated.modified)
        continue;
      current.flags = updated.flags;
      current.size = updated.size;
      current.modified = updated.modified;
      const int row = static_cast<int>(oldRows.at(i));
      emit dataChanged(index(row), index(row),
                       {FlagsRole, SizeRole, ModifiedRole, IsDirRole});
    }
  } else {
    beginResetModel();
    m_entries.swap(entries);
    endResetModel();
  }
  m_path = path;
  m_rowsPath = path;
  m_rowsKey = m_key;
  m_rowsDesc = m_desc;
  if (rowsPathChanged)
    emit pathChanged(m_path);
  return true;
}

void DirectoryModel::setSort(HermesSortKey key, bool desc)
{
  const bool keyChanged = m_key != key;
  m_key = key;
  m_desc = desc;
  if (keyChanged)
    emit sortKeyChanged();
  refresh();
}

void DirectoryModel::setSortKey(HermesSortKey key)
{
  const bool keyChanged = m_key != key;
  if (m_key == key) {
    m_desc = !m_desc;
  } else {
    m_key = key;
    m_desc = key == HERMES_SORT_DATE || key == HERMES_SORT_SIZE;
  }
  if (keyChanged)
    emit sortKeyChanged();
  refresh();
}

QStringList DirectoryModel::sortKeys() const
{
  QStringList keys;
  for (const char *name : kSortKeyNames)
    keys.append(QString::fromLatin1(name));
  return keys;
}

QString DirectoryModel::sortKey() const
{
  return QString::fromLatin1(kSortKeyNames[m_key]);
}

void DirectoryModel::setSortKey(const QString &key)
{
  const int index = sortKeys().indexOf(key);
  if (index >= 0)
    setSortKey(static_cast<HermesSortKey>(index));
}

void DirectoryModel::setShowHidden(bool show)
{
  if (m_showHidden == show)
    return;
  m_showHidden = show;
  emit showHiddenChanged();
  refresh();
}

bool DirectoryModel::showHidden() const
{
  return m_showHidden;
}

void DirectoryModel::setFolderSizes(bool on)
{
  if (m_folderSizes == on)
    return;
  m_folderSizes = on;
  emit folderSizesChanged();
  refresh();
}

bool DirectoryModel::folderSizes() const
{
  return m_folderSizes;
}
