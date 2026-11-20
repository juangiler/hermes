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

// The legacy's disp_name(): a trash directory has its own name, a remote path
// loses its trailing "::", and everything else is named by its base name.
QString labelForPath(const QString &path)
{
  if (path.isEmpty())
    return QString();
  const QString trash = QDir::cleanPath(
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
      + QStringLiteral("/Trash/files"));
  if (QDir::cleanPath(path) == trash)
    return QStringLiteral("Trash");
  const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString label = parts.isEmpty() ? QStringLiteral("/") : parts.last();
  if (label.size() > 2 && label.endsWith(QLatin1String("::")))
    label.chop(2);
  return label;
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
}  // namespace

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
    // An operation may have changed entries in any pane of any tab.
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

}  // extern "C"

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
      // One worker per pane: collect every pane's results on the GUI thread.
      // constData()/at() avoid QList's detaching non-const iterators, which
      // would require TabState to be copyable (PaneTree is move-only).
      for (int tabIndex = 0; tabIndex < controller->m_tabs.size(); ++tabIndex) {
        const TabState &tab = controller->m_tabs.at(tabIndex);
        for (const Pane &pane : tab.tree->panes())
          if (pane.model)
            pane.model->pollScan();
      }
    }, Qt::QueuedConnection);
  }, this);

  TabState first;
  m_tabs.append(std::move(first));
  createPaneResources(0, 0);
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
  for (int i = 0; i < m_tabs.size(); ++i) {
    for (Pane &pane : tabAt(i)->tree->panes())
      if (pane.model)
        pane.model->stopWorker();
  }
  hermes_core_destroy(m_core);
  m_core = nullptr;
}

void ShellController::createPaneResources(int tabIndex, int paneId)
{
  Pane *pane = paneAt(tabIndex, paneId);
  if (!pane || pane->model)
    return;

  DirectoryModel *model = new DirectoryModel(m_core, this);
  // One toolbar API surface: keep the controller's view options in step when
  // any pane's model changes them through its own QML setters.
  connect(model, &DirectoryModel::showHiddenChanged,
          this, &ShellController::showHiddenChanged);
  connect(model, &DirectoryModel::folderSizesChanged,
          this, &ShellController::folderSizesChanged);
  connect(model, &DirectoryModel::sortKeyChanged,
          this, &ShellController::sortKeyChanged);

  auto *watcher = new DirectoryWatcher(this);
  // Each watcher refreshes only its own pane. It is deleted with the model,
  // so the connection cannot outlive the pointer it captures.
  connect(watcher, &DirectoryWatcher::changed, this, [model] {
    model->refresh();
  });
  // Follow the rows actually on screen: pathChanged fires when a scan replaces
  // them with another directory's entries.
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
    for (int tabIndex = 0; tabIndex < m_tabs.size(); ++tabIndex) {
      TabState &tab = *tabAt(tabIndex);
      for (Pane &candidate : tab.tree->panes()) {
        if (candidate.model != failedModel)
          continue;
        PaneState &state = candidate.state;
        if (state.path != failedPath)
          return;
        const QString restoredPath = failedModel->path();
        if (restoredPath.isEmpty() || restoredPath == failedPath)
          return;
        int historyIndex = -1;
        for (int i = 0; i < state.history.size(); ++i)
          if (state.history.at(i) == restoredPath)
            historyIndex = i;
        if (historyIndex < 0)
          return;
        state.path = restoredPath;
        state.history.remove(historyIndex + 1,
                             state.history.size() - historyIndex - 1);
        state.historyIndex = historyIndex;
        emitTabsChanged();
        emitHistory();
        return;
      }
    }
  });

  pane->model = model;
  pane->watcher = watcher;
  if (!pane->state.path.isEmpty())
    model->navigate(pane->state.path);
}

void ShellController::destroyPaneResources(Pane &pane)
{
  // Stop the watcher first so no queued refresh can reach a dying model.
  delete pane.watcher;
  pane.watcher = nullptr;
  delete pane.model;
  pane.model = nullptr;
}

TabState *ShellController::tabAt(int index)
{
  if (index < 0 || index >= m_tabs.size())
    return nullptr;
  // const access never detaches; the stored objects are themselves non-const.
  return const_cast<TabState *>(&m_tabs.at(index));
}

