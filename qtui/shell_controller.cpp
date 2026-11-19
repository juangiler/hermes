#include "shell_controller.h"

#include "directory_model.h"
#include "open_uri_client.h"
#include "places_model.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <mutex>

namespace {
ShellController *g_controller = nullptr;
std::mutex g_controller_mutex;

bool validLeafName(const QString &name)
{
  return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..")
      && !name.contains(QLatin1Char('/')) && !name.contains(QChar(0));
}

void reportLaunchFailure(ShellController *controller, const QString &operation)
{
  emit controller->operationFailed(
      QStringLiteral("Could not start %1.").arg(operation));
}

// Keep the UTF-8 byte arrays alive for the whole C ABI call; only collect the
// raw pointers once the storage vector can no longer reallocate.
QVector<const char *> utf8Pointers(const QStringList &paths, QVector<QByteArray> &storage)
{
  storage.clear();
  storage.reserve(paths.size());
  for (const QString &path : paths) {
    const QByteArray utf8 = path.toUtf8();
    if (utf8.isEmpty())
      return {};
    storage.append(utf8);
  }
  QVector<const char *> pointers;
  pointers.reserve(storage.size());
  for (const QByteArray &utf8 : storage)
    pointers.append(utf8.constData());
  return pointers;
}
}

extern "C" {

static bool hostIsSelftest(void)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  return g_controller && QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
}

static bool hostIsScreenshot(void)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  return g_controller && QCoreApplication::arguments().contains(QStringLiteral("--screenshot"));
}

static bool hostWindowHidden(void)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  return g_controller == nullptr;
}

static void hostMarkSettingsDirty(void)
{
  // The engine owns settings persistence for this migration round.
}

static void hostRequestRedraw(void)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  ShellController *controller = g_controller;
  if (!controller)
    return;
  QMetaObject::invokeMethod(controller, [controller] {
    hermes_core_drain(controller->core());
    controller->directory()->refresh();
  }, Qt::QueuedConnection);
}

static void hostOpenTab(const char *path, int index, bool focus)
{
  if (!path)
    return;
  const QString requested = QString::fromUtf8(path);
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  ShellController *controller = g_controller;
  if (!controller)
    return;
  QMetaObject::invokeMethod(controller,
      [controller, requested, index, focus] {
        controller->openTabFromHost(requested, index, focus);
      }, Qt::QueuedConnection);
}

static int hostCurrentTab(void)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  return g_controller ? g_controller->currentTab() : 0;
}

static void hostOperationsChanged(int kind, int errorCount)
{
  std::lock_guard<std::mutex> lock(g_controller_mutex);
  ShellController *controller = g_controller;
  if (!controller)
    return;
  // The engine may publish a completion from inside its drain, so defer the
  // directory refresh (and the test-visible completion count) to the GUI thread.
  QMetaObject::invokeMethod(controller, [controller, kind, errorCount] {
    controller->recordOperationCompletion(kind, errorCount);
    controller->refreshDirectory();
  }, Qt::QueuedConnection);
}

} // extern "C"

static const HermesCoreHost kShellHost = {
    hostIsSelftest, hostIsScreenshot, hostWindowHidden,
    hostMarkSettingsDirty, hostRequestRedraw, hostOpenTab,
    hostCurrentTab, hostOperationsChanged,
};

void installShellCoreHost()
{
  hermes_core_set_host(&kShellHost);
}

