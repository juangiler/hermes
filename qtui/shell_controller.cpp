#include "shell_controller.h"

#include "directory_model.h"
#include "directory_watcher.h"
#include "open_uri_client.h"
#include "places_model.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
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

// A tab's label is the leaf its path names, matching the legacy tab strip.
QString labelForPath(const QString &path)
{
  const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  return parts.isEmpty() ? QStringLiteral("/") : parts.last();
}

void reportLaunchFailure(ShellController *controller, const QString &operation)
{
  emit controller->operationFailed(
      QStringLiteral("Could not start %1.").arg(operation));
}

// Keep the byte arrays alive for the whole C ABI call; only collect the raw
// pointers once the storage vector can no longer reallocate.
QVector<const char *> rawPointers(const QVector<QByteArray> &storage)
{
  QVector<const char *> pointers;
  pointers.reserve(storage.size());
  for (const QByteArray &bytes : storage) {
    if (bytes.isEmpty())
      return {};
    pointers.append(bytes.constData());
  }
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
    // An operation may have changed entries in any of the open directories.
    controller->refreshDirectory();
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
      // Only the panels with a live scan need their results collected.
      for (Panel &panel : controller->m_tree.panels())
        if (panel.model)
          panel.model->pollScan();
    }, Qt::QueuedConnection);
  }, this);
  createPanelResources(0);
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
  syncCurrentTab();
}

ShellController::~ShellController()
{
  // Late callbacks must see no controller before the core goes away.
  {
    std::lock_guard<std::mutex> lock(g_controller_mutex);
    if (g_controller == this)
      g_controller = nullptr;
  }
  // Each scan worker polls handles owned by this core; join them first.
  for (Panel &panel : m_tree.panels())
    if (panel.model)
      panel.model->stopWorker();
  hermes_core_destroy(m_core);
  m_core = nullptr;
}

void ShellController::createPanelResources(int panelId)
{
  Panel *panel = panelAt(panelId);
  if (!panel || panel->model)
    return;

  DirectoryModel *model = new DirectoryModel(m_core, this);
  // One toolbar API surface: keep the controller's view options in step when
  // any panel's model changes them through its own QML setters.
  connect(model, &DirectoryModel::showHiddenChanged,
          this, &ShellController::showHiddenChanged);
  connect(model, &DirectoryModel::folderSizesChanged,
          this, &ShellController::folderSizesChanged);
  connect(model, &DirectoryModel::sortKeyChanged,
          this, &ShellController::sortKeyChanged);

  auto *watcher = new DirectoryWatcher(this);
  // Each watcher refreshes only its own panel. It is deleted with the model,
  // so the connection cannot outlive the pointer it captures.
  connect(watcher, &DirectoryWatcher::changed, this, [model] {
    model->refresh();
  });
  // Follow the rows actually on screen: pathChanged fires when a scan replaces
  // them with another directory's entries, including on a tab switch.
  connect(model, &DirectoryModel::pathChanged, this,
          [watcher](const QString &path) {
    if (path.isEmpty())
      watcher->unwatch();
    else
      watcher->watch(path);
  });

  QPointer<DirectoryModel> weak = model;
  connect(model, &DirectoryModel::navigateFailed, this,
          [this, weak](const QString &failedPath) {
    DirectoryModel *failedModel = weak.data();
    if (!failedModel)
      return;
    for (Panel &candidate : m_tree.panels()) {
      if (candidate.model != failedModel || candidate.currentTab < 0
          || candidate.currentTab >= candidate.tabs.size())
        continue;
      PanelTab &tab = candidate.tabs[candidate.currentTab];
      const QString restoredPath = failedModel->path();
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
      emitPanelsChanged();
      emitHistory();
      return;
    }
  });

  panel->model = model;
  panel->watcher = watcher;
}

void ShellController::destroyPanelResources(Panel &panel)
{
  // Stop the watcher first so no queued refresh can reach a dying model.
  delete panel.watcher;
  panel.watcher = nullptr;
  delete panel.model;
  panel.model = nullptr;
}

Panel *ShellController::panelAt(int id)
{
  if (id < 0 || id >= m_tree.panels().size())
    return nullptr;
  return &m_tree.panels()[id];
}

const Panel *ShellController::panelAt(int id) const
{
  if (id < 0 || id >= m_tree.panels().size())
    return nullptr;
  return &m_tree.panels().at(id);
}

DirectoryModel *ShellController::activeModel() const
{
  const Panel *panel = panelAt(m_tree.activePanel());
  return panel ? panel->model : nullptr;
}

