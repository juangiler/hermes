#pragma once
#include "core_api.h"
#include "core_host.h"
#include "pane_tree.h"
#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <atomic>

class DirectoryModel;
class DirectoryWatcher;
class OpenUriClient;
class PlacesModel;

// TEMPORARY (Task 2 removes this): the shipped QML and the controller still
// speak the panel-shaped tree API. Adapt it onto PaneTree so the tree data
// change lands without rewriting the controller in the same commit.
using PanelTab = PaneState;

struct Panel {
  QVector<PanelTab> tabs;
  int currentTab = 0;
  DirectoryModel *model = nullptr;
  DirectoryWatcher *watcher = nullptr;
};

class PanelTree {
 public:
  static constexpr int MaxPanels = PaneTree::MaxPanes;

  enum class Zone { Left, Right, Top, Bottom };

  PanelTree();
  QVector<Panel> &panels();
  const QVector<Panel> &panels() const;
  int activePanel() const;
  void setActivePanel(int index);

  int splitPanel(int panelIndex, Zone zone, int tabIndex, const QString &path);
  void closePanel(int panelIndex);
  QVector<int> panelsInReadingOrder() const;
  QVector<QRect> layout(const QSize &area) const;

 private:
  PaneTree m_tree;
  QVector<Panel> m_panes;
};

// Install the Qt host table before any HermesCore is created. The controller
// also does this in its constructor so standalone consumers (tests) get hooks.
void installShellCoreHost();

class ShellController : public QObject
{
  Q_OBJECT

  // The QML still binds to these until the panel-aware QML lands; they read
  // through to the active panel. The panel properties are the real API.
  Q_PROPERTY(DirectoryModel *directory READ directory NOTIFY activePanelChanged)
  Q_PROPERTY(PlacesModel *places READ places CONSTANT)
  Q_PROPERTY(QVariantList panels READ panels NOTIFY panelsChanged)
  Q_PROPERTY(int activePanel READ activePanel NOTIFY activePanelChanged)
  Q_PROPERTY(QStringList tabs READ tabs NOTIFY tabsChanged)
  Q_PROPERTY(int currentTab READ currentTab NOTIFY currentTabChanged)
  Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
  Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
  Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
  Q_PROPERTY(int viewMode READ viewMode NOTIFY viewModeChanged)   // 0 list, 1 details, 2 grid
  Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)  // each: {key, width}
  Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)             // 0.6 .. 2.5
  Q_PROPERTY(bool showHidden READ showHidden NOTIFY showHiddenChanged)
  Q_PROPERTY(bool folderSizes READ folderSizes NOTIFY folderSizesChanged)
  Q_PROPERTY(QString sortKey READ sortKey NOTIFY sortKeyChanged)