ShellController::ShellController(QObject *parent)
  : QObject(parent),
    m_openUri(new OpenUriClient(this))
{
  {
    std::lock_guard<std::mutex> lock(g_controller_mutex);
    g_controller = this;
  }
  installShellCoreHost();
  m_core = hermes_core_create([](void *user) {
    auto *controller = static_cast<ShellController *>(user);
    std::lock_guard<std::mutex> lock(g_controller_mutex);
    if (g_controller != controller)
      return;
    QMetaObject::invokeMethod(controller, [controller] {
      hermes_core_drain(controller->m_core);
      if (controller->m_directory)
        controller->m_directory->pollScan();
    }, Qt::QueuedConnection);
  }, this);
  m_directory = new DirectoryModel(m_core, this);
  connect(m_directory, &DirectoryModel::navigateFailed, this,
          [this](const QString &failedPath) {
    const int current = currentTab();
    if (current < 0 || current >= m_tabs.size())
      return;
    TabState &tab = m_tabs[current];
    const QString restoredPath = m_directory->path();
    if (tab.path != failedPath || restoredPath.isEmpty() || restoredPath == failedPath)
      return;

    int historyIndex = -1;
    for (int i = 0; i < tab.history.size(); ++i)
      if (tab.history.at(i) == restoredPath)
        historyIndex = i;
    if (historyIndex < 0)
      return;

    tab.path = restoredPath;
    tab.history.remove(historyIndex + 1, tab.history.size() - historyIndex - 1);
    tab.historyIndex = historyIndex;
    emit tabsChanged();
    emitHistory();
  });
  m_places = new PlacesModel(this);

  // Legacy column defaults (name,date:140,size:88). Persistence belongs to the
  // settings slice, so these live only in the controller for now.
  m_columns = QVariantList({
      QVariantMap{{QStringLiteral("key"), QStringLiteral("name")},
                  {QStringLiteral("width"), 0}},
      QVariantMap{{QStringLiteral("key"), QStringLiteral("date")},
                  {QStringLiteral("width"), 140}},
      QVariantMap{{QStringLiteral("key"), QStringLiteral("size")},
                  {QStringLiteral("width"), 88}},
  });
}

ShellController::~ShellController()
{
  // Late callbacks must see no controller before the core goes away.
  {
    std::lock_guard<std::mutex> lock(g_controller_mutex);
    if (g_controller == this)
      g_controller = nullptr;
  }
  hermes_core_destroy(m_core);
  m_core = nullptr;
}

DirectoryModel *ShellController::directory() const
{
  return m_directory;
}

PlacesModel *ShellController::places() const
{
  return m_places;
}

QStringList ShellController::tabs() const
{
  QStringList result;
  result.reserve(m_tabs.size());
  for (const TabState &tab : m_tabs)
    result.append(tab.path);
  return result;
}

int ShellController::currentTab() const
{
  return m_currentTab.load();
}

bool ShellController::canPaste() const
{
  return !m_clipboard.isEmpty();
}

bool ShellController::canGoBack() const
{
  const int current = currentTab();
  return current >= 0 && current < m_tabs.size()
      && m_tabs.at(current).historyIndex > 0;
}

bool ShellController::canGoForward() const
{
  const int current = currentTab();
  return current >= 0 && current < m_tabs.size()
      && m_tabs.at(current).historyIndex >= 0
      && m_tabs.at(current).historyIndex + 1 < m_tabs.at(current).history.size();
}

int ShellController::viewMode() const
{
  return m_viewMode;
}

QVariantList ShellController::columns() const
{
  return m_columns;
}

double ShellController::zoom() const
{
  return m_zoom;
}

void ShellController::setViewMode(int mode)
{
  mode = qBound(0, mode, 2);
  if (m_viewMode == mode)
    return;
  m_viewMode = mode;
  emit viewModeChanged();
}

void ShellController::setZoom(double zoom)
{
  zoom = qBound(0.6, zoom, 2.5);
  if (qFuzzyCompare(m_zoom, zoom))
    return;
  m_zoom = zoom;
  emit zoomChanged();
}

HermesCore *ShellController::core() const
{
  return m_core;
}

bool ShellController::createFolder(const QString &name)
{
  const QByteArray parent = m_directory ? m_directory->path().toUtf8() : QByteArray();
  const QByteArray utf8Name = name.toUtf8();
  if (parent.isEmpty() || !validLeafName(name)) {
    emit operationFailed(QStringLiteral("Folder name must be a single leaf name."));
    return false;
  }
  if (hermes_core_mkdir(m_core, parent.constData(), utf8Name.constData()) != 0) {
    reportLaunchFailure(this, QStringLiteral("creating the folder"));
    return false;
  }
  return true;
}

