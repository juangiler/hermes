#pragma once
#include "core_api.h"
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class DirectoryModel : public QAbstractListModel
{
  Q_OBJECT

  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
  Q_PROPERTY(bool folderSizes READ folderSizes WRITE setFolderSizes NOTIFY folderSizesChanged)
  Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)

public:
  enum Role { NameRole = Qt::UserRole + 1, PathRole, FlagsRole,
              SizeRole, ModifiedRole, IsDirRole };
  Q_ENUM(Role)

  // A displayed path can name two different files when raw names decode to the
  // same QString. Operations must refuse that case instead of guessing.
  enum class RawPathResult { Unique, Ambiguous, NotListed };

  explicit DirectoryModel(HermesCore *core, QObject *parent = nullptr);
  ~DirectoryModel() override;

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  // QML cannot call data()/index(), so expose row paths for range selection.
  Q_INVOKABLE QStringList paths() const;
  RawPathResult rawPathFor(const QString &displayedPath, QByteArray *rawPath) const;

  QString path() const;
  bool navigate(const QString &path);
  void setSort(HermesSortKey key, bool desc);
  void setSortKey(HermesSortKey key);
  Q_INVOKABLE QStringList sortKeys() const;
  QString sortKey() const;
  Q_INVOKABLE void setSortKey(const QString &key);
  Q_INVOKABLE void setShowHidden(bool show);
  bool showHidden() const;
  Q_INVOKABLE void setFolderSizes(bool on);
  bool folderSizes() const;

  // Join the scan worker before a core is destroyed. The destructor calls it
  // too; the controller needs the earlier call because it owns the core.
  void stopWorker();

public slots:
  void refresh();
  void pollScan();

signals:
  void pathChanged(const QString &path);
  void navigateFailed(const QString &path);
  void scanFinished();
  void showHiddenChanged();
  void folderSizesChanged();
  void sortKeyChanged();

private:
  // Test seam for the stale-result guard: the integration test cannot force a
  // queued scan to finish after a newer navigation.
  friend class HermesQtUiTest;

  struct Entry {
    QString name;
    QString path;
    QByteArray rawPath;
    unsigned char flags;
    quint64 size;
    qint64 modified;

    bool operator==(const Entry &other) const
    {
      return name == other.name && path == other.path && rawPath == other.rawPath
          && flags == other.flags && size == other.size && modified == other.modified;
    }
  };

  // Captured on the GUI thread when a scan starts. The worker must never read
  // mutable model options: a later toggle starts a new generation instead.
  struct ScanConfig {
    HermesSortKey key = HERMES_SORT_NAME;
    bool desc = false;
    bool showHidden = false;
    bool folderSizes = false;
    QString path;
    QString previousPath;
    quint64 generation = 0;
  };

  struct ScanSnapshot {
    ScanConfig config;
    QVector<Entry> entries;
    std::shared_ptr<std::atomic<bool>> cancelled;
    bool failed = false;
    bool firstResult = false;
    bool folderSizesActive = false;
  };

  struct ScanJob {
    HermesCore *core = nullptr;
    HermesScan *scan = nullptr;
    ScanConfig config;
    std::shared_ptr<std::atomic<bool>> cancelled;
    // Worker-local progress; never read from the GUI thread.
    bool applied = false;
    bool folderSizesActive = false;
    QVector<Entry> lastEntries;
  };
  using JobPtr = std::shared_ptr<ScanJob>;

  ScanConfig currentConfig() const;
  static ScanSnapshot makeSnapshot(const ScanConfig &config,
                                   const HermesListing &listing, bool failed,
                                   bool firstResult, bool folderSizesActive,
                                   const std::shared_ptr<std::atomic<bool>> &cancelled);
  bool applySnapshot(ScanSnapshot snapshot);
  bool startScan(const QString &path, quint64 generation,
                 const QString &previousPath);
  void cancelScan();
  void workerLoop();
  void postSnapshot(const std::shared_ptr<ScanSnapshot> &snapshot);

  HermesCore *m_core;
  QVector<Entry> m_entries;
  QString m_path;
  QString m_rowsPath;
  HermesSortKey m_key = HERMES_SORT_NAME;
  bool m_desc = false;
  // The sort that produced m_entries: a changed key/direction must reapply the
  // engine's order, while a size-pass result for the same must not.
  HermesSortKey m_rowsKey = HERMES_SORT_NAME;
  bool m_rowsDesc = false;
  bool m_showHidden = false;
  bool m_folderSizes = false;
  quint64 m_generation = 0;

  // The worker owns the scan handle for its whole life. The GUI thread only
  // queues new jobs and flips the shared cancel flag.
  std::thread m_worker;
  std::mutex m_jobMutex;
  std::condition_variable m_jobCv;
  std::deque<JobPtr> m_pending;
  JobPtr m_activeJob;
  bool m_stop = false;
};