public:
  explicit ShellController(QObject *parent = nullptr);
  ~ShellController() override;

  DirectoryModel *directory() const;  // the active panel's model
  PlacesModel *places() const;
  QVariantList panels() const;
  int activePanel() const;
  QStringList tabs() const;  // the active panel's tab paths
  int currentTab() const;    // the active panel's showing tab
  bool canGoBack() const;
  bool canGoForward() const;
  bool canPaste() const;
  int viewMode() const;
  QVariantList columns() const;
  double zoom() const;
  bool showHidden() const;
  bool folderSizes() const;
  QString sortKey() const;
  HermesCore *core() const;

  Q_INVOKABLE void activatePanel(int panelId);
  Q_INVOKABLE void selectTab(int panelId, int tabIndex);
  Q_INVOKABLE void closeTab(int panelId, int tabIndex);
  Q_INVOKABLE void newTabInPanel(int panelId);
  Q_INVOKABLE void splitActiveRight();
  Q_INVOKABLE void splitActiveDown();
  Q_INVOKABLE void openInSplit(const QString &path, bool right);
  Q_INVOKABLE void moveTabToSplit(int panelId, int tabIndex, bool right);
  Q_INVOKABLE void cyclePanel(int direction);

  // The size of the area the panels are laid out in; the QML reports it.
  Q_INVOKABLE void setContentSize(int width, int height);

  // The engine's open_tab hook arrives on this method, off the GUI thread.
  void openTabFromHost(const QString &path, int index, bool focus);

  Q_INVOKABLE bool createFolder(const QString &name);
  Q_INVOKABLE bool renameEntry(const QString &from, const QString &to);
  Q_INVOKABLE bool removeEntries(const QStringList &paths, bool trash);
  Q_INVOKABLE bool copyEntries(const QStringList &paths, const QString &destDir, bool move);
  Q_INVOKABLE void copySelection(const QStringList &paths, bool move);
  Q_INVOKABLE void pasteIntoCurrentDirectory();
  Q_INVOKABLE void setConflictPolicy(HermesConflictPolicy policy);
  Q_INVOKABLE void setViewMode(int mode);
  Q_INVOKABLE void setZoom(double zoom);
  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setFolderSizes(bool on);
  Q_INVOKABLE void setSortKey(const QString &key);

  // Called from the queued operations_changed hook on the GUI thread.
  void recordOperationCompletion(int kind, int errorCount);
  void refreshDirectory();
  int operationsChangedCount() const;
  void resetOperationsChangedCount();

public slots:
  void setPaths(const QStringList &paths);
  void navigate(const QString &path);
  void openEntry(const QString &path);
  // Active-panel shims: kept so the shipped QML and the host hook keep working
  // until the panel-aware QML binds to the commands above directly.
  void newTab(const QString &path);
  void closeTab(int index);
  void selectTab(int index);
  void up();
  void back();
  void forward();

signals:
  void panelsChanged();
  void activePanelChanged();
  void tabsChanged();
  void currentTabChanged();
  void historyChanged();
  void operationFailed(const QString &message);
  void operationCompleted(int kind, int errorCount);
  void operationFinished(int kind, bool succeeded, int errorCount);
  void clipboardChanged();
  void viewModeChanged();
  void columnsChanged();
  void zoomChanged();
  void showHiddenChanged();
  void folderSizesChanged();
  void sortKeyChanged();

private:
  bool goTo(const QString &path, bool record, int historyIndex = -2);
  void emitHistory();
  bool resolveRawPaths(const QStringList &displayedPaths, QVector<QByteArray> &rawPaths);
  bool copyRawEntries(const QVector<QByteArray> &rawPaths, const QString &destDir, bool move);

  Panel *panelAt(int id);
  const Panel *panelAt(int id) const;
  DirectoryModel *activeModel() const;
  PanelTab *activeTab();
  const PanelTab *activeTab() const;
  void syncCurrentTab();
  void emitPanelsChanged();
  void createPanelResources(int panelId);
  static void destroyPanelResources(Panel &panel);
  int splitPanel(int panelId, PanelTree::Zone zone, int tabIndex, const QString &path);
  void insertTabInPanel(int panelId, const QString &path, int index, bool focus);
  void closeWholePanel(int panelId);

  HermesCore *m_core = nullptr;
  PanelTree m_tree;
  QSize m_contentSize;
  PlacesModel *m_places = nullptr;
  OpenUriClient *m_openUri = nullptr;
  // Raw bytes, so a paste after a directory change still targets the entries
  // the user originally selected.
  QVector<QByteArray> m_clipboard;
  bool m_clipboardMove = false;
  HermesConflictPolicy m_conflictPolicy = HERMES_CONFLICT_RENAME;
  int m_viewMode = 0;
  double m_zoom = 1.0;
  QVariantList m_columns;
  std::atomic<int> m_operationsChangedCount{0};
  // A mirror of the active panel's showing tab: hostCurrentTab() reads it on
  // the engine thread, so it must not touch the tree.
  std::atomic<int> m_currentTab{-1};
};