PanelTab *ShellController::activeTab()
{
  Panel *panel = panelAt(m_tree.activePanel());
  if (!panel || panel->currentTab < 0 || panel->currentTab >= panel->tabs.size())
    return nullptr;
  return &panel->tabs[panel->currentTab];
}

const PanelTab *ShellController::activeTab() const
{
  const Panel *panel = panelAt(m_tree.activePanel());
  if (!panel || panel->currentTab < 0 || panel->currentTab >= panel->tabs.size())
    return nullptr;
  return &panel->tabs.at(panel->currentTab);
}

void ShellController::syncCurrentTab()
{
  const Panel *panel = panelAt(m_tree.activePanel());
  const int value = panel ? panel->currentTab : -1;
  if (m_currentTab.exchange(value) != value)
    emit currentTabChanged();
}

void ShellController::emitPanelsChanged()
{
  syncCurrentTab();
  emit panelsChanged();
  emit tabsChanged();  // the shipped QML still binds to the flat tab strip
}

DirectoryModel *ShellController::directory() const
{
  return activeModel();
}

PlacesModel *ShellController::places() const
{
  return m_places;
}

QVariantList ShellController::panels() const
{
  const QVector<QRect> rects = m_tree.layout(m_contentSize);
  QVariantList result;
  result.reserve(m_tree.panels().size());
  for (int i = 0; i < m_tree.panels().size(); ++i) {
    const Panel &panel = m_tree.panels().at(i);
    QVariantList tabs;
    tabs.reserve(panel.tabs.size());
    for (const PanelTab &tab : panel.tabs) {
      tabs.append(QVariantMap{{QStringLiteral("label"), labelForPath(tab.path)},
                              {QStringLiteral("path"), tab.path}});
    }
    const QRect &rect = rects.at(i);
    result.append(QVariantMap{
        {QStringLiteral("id"), i},
        {QStringLiteral("tabs"), tabs},
        {QStringLiteral("currentTab"), panel.currentTab},
        {QStringLiteral("model"), QVariant::fromValue(static_cast<QObject *>(panel.model))},
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    });
  }
  return result;
}

int ShellController::activePanel() const
{
  return m_tree.activePanel();
}

QStringList ShellController::tabs() const
{
  QStringList result;
  const Panel *panel = panelAt(m_tree.activePanel());
  if (!panel)
    return result;
  result.reserve(panel->tabs.size());
  for (const PanelTab &tab : panel->tabs)
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
  const PanelTab *tab = activeTab();
  return tab && tab->historyIndex > 0;
}