const TabState *ShellController::tabAt(int index) const
{
  if (index < 0 || index >= m_tabs.size())
    return nullptr;
  return &m_tabs.at(index);
}

Pane *ShellController::paneAt(int tabIndex, int paneId)
{
  TabState *tab = tabAt(tabIndex);
  if (!tab || paneId < 0 || paneId >= tab->tree->panes().size())
    return nullptr;
  return &tab->tree->panes()[paneId];
}

const Pane *ShellController::paneAt(int tabIndex, int paneId) const
{
  const TabState *tab = tabAt(tabIndex);
  if (!tab || paneId < 0 || paneId >= tab->tree->panes().size())
    return nullptr;
  return &tab->tree->panes().at(paneId);
}

Pane *ShellController::focusedPane()
{
  return paneAt(m_active_tab, tabAt(m_active_tab)
                ? tabAt(m_active_tab)->tree->focusedPane() : -1);
}

const Pane *ShellController::focusedPane() const
{
  const TabState *tab = tabAt(m_active_tab);
  return paneAt(m_active_tab, tab ? tab->tree->focusedPane() : -1);
}

PaneState *ShellController::focusedState()
{
  Pane *pane = focusedPane();
  return pane ? &pane->state : nullptr;
}

const PaneState *ShellController::focusedState() const
{
  const Pane *pane = focusedPane();
  return pane ? &pane->state : nullptr;
}

DirectoryModel *ShellController::activeModel() const
{
  const Pane *pane = focusedPane();
  return pane ? pane->model : nullptr;
}

void ShellController::syncCurrentTab()
{
  // The engine reads this on its own thread; only the GUI thread writes it.
  m_currentTab.store(m_active_tab);
}

void ShellController::emitTabsChanged()
{
  syncCurrentTab();
  emit tabsChanged();
}

int ShellController::activeTab() const
{
  return m_active_tab;
}

QVariantList ShellController::tabs() const
{
  QVariantList result;
  result.reserve(m_tabs.size());
  for (int i = 0; i < m_tabs.size(); ++i) {
    const TabState &tab = m_tabs.at(i);
    const int paneId = tab.tree->focusedPane();
    const Pane *pane = paneAt(i, paneId);
    const QString path = pane ? pane->state.path : QString();
    const QString label = tab.customName.isEmpty()
        ? labelForPath(path) : tab.customName;
    const QVariantMap entry{
        {QStringLiteral("label"), label},
        {QStringLiteral("customName"), tab.customName},
        {QStringLiteral("multi"), tab.tree->panes().size() > 1},
        {QStringLiteral("focusedPane"), paneId},
        {QStringLiteral("path"), path},
        {QStringLiteral("index"), i},
    };
    result.append(entry);
  }
  return result;
}

QVariantList ShellController::panes() const
{
  QVariantList result;
  const TabState *tab = tabAt(m_active_tab);
  if (!tab)
    return result;
  const QVector<QRect> rects = tab->tree->layout(m_contentSize);
  const QVector<Pane> &panes = tab->tree->panes();
  result.reserve(panes.size());
  for (int i = 0; i < panes.size(); ++i) {
    const Pane &pane = panes.at(i);
    const QRect &rect = rects.at(i);
    result.append(QVariantMap{
        {QStringLiteral("id"), i},
        {QStringLiteral("path"), pane.state.path},
        {QStringLiteral("model"), QVariant::fromValue(static_cast<QObject *>(pane.model))},
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    });
  }
  return result;
}

QStringList ShellController::selectedPaths(int paneId) const
{
  const Pane *pane = paneAt(m_active_tab, paneId);
  return pane ? pane->state.selectedPaths : QStringList();
}

void ShellController::setSelectedPaths(int paneId, const QStringList &paths)
{
  Pane *pane = paneAt(m_active_tab, paneId);
  if (!pane || pane->state.selectedPaths == paths)
    return;
  pane->state.selectedPaths = paths;
  emitTabsChanged();
}

int ShellController::currentIndex(int paneId) const
{
  const Pane *pane = paneAt(m_active_tab, paneId);
  return pane ? pane->state.currentIndex : -1;
}

void ShellController::setCurrentIndex(int paneId, int index)
{
  Pane *pane = paneAt(m_active_tab, paneId);
  if (!pane || pane->state.currentIndex == index)
    return;
  pane->state.currentIndex = index;
  emitTabsChanged();
}

