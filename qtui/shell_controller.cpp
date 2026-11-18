#include "shell_controller.h"

#include "directory_model.h"
#include "open_uri_client.h"
#include "places_model.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>

namespace {
ShellController *g_controller = nullptr;
}

extern "C" {

static bool hostIsSelftest(void)
{
  return g_controller && QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
}

static bool hostIsScreenshot(void)
{
  return g_controller && QCoreApplication::arguments().contains(QStringLiteral("--screenshot"));
}

static bool hostWindowHidden(void)
{
  return g_controller == nullptr;
}

static void hostMarkSettingsDirty(void)
{
  // The engine owns settings persistence for this migration round.
}

static void hostRequestRedraw(void)
{
  if (!g_controller)
    return;
  QMetaObject::invokeMethod(g_controller, [] {
    if (!g_controller)
      return;
    hermes_core_drain(g_controller->core());
    g_controller->directory()->refresh();
  }, Qt::QueuedConnection);
}

static void hostOpenTab(const char *path, int index, bool focus)
{
  if (!g_controller || !path)
    return;
  const QString requested = QString::fromUtf8(path);
  QMetaObject::invokeMethod(g_controller,
      [requested, index, focus] {
        if (g_controller)
          g_controller->openTabFromHost(requested, index, focus);
      }, Qt::QueuedConnection);
}

static int hostCurrentTab(void)
{
  return g_controller ? g_controller->currentTab() : 0;
}

static void hostOperationsChanged(int kind, int errorCount)
{
  // Task 5 wires engine operations to a queued directory refresh.
  (void)kind;
  (void)errorCount;
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
  g_controller = this;
  installShellCoreHost();
  m_core = hermes_core_create([](void *user) {
    auto *controller = static_cast<ShellController *>(user);
    QMetaObject::invokeMethod(controller, [controller] {
      hermes_core_drain(controller->m_core);
      if (controller->m_directory)
        controller->m_directory->refresh();
    }, Qt::QueuedConnection);
  }, this);
  m_directory = new DirectoryModel(m_core, this);
  m_places = new PlacesModel(this);
}

ShellController::~ShellController()
{
  // Late callbacks must see no controller before the core goes away.
  g_controller = nullptr;
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
  return m_currentTab;
}

bool ShellController::canGoBack() const
{
  return m_currentTab >= 0 && m_currentTab < m_tabs.size()
      && m_tabs.at(m_currentTab).historyIndex > 0;
}

bool ShellController::canGoForward() const
{
  return m_currentTab >= 0 && m_currentTab < m_tabs.size()
      && m_tabs.at(m_currentTab).historyIndex >= 0
      && m_tabs.at(m_currentTab).historyIndex + 1 < m_tabs.at(m_currentTab).history.size();
}

HermesCore *ShellController::core() const
{
  return m_core;
}

void ShellController::goTo(const QString &path, bool record)
{
  if (!m_directory || path.isEmpty() || !m_directory->navigate(path))
    return;

  if (m_tabs.isEmpty() || m_currentTab < 0 || m_currentTab >= m_tabs.size()) {
    TabState tab;
    tab.path = path;
    tab.history.append(path);
    tab.historyIndex = 0;
    m_tabs.append(tab);
    emit tabsChanged();
    const int index = m_tabs.size() - 1;
    if (m_currentTab != index) {
      m_currentTab = index;
      emit currentTabChanged();
    }
  } else {
    TabState &tab = m_tabs[m_currentTab];
    tab.path = path;
    if (record) {
      if (tab.historyIndex >= 0 && tab.historyIndex + 1 < tab.history.size())
        tab.history.remove(tab.historyIndex + 1,
                           tab.history.size() - tab.historyIndex - 1);
      tab.history.append(path);
      tab.historyIndex = tab.history.size() - 1;
    }
  }
  emitHistory();
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
  if (m_currentTab < 0 || m_currentTab >= m_tabs.size())
    return;
  const int index = m_tabs.at(m_currentTab).historyIndex - 1;
  if (index < 0)
    return;
  m_tabs[m_currentTab].historyIndex = index;
  goTo(m_tabs.at(m_currentTab).history.at(index), false);
}

void ShellController::forward()
{
  if (m_currentTab < 0 || m_currentTab >= m_tabs.size())
    return;
  TabState &tab = m_tabs[m_currentTab];
  const int index = tab.historyIndex + 1;
  if (tab.historyIndex < 0 || index >= tab.history.size())
    return;
  tab.historyIndex = index;
  goTo(tab.history.at(index), false);
}
