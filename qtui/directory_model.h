#pragma once
#include "core_api.h"
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

class DirectoryModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role { NameRole = Qt::UserRole + 1, PathRole, FlagsRole,
              SizeRole, ModifiedRole, IsDirRole };
  Q_ENUM(Role)

  explicit DirectoryModel(HermesCore *core, QObject *parent = nullptr);
  ~DirectoryModel() override;

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString path() const;
  bool navigate(const QString &path);
  void setSort(HermesSortKey key, bool desc);

public slots:
  void refresh();
  void pollScan();

signals:
  void pathChanged(const QString &path);
  void navigateFailed(const QString &path);
  void scanFinished();

private:
  // Test seam for the stale-result guard: the integration test cannot force a
  // queued scan to finish after a newer navigation.
  friend class HermesQtUiTest;

  bool applyScan(const QString &path, quint64 generation,
                 const HermesListing &listing, const QString &previousPath);
  bool startScan(const QString &path, quint64 generation,
                 const QString &previousPath);
  void cancelScan();

  struct Entry {
    QString name;
    QString path;
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
  bool m_showHidden = true;
  bool m_folderSizes = false;
  quint64 m_generation = 0;
};
