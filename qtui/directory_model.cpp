#include "directory_model.h"

#include <QDir>
#include <QModelIndex>

#include <chrono>

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
  m_worker = std::thread(&DirectoryModel::workerLoop, this);
}

DirectoryModel::~DirectoryModel()
{
  stopWorker();
}

void DirectoryModel::stopWorker()
{
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    m_stop = true;
    if (m_activeJob)
      m_activeJob->cancelled->store(true);
    for (const JobPtr &job : m_pending)
      job->cancelled->store(true);
  }
  m_jobCv.notify_all();
  if (m_worker.joinable())
    m_worker.join();
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

DirectoryModel::ScanConfig DirectoryModel::currentConfig() const
{
  ScanConfig config;
  config.key = m_key;
  config.desc = m_desc;
  config.showHidden = m_showHidden;
  config.folderSizes = m_folderSizes;
  return config;
}

// The engine wake only nudges the worker; the GUI thread never polls a handle.
void DirectoryModel::pollScan()
{
  std::lock_guard<std::mutex> lock(m_jobMutex);
  m_jobCv.notify_all();
}

DirectoryModel::ScanSnapshot DirectoryModel::makeSnapshot(
    const ScanConfig &config, const HermesListing &listing, bool failed,
    bool firstResult, bool folderSizesActive,
    const std::shared_ptr<std::atomic<bool>> &cancelled)
{
  ScanSnapshot snapshot;
  snapshot.config = config;
  snapshot.cancelled = cancelled;
  snapshot.failed = failed;
  snapshot.firstResult = firstResult;
  snapshot.folderSizesActive = folderSizesActive;
  if (failed)
    return snapshot;

  // Sort, name copies and Qt string building all happen here, off the GUI
  // thread. Entries own their data, so the ABI listing can be freed on return.
  snapshot.entries.reserve(static_cast<qsizetype>(listing.count));
  const QDir dir(config.path);
  const QByteArray dirBytes = config.path.toUtf8();
  for (size_t i = 0; i < listing.count; ++i) {
    const HermesEntry &item = listing.items[i];
    const QByteArray rawName(item.name, static_cast<qsizetype>(item.name_len));
    const QString name = QString::fromUtf8(rawName);
    // Display may replace malformed bytes with U+FFFD; operations still need
    // the exact bytes the engine scanned, so keep them alongside the path.
    const QByteArray rawPath = dirBytes + "/" + rawName;
    snapshot.entries.append({name, dir.filePath(name), rawPath, item.flags,
                             item.size, item.mtime});
  }
  return snapshot;
}

void DirectoryModel::postSnapshot(const std::shared_ptr<ScanSnapshot> &snapshot)
{
  // A snapshot already queued when the model dies must not touch it.
  QPointer<DirectoryModel> guard(this);
  QMetaObject::invokeMethod(this, [guard, snapshot] {
    if (guard)
      guard->applySnapshot(*snapshot);
  }, Qt::QueuedConnection);
}

