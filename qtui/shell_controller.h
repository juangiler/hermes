#pragma once
#include "core_api.h"
#include "core_host.h"
#include "pane_tree.h"
#include <QByteArray>
#include <QObject>
#include <QRect>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <atomic>

class DirectoryModel;
class DirectoryWatcher;
class OpenUriClient;
class PlacesModel;

// The window's tab strip owns one tree of panes per tab. PaneTree is move-only
// (its nodes are unique_ptrs), so QVector holds it indirectly; each TabState
// still constructs its own tree and never shares it with another tab.
struct TabState {
  QSharedPointer<PaneTree> tree;
  QString customName;

  TabState() : tree(new PaneTree) {}
};

// Install the Qt host table before any HermesCore is created. The controller
// also does this in its constructor so standalone consumers (tests) get hooks.
void installShellCoreHost();

class ShellController : public QObject
{
  Q_OBJECT

  Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)       // one map per tab
  Q_PROPERTY(int activeTab READ activeTab NOTIFY activeTabChanged)
  Q_PROPERTY(QVariantList panes READ panes NOTIFY tabsChanged)     // active tab's panes
  Q_PROPERTY(DirectoryModel *directory READ directory NOTIFY focusedPaneChanged)
  Q_PROPERTY(PlacesModel *places READ places CONSTANT)
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

  int activeTab() const;
  QVariantList tabs() const;
  QVariantList panes() const;
  DirectoryModel *directory() const;  // the focused pane's model
  PlacesModel *places() const;
  // Only the engine's off-thread host hook uses the atomic mirror; QML binds
  // to activeTab instead.
  int currentTab() const;
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

  Q_INVOKABLE void selectTab(int index);
  Q_INVOKABLE void closeTab(int index);
  Q_INVOKABLE void newTab();
  Q_INVOKABLE void renameTab(int index, const QString &name);
  Q_INVOKABLE void setFocusedPane(int paneId);
  Q_INVOKABLE void closeFocusedPane();
  Q_INVOKABLE void splitFocusedRight();
  Q_INVOKABLE void splitFocusedDown();
  Q_INVOKABLE void openInSplit(const QString &path, bool right);
  Q_INVOKABLE void cyclePane(int direction);
  Q_INVOKABLE void setContentSize(int width, int height);

  Q_INVOKABLE QStringList selectedPaths(int paneId) const;
  Q_INVOKABLE void setSelectedPaths(int paneId, const QStringList &paths);
  Q_INVOKABLE int currentIndex(int paneId) const;
  Q_INVOKABLE void setCurrentIndex(int paneId, int index);
  Q_INVOKABLE int anchorIndex(int paneId) const;
  Q_INVOKABLE void setAnchorIndex(int paneId, int index);

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
  // Opens path when given; an empty path copies the focused pane's directory.
  void newTab(const QString &path);
  void up();
  void back();
  void forward();

 signals:
  void tabsChanged();
  void activeTabChanged();
  void focusedPaneChanged();
  void paneClosed(const QRect &rect);
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

  TabState *tabAt(int index);
  const TabState *tabAt(int index) const;
  Pane *paneAt(int tabIndex, int paneId);
  const Pane *paneAt(int tabIndex, int paneId) const;
  Pane *focusedPane();
  const Pane *focusedPane() const;
  PaneState *focusedState();
  const PaneState *focusedState() const;
  DirectoryModel *activeModel() const;
  void syncCurrentTab();
  void emitTabsChanged();
  void createPaneResources(int tabIndex, int paneId);
  static void destroyPaneResources(Pane &pane);
  int splitPane(PaneTree::Zone zone, const QString &path);
  void insertTab(const QString &path, int index, bool focus);

  HermesCore *m_core = nullptr;
  QVector<TabState> m_tabs;
  int m_active_tab = 0;
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
  // A mirror of the active tab index: hostCurrentTab() reads it on the engine
  // thread and must never touch the tab vector or the pane tree.
  std::atomic<int> m_currentTab{-1};
};
