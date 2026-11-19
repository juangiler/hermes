#pragma once
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

// Watches one directory and reports changes at most once per quiet period.
// The controller owns this so the model can still be used headless.
class DirectoryWatcher : public QObject
{
  Q_OBJECT

public:
  explicit DirectoryWatcher(QObject *parent = nullptr);

  void watch(const QString &path);
  void unwatch();

signals:
  void changed();

private:
  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_timer = nullptr;
};