void DirectoryModel::workerLoop()
{
  const auto dispose = [](const JobPtr &job) {
    if (!job->scan)
      return;
    if (job->cancelled->load())
      hermes_core_scan_cancel(job->scan);
    hermes_core_scan_free(job->scan);
    job->scan = nullptr;
  };

  std::unique_lock<std::mutex> lock(m_jobMutex);
  for (;;) {
    m_jobCv.wait(lock, [this] {
      return m_stop || (!m_activeJob && !m_pending.empty());
    });
    if (m_stop) {
      if (m_activeJob)
        dispose(m_activeJob);
      for (const JobPtr &job : m_pending)
        dispose(job);
      m_pending.clear();
      m_activeJob.reset();
      return;
    }

    m_activeJob = m_pending.front();
    m_pending.pop_front();
    JobPtr job = m_activeJob;
    lock.unlock();

    bool done = false;
    while (!job->cancelled->load() && !done) {
      HermesListing listing{};
      const int result = hermes_core_scan_poll(job->core, job->scan, &listing);
      if (result < 0) {
        if (!job->cancelled->load()) {
          auto snapshot = std::make_shared<ScanSnapshot>(makeSnapshot(
              job->config, listing, true, false, false, job->cancelled));
          postSnapshot(snapshot);
        }
        hermes_listing_free(&listing);
        done = true;
      } else if (result > 0) {
        const bool firstResult = !job->applied;
        if (firstResult && job->config.folderSizes) {
          job->folderSizesActive =
              hermes_core_scan_start_folder_sizes(job->core, job->scan) == 0;
        }
        auto snapshot = std::make_shared<ScanSnapshot>(makeSnapshot(
            job->config, listing, false, firstResult, job->folderSizesActive,
            job->cancelled));
        const bool sizesPending = job->folderSizesActive
            && has_pending_folder_sizes(listing, job->config.showHidden);
        // A size pass republishes the whole listing; skip identical snapshots
        // so an unchanged directory does not churn the GUI thread.
        const bool changed = !job->applied || snapshot->entries != job->lastEntries;
        hermes_listing_free(&listing);
        if (changed && !job->cancelled->load()) {
          job->lastEntries = snapshot->entries;
          postSnapshot(snapshot);
        }
        job->applied = true;
        if (!sizesPending)
          done = true;
      }
      if (!done && !job->cancelled->load())
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    dispose(job);
    lock.lock();
    m_activeJob.reset();
  }
}

bool DirectoryModel::startScan(const QString &path, quint64 generation,
                               const QString &previousPath)
{
  const QByteArray utf8 = path.toUtf8();
  const HermesScanOptions options = {
    m_key, m_desc, m_showHidden, m_folderSizes
  };
  HermesScan *scan = hermes_core_scan_start(m_core, utf8.constData(), &options);
  if (!scan)
    return false;

  auto job = std::make_shared<ScanJob>();
  job->core = m_core;
  job->scan = scan;
  job->config = currentConfig();
  job->config.path = path;
  job->config.previousPath = previousPath;
  job->config.generation = generation;
  job->cancelled = std::make_shared<std::atomic<bool>>(false);

  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    // Only the newest navigation can produce visible rows.
    if (m_activeJob)
      m_activeJob->cancelled->store(true);
    for (const JobPtr &pending : m_pending)
      pending->cancelled->store(true);
    m_pending.push_back(job);
  }
  m_jobCv.notify_all();
  return true;
}

void DirectoryModel::cancelScan()
{
  {
    std::lock_guard<std::mutex> lock(m_jobMutex);
    if (m_activeJob)
      m_activeJob->cancelled->store(true);
    for (const JobPtr &job : m_pending)
      job->cancelled->store(true);
  }
  m_jobCv.notify_all();
}

bool DirectoryModel::applySnapshot(ScanSnapshot snapshot)
{
  // A job cancelled after its snapshot was queued must not change the view.
  if (snapshot.cancelled && snapshot.cancelled->load())
    return false;
  const ScanConfig &config = snapshot.config;
  if (config.generation != m_generation || config.path != m_path)
    return false;

  if (snapshot.failed) {
    // The rows on screen still belong to the last successful scan.
    m_path = m_rowsPath.isEmpty() ? config.previousPath : m_rowsPath;
    emit navigateFailed(config.path);
    return false;
  }

  const bool rowsPathChanged = m_rowsPath != config.path;
  QVector<Entry> &entries = snapshot.entries;
  // Match by raw bytes, never by the decoded display name: two names may
  // decode to the same string, and a size pass may return them in a new order.
  QVector<qsizetype> oldRows(entries.size(), -1);
  bool sameRows = m_rowsPath == config.path && m_rowsKey == config.key
      && m_rowsDesc == config.desc && m_entries.size() == entries.size();
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
  m_path = config.path;
  m_rowsPath = config.path;
  m_rowsKey = config.key;
  m_rowsDesc = config.desc;
  if (rowsPathChanged)
    emit pathChanged(m_path);
  if (snapshot.firstResult)
    emit scanFinished();
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