bool ShellController::canGoForward() const
{
  const PanelTab *tab = activeTab();
  return tab && tab->historyIndex >= 0
      && tab->historyIndex + 1 < tab->history.size();
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

bool ShellController::showHidden() const
{
  DirectoryModel *model = activeModel();
  return model ? model->showHidden() : false;
}

bool ShellController::folderSizes() const
{
  DirectoryModel *model = activeModel();
  return model ? model->folderSizes() : false;
}

QString ShellController::sortKey() const
{
  DirectoryModel *model = activeModel();
  return model ? model->sortKey() : QStringLiteral("name");
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

void ShellController::setShowHidden(bool show)
{
  if (DirectoryModel *model = activeModel())
    model->setShowHidden(show);
}

void ShellController::setFolderSizes(bool on)
{
  if (DirectoryModel *model = activeModel())
    model->setFolderSizes(on);
}

void ShellController::setSortKey(const QString &key)
{
  if (DirectoryModel *model = activeModel())
    model->setSortKey(key);
}

HermesCore *ShellController::core() const
{
  return m_core;
}

bool ShellController::createFolder(const QString &name)
{
  DirectoryModel *model = activeModel();
  const QByteArray parent = model ? model->path().toUtf8() : QByteArray();
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

bool ShellController::resolveRawPaths(const QStringList &displayedPaths,
                                      QVector<QByteArray> &rawPaths)
{
  rawPaths.clear();
  rawPaths.reserve(displayedPaths.size());
  // The selection came from the active panel's view, so that is the only model
  // that can map a displayed name back to its raw bytes.
  DirectoryModel *model = activeModel();
  for (const QString &path : displayedPaths) {
    QByteArray raw;
    const DirectoryModel::RawPathResult result = model
        ? model->rawPathFor(path, &raw)
        : DirectoryModel::RawPathResult::NotListed;
    if (result == DirectoryModel::RawPathResult::Ambiguous) {
      emit operationFailed(QStringLiteral(
          "Entries with the same displayed name cannot be told apart; the operation was not started."));
      return false;
    }
    // NotListed paths did not come from this model, so their own bytes are all
    // the caller can mean.
    if (raw.isEmpty())
      raw = path.toUtf8();
    if (raw.isEmpty()) {
      reportLaunchFailure(this, QStringLiteral("identifying the entries"));
      return false;
    }
    rawPaths.append(raw);
  }
  return true;
}

bool ShellController::copyRawEntries(const QVector<QByteArray> &rawPaths,
                                     const QString &destDir, bool move)
{
  const QByteArray utf8Dest = destDir.toUtf8();
  const QVector<const char *> pointers = rawPointers(rawPaths);
  if (utf8Dest.isEmpty() || pointers.size() != rawPaths.size()) {
    reportLaunchFailure(this, QStringLiteral("copying entries"));
    return false;
  }
  if (hermes_core_copy(m_core, pointers.constData(),
                       static_cast<size_t>(pointers.size()), utf8Dest.constData(),
                       move, m_conflictPolicy) != 0) {
    reportLaunchFailure(this, QStringLiteral("copying entries"));
    return false;
  }
  return true;
}

bool ShellController::renameEntry(const QString &from, const QString &to)
{
  DirectoryModel *model = activeModel();
  const QString fromParent = QDir::cleanPath(QFileInfo(from).absolutePath());
  const QString toParent = QDir::cleanPath(QFileInfo(to).absolutePath());
  if (!validLeafName(QFileInfo(to).fileName())
      || fromParent != toParent || !model
      || fromParent != QDir::cleanPath(model->path())) {
    emit operationFailed(QStringLiteral("Rename must use a single name in the displayed directory."));
    return false;
  }
  QVector<QByteArray> rawPaths;
  if (!resolveRawPaths(QStringList({from}), rawPaths))
    return false;
  const QByteArray &rawFrom = rawPaths.constFirst();
  const qsizetype slash = rawFrom.lastIndexOf('/');
  if (slash <= 0) {
    emit operationFailed(QStringLiteral("Rename must use a single name in the displayed directory."));
    return false;
  }
  const QByteArray rawTo = rawFrom.left(slash) + "/" + QFileInfo(to).fileName().toUtf8();
  if (hermes_core_rename(m_core, rawFrom.constData(), rawTo.constData()) != 0) {
    reportLaunchFailure(this, QStringLiteral("renaming the entry"));
    return false;
  }
  return true;
}

bool ShellController::removeEntries(const QStringList &paths, bool trash)
{
  QVector<QByteArray> rawPaths;
  if (!resolveRawPaths(paths, rawPaths))
    return false;
  const QVector<const char *> pointers = rawPointers(rawPaths);
  if (pointers.size() != rawPaths.size()) {
    reportLaunchFailure(this, QStringLiteral("removing entries"));
    return false;
  }
  if (hermes_core_remove(m_core, pointers.constData(),
                         static_cast<size_t>(pointers.size()), trash) != 0) {
    reportLaunchFailure(this, QStringLiteral("removing entries"));
    return false;
  }
  return true;
}

bool ShellController::copyEntries(const QStringList &paths, const QString &destDir, bool move)
{
  QVector<QByteArray> rawPaths;
  if (!resolveRawPaths(paths, rawPaths))
    return false;
  return copyRawEntries(rawPaths, destDir, move);
}

void ShellController::copySelection(const QStringList &paths, bool move)
{
  QVector<QByteArray> rawPaths;
  if (!resolveRawPaths(paths, rawPaths))
    return;
  m_clipboard = rawPaths;
  m_clipboardMove = move;
  emit clipboardChanged();
}

void ShellController::pasteIntoCurrentDirectory()
{
  DirectoryModel *model = activeModel();
  if (!model || m_clipboard.isEmpty())
    return;
  copyRawEntries(m_clipboard, model->path(), m_clipboardMove);
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
  for (Panel &panel : m_tree.panels())
    if (panel.model)
      panel.model->refresh();
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
  DirectoryModel *model = activeModel();
  if (!model || path.isEmpty() || !model->navigate(path))
    return false;
  PanelTab *tab = activeTab();
  if (!tab)
    return false;
  if (tab->path != path)
    tab->path = path;
  if (record) {
    if (tab->historyIndex >= 0 && tab->historyIndex + 1 < tab->history.size())
      tab->history.remove(tab->historyIndex + 1, tab->history.size() - tab->historyIndex - 1);
    tab->history.append(path);
    tab->historyIndex = tab->history.size() - 1;
  } else if (historyIndex >= 0) {
    tab->historyIndex = historyIndex;
  }
  emitPanelsChanged();
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
      const PanelTab *tab = activeTab();
      if (!tab || tab->path.isEmpty())
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

void ShellController::insertTabInPanel(int panelId, const QString &path, int index, bool focus)
{
  Panel *panel = panelAt(panelId);
  if (!panel || path.isEmpty())
    return;

  PanelTab tab;
  tab.path = path;
  tab.history.append(path);
  tab.historyIndex = 0;

  // The tree starts with one empty tab. The first open fills it instead of
  // leaving a blank tab in the strip, as the legacy does.
  if (index < 0 && panel->tabs.size() == 1 && panel->tabs.first().path.isEmpty()) {
    panel->tabs[0] = tab;
    index = 0;
  } else {
    if (index < 0 || index > panel->tabs.size())
      index = panel->tabs.size();
    panel->tabs.insert(index, tab);
  }

  if (focus || panel->currentTab < 0) {
    panel->currentTab = index;
    if (m_tree.activePanel() != panelId) {
      m_tree.setActivePanel(panelId);
      emit activePanelChanged();
    }
    if (panel->model)
      panel->model->navigate(path);
    emitHistory();
  } else if (panel->currentTab >= index) {
    ++panel->currentTab;
  }
  emitPanelsChanged();
}

void ShellController::openTabFromHost(const QString &path, int index, bool focus)
{
  // The engine knows nothing about panels, so its open_tab hook lands in the
  // active panel.
  insertTabInPanel(m_tree.activePanel(), path, index, focus);
}

void ShellController::newTab(const QString &path)
{
  insertTabInPanel(m_tree.activePanel(), path, -1, true);
}

void ShellController::newTabInPanel(int panelId)
{
  const Panel *panel = panelAt(panelId);
  if (!panel || panel->tabs.isEmpty() || panel->currentTab < 0
      || panel->currentTab >= panel->tabs.size())
    return;
  insertTabInPanel(panelId, panel->tabs.at(panel->currentTab).path, -1, true);
}

int ShellController::splitPanel(int panelId, PanelTree::Zone zone, int tabIndex,
                                const QString &path)
{
  const Panel *sourcePanel = panelAt(panelId);
  if (!sourcePanel)
    return -1;
  const int sourceTabCount = sourcePanel->tabs.size();
  QString openPath = path;
  if (tabIndex < 0 && openPath.isEmpty() && sourcePanel->currentTab >= 0
      && sourcePanel->currentTab < sourcePanel->tabs.size())
    openPath = sourcePanel->tabs.at(sourcePanel->currentTab).path;

  // The tree owns the tab bookkeeping; it returns -1 both for a seventeenth
  // panel and for a move that would empty the source panel.
  const int created = m_tree.splitPanel(panelId, zone, tabIndex, openPath);
  if (created < 0) {
    if (tabIndex >= 0 && sourceTabCount < 2)
      emit operationFailed(QStringLiteral("A panel must keep at least one tab."));
    else
      emit operationFailed(QStringLiteral("Too many panels."));
    return -1;
  }

  createPanelResources(created);
  Panel &fresh = *panelAt(created);
  if (fresh.model && !fresh.tabs.isEmpty())
    fresh.model->navigate(fresh.tabs.first().path);

  // Moving a tab can change which tab the source panel shows; that model has
  // to follow its new tab too.
  if (tabIndex >= 0) {
    Panel &remaining = *panelAt(panelId);
    if (remaining.model && remaining.currentTab >= 0
        && remaining.currentTab < remaining.tabs.size())
      remaining.model->navigate(remaining.tabs.at(remaining.currentTab).path);
  }

  emitPanelsChanged();
  emit activePanelChanged();
  emitHistory();
  return created;
}

void ShellController::splitActiveRight()
{
  splitPanel(m_tree.activePanel(), PanelTree::Zone::Right, -1, QString());
}

void ShellController::splitActiveDown()
{
  splitPanel(m_tree.activePanel(), PanelTree::Zone::Bottom, -1, QString());
}

void ShellController::openInSplit(const QString &path, bool right)
{
  splitPanel(m_tree.activePanel(),
             right ? PanelTree::Zone::Right : PanelTree::Zone::Bottom, -1, path);
}

void ShellController::moveTabToSplit(int panelId, int tabIndex, bool right)
{
  const Panel *panel = panelAt(panelId);
  if (!panel || tabIndex < 0 || tabIndex >= panel->tabs.size())
    return;
  // Let the tree refuse the move when this is the panel's last tab; the shared
  // split path turns that refusal into the same operationFailed message.
  splitPanel(panelId, right ? PanelTree::Zone::Right : PanelTree::Zone::Bottom,
             tabIndex, QString());
}

void ShellController::closeWholePanel(int panelId)
{
  Panel *panel = panelAt(panelId);
  if (!panel || m_tree.panels().size() < 2)
    return;
  destroyPanelResources(*panel);
  m_tree.closePanel(panelId);
  emitPanelsChanged();
  emit activePanelChanged();
  emitHistory();
}

void ShellController::closeTab(int panelId, int tabIndex)
{
  Panel *panel = panelAt(panelId);
  if (!panel || tabIndex < 0 || tabIndex >= panel->tabs.size())
    return;
  if (panel->tabs.size() == 1) {
    // Closing a panel's last tab closes the panel; the tree gives its space to
    // the sibling of its leaf and activates that subtree's first panel.
    closeWholePanel(panelId);
    return;
  }
  DirectoryModel *model = panel->model;
  panel->tabs.remove(tabIndex);
  if (panel->currentTab > tabIndex)
    --panel->currentTab;
  else if (panel->currentTab >= panel->tabs.size())
    panel->currentTab = panel->tabs.size() - 1;

  // Even when the panel is not showing, its model must follow its own tab;
  // activating a panel later cannot repair rows from a tab that no longer exists.
  if (model) {
    const PanelTab &tab = panel->tabs.at(panel->currentTab);
    if (model->path() != tab.path)
      model->navigate(tab.path);
  }
  if (panelId == m_tree.activePanel())
    emitHistory();
  emitPanelsChanged();
}

void ShellController::closeTab(int index)
{
  closeTab(m_tree.activePanel(), index);
}

void ShellController::selectTab(int panelId, int tabIndex)
{
  Panel *panel = panelAt(panelId);
  if (!panel || tabIndex < 0 || tabIndex >= panel->tabs.size())
    return;
  panel->currentTab = tabIndex;
  if (m_tree.activePanel() != panelId) {
    m_tree.setActivePanel(panelId);
    emit activePanelChanged();
  }
  // The panel's own model follows the tab; navigate keeps the old rows on
  // screen until the new ones arrive, so the view never blanks on a switch.
  const PanelTab &tab = panel->tabs.at(tabIndex);
  if (panel->model && panel->model->path() != tab.path)
    panel->model->navigate(tab.path);
  emitPanelsChanged();
  emitHistory();
}

void ShellController::selectTab(int index)
{
  selectTab(m_tree.activePanel(), index);
}

void ShellController::activatePanel(int panelId)
{
  if (!panelAt(panelId) || m_tree.activePanel() == panelId)
    return;
  m_tree.setActivePanel(panelId);
  emit activePanelChanged();
  emitPanelsChanged();
  emitHistory();
}

void ShellController::cyclePanel(int direction)
{
  const QVector<int> order = m_tree.panelsInReadingOrder();
  if (order.isEmpty())
    return;
  int position = order.indexOf(m_tree.activePanel());
  if (position < 0)
    position = 0;
  const int count = order.size();
  const int next = ((position + direction) % count + count) % count;
  activatePanel(order.at(next));
}

void ShellController::setContentSize(int width, int height)
{
  const QSize size(qMax(0, width), qMax(0, height));
  if (m_contentSize == size)
    return;
  m_contentSize = size;
  emit panelsChanged();
}

void ShellController::up()
{
  DirectoryModel *model = activeModel();
  const QString current = model ? model->path() : QString();
  if (current.isEmpty())
    return;
  const QString parent = QFileInfo(current).absolutePath();
  if (parent != current)
    navigate(parent);
}

void ShellController::back()
{
  const PanelTab *tab = activeTab();
  if (!tab)
    return;
  const int index = tab->historyIndex - 1;
  if (index < 0 || index >= tab->history.size())
    return;
  goTo(tab->history.at(index), false, index);
}

void ShellController::forward()
{
  const PanelTab *tab = activeTab();
  if (!tab || tab->historyIndex < 0 || tab->historyIndex + 1 >= tab->history.size())
    return;
  const int index = tab->historyIndex + 1;
  goTo(tab->history.at(index), false, index);
}