int ShellController::anchorIndex(int paneId) const
{
  const Pane *pane = paneAt(m_active_tab, paneId);
  return pane ? pane->state.anchorIndex : -1;
}

void ShellController::setAnchorIndex(int paneId, int index)
{
  Pane *pane = paneAt(m_active_tab, paneId);
  if (!pane || pane->state.anchorIndex == index)
    return;
  pane->state.anchorIndex = index;
  emitTabsChanged();
}

int ShellController::currentTab() const
{
  return m_currentTab.load();
}

DirectoryModel *ShellController::directory() const
{
  return activeModel();
}

PlacesModel *ShellController::places() const
{
  return m_places;
}

bool ShellController::canPaste() const
{
  return !m_clipboard.isEmpty();
}

bool ShellController::canGoBack() const
{
  const PaneState *state = focusedState();
  return state && state->historyIndex > 0;
}

bool ShellController::canGoForward() const
{
  const PaneState *state = focusedState();
  return state && state->historyIndex >= 0
      && state->historyIndex + 1 < state->history.size();
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
  // The selection came from the focused pane's view, so that is the only model
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
  for (int i = 0; i < m_tabs.size(); ++i) {
    for (Pane &pane : tabAt(i)->tree->panes())
      if (pane.model)
        pane.model->refresh();
  }
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
  PaneState *state = focusedState();
  if (!state)
    return false;
  if (state->path != path)
    state->path = path;
  state->selectedPaths.clear();
  state->currentIndex = 0;
  state->anchorIndex = 0;
  if (record) {
    if (state->historyIndex >= 0 && state->historyIndex + 1 < state->history.size())
      state->history.remove(state->historyIndex + 1,
                            state->history.size() - state->historyIndex - 1);
    state->history.append(path);
    state->historyIndex = state->history.size() - 1;
  } else if (historyIndex >= 0) {
    state->historyIndex = historyIndex;
  }
  emitTabsChanged();
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
      const PaneState *state = focusedState();
      if (!state || state->path.isEmpty())
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

void ShellController::insertTab(const QString &path, int index, bool focus)
{
  if (path.isEmpty())
    return;

  TabState tab;
  PaneState &state = tab.tree->panes()[0].state;
  state.path = path;
  state.history.append(path);
  state.historyIndex = 0;

  // The first blank tab is filled rather than left behind in the strip.
  int target = index;
  bool inserted = false;
  if (m_tabs.size() == 1 && tabAt(0)->tree->panes()[0].state.path.isEmpty()) {
    *tabAt(0) = std::move(tab);
    target = 0;
  } else {
    if (target < 0 || target > m_tabs.size())
      target = m_tabs.size();
    m_tabs.insert(target, std::move(tab));
    inserted = true;
  }

  createPaneResources(target, 0);  // every tab's pane follows its own directory

  if (focus || m_active_tab < 0) {
    m_active_tab = target;
  } else if (inserted && m_active_tab >= target) {
    // Only a real insertion shifts the existing tabs; filling the blank tab
    // replaces tab 0 in place and must leave the active index alone.
    ++m_active_tab;
  }
  emitTabsChanged();
  emit activeTabChanged();
  emit focusedPaneChanged();
  emitHistory();
}

void ShellController::openTabFromHost(const QString &path, int index, bool focus)
{
  // The engine knows nothing about tabs or panes; its queued hook lands here
  // on the GUI thread and opens a window-level tab.
  insertTab(path, index, focus);
}

void ShellController::newTab(const QString &path)
{
  QString target = path;
  if (target.isEmpty() && focusedState())
    target = focusedState()->path;
  insertTab(target, -1, true);
}

void ShellController::newTab()
{
  newTab(focusedState() ? focusedState()->path : QString());
}

void ShellController::selectTab(int index)
{
  const TabState *tab = tabAt(index);
  if (!tab || m_active_tab == index)
    return;
  m_active_tab = index;
  syncCurrentTab();
  emit activeTabChanged();
  emit focusedPaneChanged();
  emitTabsChanged();
  emitHistory();
}

void ShellController::closeTab(int index)
{
  if (!tabAt(index) || m_tabs.size() < 2)
    return;
  for (Pane &pane : tabAt(index)->tree->panes())
    destroyPaneResources(pane);
  // QVector::remove() and its non-const iterators instantiate copy operations,
  // but a PaneTree is move-only (its nodes are unique_ptrs). Shift by move.
  for (int i = index + 1; i < m_tabs.size(); ++i)
    *tabAt(i - 1) = std::move(*tabAt(i));
  m_tabs.resize(m_tabs.size() - 1);
  if (m_active_tab > index)
    --m_active_tab;
  else if (m_active_tab >= m_tabs.size())
    m_active_tab = m_tabs.size() - 1;
  syncCurrentTab();
  emit activeTabChanged();
  emit focusedPaneChanged();
  emitTabsChanged();
  emitHistory();
}

void ShellController::renameTab(int index, const QString &name)
{
  TabState *tab = tabAt(index);
  if (!tab || tab->customName == name)
    return;
  tab->customName = name;
  emitTabsChanged();
}

void ShellController::setFocusedPane(int paneId)
{
  TabState *tab = tabAt(m_active_tab);
  if (!tab || paneId < 0 || paneId >= tab->tree->panes().size()
      || tab->tree->focusedPane() == paneId)
    return;
  tab->tree->setFocusedPane(paneId);
  emit focusedPaneChanged();
  emitTabsChanged();
  emitHistory();
}

int ShellController::splitPane(PaneTree::Zone zone, const QString &path)
{
  TabState *tab = tabAt(m_active_tab);
  if (!tab)
    return -1;
  const int source = tab->tree->focusedPane();
  QString openPath = path;
  if (openPath.isEmpty()) {
    const Pane *pane = paneAt(m_active_tab, source);
    if (pane)
      openPath = pane->state.path;
  }
  if (tab->tree->panes().size() >= PaneTree::MaxPanes) {
    emit operationFailed(QStringLiteral("Too many panes."));
    return -1;
  }
  const int created = tab->tree->splitPane(source, zone, openPath);
  if (created < 0) {
    emit operationFailed(QStringLiteral("Too many panes."));
    return -1;
  }
  createPaneResources(m_active_tab, created);
  emitTabsChanged();
  emit focusedPaneChanged();
  emitHistory();
  return created;
}

void ShellController::splitFocusedRight()
{
  splitPane(PaneTree::Zone::Right, QString());
}

void ShellController::splitFocusedDown()
{
  splitPane(PaneTree::Zone::Bottom, QString());
}

void ShellController::openInSplit(const QString &path, bool right)
{
  splitPane(right ? PaneTree::Zone::Right : PaneTree::Zone::Bottom, path);
}

void ShellController::closeFocusedPane()
{
  TabState *tab = tabAt(m_active_tab);
  if (!tab || tab->tree->panes().size() < 2)
    return;
  const int paneId = tab->tree->focusedPane();
  const QRect rect = tab->tree->layout(m_contentSize).at(paneId);
  Pane *pane = paneAt(m_active_tab, paneId);
  if (!pane)
    return;
  DirectoryModel *model = pane->model;
  DirectoryWatcher *watcher = pane->watcher;

  // The QML close animation needs the old rectangle before the tree collapses.
  emit paneClosed(rect);
  tab->tree->closePane(paneId);
  delete watcher;
  delete model;
  emitTabsChanged();
  emit focusedPaneChanged();
  emitHistory();
}

void ShellController::cyclePane(int direction)
{
  TabState *tab = tabAt(m_active_tab);
  if (!tab)
    return;
  const QVector<int> order = tab->tree->panesInReadingOrder();
  if (order.isEmpty())
    return;
  int position = order.indexOf(tab->tree->focusedPane());
  if (position < 0)
    position = 0;
  const int count = order.size();
  const int next = ((position + direction) % count + count) % count;
  setFocusedPane(order.at(next));
}

void ShellController::setContentSize(int width, int height)
{
  const QSize size(qMax(0, width), qMax(0, height));
  if (m_contentSize == size)
    return;
  m_contentSize = size;
  emitTabsChanged();
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
  const PaneState *state = focusedState();
  if (!state)
    return;
  const int index = state->historyIndex - 1;
  if (index < 0 || index >= state->history.size())
    return;
  goTo(state->history.at(index), false, index);
}

void ShellController::forward()
{
  const PaneState *state = focusedState();
  if (!state || state->historyIndex < 0
      || state->historyIndex + 1 >= state->history.size())
    return;
  const int index = state->historyIndex + 1;
  goTo(state->history.at(index), false, index);
}