bool ShellController::renameEntry(const QString &from, const QString &to)
{
  const QByteArray utf8From = from.toUtf8();
  const QByteArray utf8To = to.toUtf8();
  const QString fromParent = QDir::cleanPath(QFileInfo(from).absolutePath());
  const QString toParent = QDir::cleanPath(QFileInfo(to).absolutePath());
  if (utf8From.isEmpty() || !validLeafName(QFileInfo(to).fileName())
      || fromParent != toParent || !m_directory
      || fromParent != QDir::cleanPath(m_directory->path())) {
    emit operationFailed(QStringLiteral("Rename must use a single name in the displayed directory."));
    return false;
  }
  if (hermes_core_rename(m_core, utf8From.constData(), utf8To.constData()) != 0) {
    reportLaunchFailure(this, QStringLiteral("renaming the entry"));
    return false;
  }
  return true;
}

bool ShellController::removeEntries(const QStringList &paths, bool trash)
{
  QVector<QByteArray> storage;
  const QVector<const char *> utf8Paths = utf8Pointers(paths, storage);
  if (utf8Paths.size() != paths.size()) {
    reportLaunchFailure(this, QStringLiteral("removing entries"));
    return false;
  }
  if (hermes_core_remove(m_core, utf8Paths.constData(),
                         static_cast<size_t>(utf8Paths.size()), trash) != 0) {
    reportLaunchFailure(this, QStringLiteral("removing entries"));
    return false;
  }
  return true;
}

bool ShellController::copyEntries(const QStringList &paths, const QString &destDir, bool move)
{
  const QByteArray utf8Dest = destDir.toUtf8();
  QVector<QByteArray> storage;
  const QVector<const char *> utf8Paths = utf8Pointers(paths, storage);
  if (utf8Dest.isEmpty() || utf8Paths.size() != paths.size()) {
    reportLaunchFailure(this, QStringLiteral("copying entries"));
    return false;
  }
  if (hermes_core_copy(m_core, utf8Paths.constData(),
                       static_cast<size_t>(utf8Paths.size()), utf8Dest.constData(),
                       move, m_conflictPolicy) != 0) {
    reportLaunchFailure(this, QStringLiteral("copying entries"));
    return false;
  }
  return true;
}

void ShellController::copySelection(const QStringList &paths, bool move)
{
  m_clipboard = paths;
  m_clipboardMove = move;
  emit clipboardChanged();
}

void ShellController::pasteIntoCurrentDirectory()
{
  if (!m_directory || m_clipboard.isEmpty())
    return;
  copyEntries(m_clipboard, m_directory->path(), m_clipboardMove);
}

void ShellController::setConflictPolicy(HermesConflictPolicy policy)
{
  m_conflictPolicy = policy;
}

void ShellController::recordOperationCompletion(int kind, int errorCount)
{
  m_operationsChangedCount.fetch_add(1);
  if (errorCount > 0) {
    emit operationFailed(
        QStringLiteral("Operation completed with %1 failure(s).").arg(errorCount));
    emit operationFinished(kind, false, errorCount);
    return;
  }
  if (kind == HERMES_OP_MOVE) {
    m_clipboard.clear();
    m_clipboardMove = false;
    emit clipboardChanged();
  }
  emit operationCompleted(kind, errorCount);
  emit operationFinished(kind, true, errorCount);
}

void ShellController::refreshDirectory()
{
  if (m_directory)
    m_directory->refresh();
}

int ShellController::operationsChangedCount() const
{
  return m_operationsChangedCount.load();
}

void ShellController::resetOperationsChangedCount()
{
  m_operationsChangedCount.store(0);
}

bool ShellController::goTo(const QString &path, bool record, int historyIndex)
{
  if (!m_directory || path.isEmpty() || !m_directory->navigate(path))
    return false;

  const int current = currentTab();
  if (m_tabs.isEmpty() || current < 0 || current >= m_tabs.size()) {
    TabState tab;
    tab.path = path;
    tab.history.append(path);
    tab.historyIndex = 0;
    m_tabs.append(tab);
    emit tabsChanged();
    const int index = m_tabs.size() - 1;
    if (currentTab() != index) {
      m_currentTab = index;
      emit currentTabChanged();
    }
  } else {
    TabState &tab = m_tabs[current];
    if (tab.path != path) {
      tab.path = path;
      emit tabsChanged();
    }
    if (record) {
      if (tab.historyIndex >= 0 && tab.historyIndex + 1 < tab.history.size())
        tab.history.remove(tab.historyIndex + 1,
                           tab.history.size() - tab.historyIndex - 1);
      tab.history.append(path);
      tab.historyIndex = tab.history.size() - 1;
    } else if (historyIndex >= 0) {
      tab.historyIndex = historyIndex;
    }
  }
  emitHistory();
  return true;
}

