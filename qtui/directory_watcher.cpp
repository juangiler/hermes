#include "directory_watcher.h"

#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>

DirectoryWatcher::DirectoryWatcher(QObject *parent)
  : QObject(parent),
    m_watcher(new QFileSystemWatcher(this)),
    m_timer(new QTimer(this))
{
  m_timer->setSingleShot(true);
  m_timer->setInterval(300);
  // Restarting the single-shot timer folds a burst of filesystem events into
  // one notification, so a copy or build does not rescan once per file.
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
    m_timer->start();
  });
  connect(m_timer, &QTimer::timeout, this, &DirectoryWatcher::changed);
}

void DirectoryWatcher::watch(const QString &path)
{
  unwatch();
  if (!path.isEmpty())
    m_watcher->addPath(path);
}

void DirectoryWatcher::unwatch()
{
  m_timer->stop();
  const QStringList paths = m_watcher->directories();
  if (!paths.isEmpty())
    m_watcher->removePaths(paths);
}
