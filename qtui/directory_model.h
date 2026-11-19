#pragma once
#include "core_api.h"
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

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

  bool applyScan(const QString &path, quint64 generation,
                 const HermesListing &listing);
  bool startScan(const QString &path, quint64 generation,
                 const QString &previousPath);
  void cancelScan();

  struct Entry {
    QString name;
    QString path;
    QByteArray rawPath;
    unsigned char flags;
    quint64 size;
    qint64 modified;
  };

  HermesCore *m_core;
  HermesScan *m_scan = nullptr;
  QString m_scanPath;
  QString m_scanPreviousPath;
  quint64 m_scanGeneration = 0;
  QVector<Entry> m_entries;
  QString m_path;
  QString m_rowsPath;
  HermesSortKey m_key = HERMES_SORT_NAME;
  bool m_desc = false;
  // The sort that produced m_entries: a changed key/direction must reapply the
  // engine's order, while a size-pass result for the same must not.
  HermesSortKey m_rowsKey = HERMES_SORT_NAME;
  bool m_rowsDesc = false;
  bool m_showHidden = true;
  bool m_folderSizes = false;
  bool m_scanApplied = false;
  bool m_scanFolderSizesActive = false;
  quint64 m_generation = 0;
};