void ShellController::emitHistory()
{
  emit historyChanged();
}

void ShellController::setPaths(const QStringList &paths)
{
  if (paths.isEmpty()) {
    const QString home = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).value(0);
    if (!home.isEmpty())
      navigate(home);
    return;
  }
  for (const QString &argument : paths) {
    const QString path = argument.startsWith(QLatin1String("file://"))
        ? QUrl(argument).toLocalFile() : argument;
    if (path.isEmpty())
      continue;
    if (QFileInfo(path).isDir()) {
      if (m_tabs.isEmpty())
        navigate(path);
      else
        newTab(path);
    } else {
      openEntry(path);
    }
  }
}

void ShellController::navigate(const QString &path)
{
  goTo(path, true);
}

void ShellController::openEntry(const QString &path)
{
  if (QFileInfo(path).isDir())
    navigate(path);
  else if (!path.isEmpty())
    m_openUri->openFile(path, QString());
}

void ShellController::newTab(const QString &path)
{
  openTabFromHost(path, -1, true);
}

void ShellController::openTabFromHost(const QString &path, int index, bool focus)
{
  if (path.isEmpty())
    return;

  TabState tab;
  tab.path = path;
  tab.history.append(path);
  tab.historyIndex = 0;

  // Match the engine's tab_new(): insert at the requested index, or append.
  if (index < 0 || index > m_tabs.size())
    index = m_tabs.size();
  m_tabs.insert(index, tab);
  emit tabsChanged();

  if (focus || m_currentTab < 0) {
    if (m_currentTab != index) {
      m_currentTab = index;
      emit currentTabChanged();
    }
    if (m_directory) {
      m_directory->navigate(path);
      emitHistory();
    }
  } else if (m_currentTab >= index) {
    // Inserting before the focused tab shifts its index but not its context.
    ++m_currentTab;
    emit currentTabChanged();
  }
}

void ShellController::closeTab(int index)
{
  if (index < 0 || index >= m_tabs.size())
    return;
  const int previous = m_currentTab;
  m_tabs.remove(index);
  if (index < m_currentTab)
    --m_currentTab;
  if (m_tabs.isEmpty())
    m_currentTab = -1;
  else if (m_currentTab >= m_tabs.size())
    m_currentTab = m_tabs.size() - 1;

  emit tabsChanged();
  if (m_currentTab != previous)
    emit currentTabChanged();
  emitHistory();
  if (m_currentTab >= 0 && m_directory && m_directory->path() != m_tabs.at(m_currentTab).path)
    goTo(m_tabs.at(m_currentTab).path, false);
}

void ShellController::selectTab(int index)
{
  if (index < 0 || index >= m_tabs.size())
    return;
  m_currentTab = index;
  emit currentTabChanged();
  goTo(m_tabs.at(index).path, false);
}

void ShellController::up()
{
  const QString current = m_directory ? m_directory->path() : QString();
  if (current.isEmpty())
    return;
  const QString parent = QFileInfo(current).absolutePath();
  if (parent != current)
    navigate(parent);
}

void ShellController::back()
{
  const int current = currentTab();
  if (current < 0 || current >= m_tabs.size())
    return;
  const int index = m_tabs.at(current).historyIndex - 1;
  if (index < 0)
    return;
  goTo(m_tabs.at(current).history.at(index), false, index);
}

void ShellController::forward()
{
  const int current = currentTab();
  if (current < 0 || current >= m_tabs.size())
    return;
  TabState &tab = m_tabs[current];
  const int index = tab.historyIndex + 1;
  if (tab.historyIndex < 0 || index >= tab.history.size())
    return;
  goTo(tab.history.at(index), false, index);
}
