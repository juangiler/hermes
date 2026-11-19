#pragma once
#include "core_api.h"
#include "core_host.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <atomic>

class DirectoryModel;
class OpenUriClient;
class PlacesModel;

// Install the Qt host table before any HermesCore is created. The controller
// also does this in its constructor so standalone consumers (tests) get hooks.
void installShellCoreHost();

class ShellController : public QObject
{
  Q_OBJECT

  Q_PROPERTY(DirectoryModel *directory READ directory CONSTANT)
  Q_PROPERTY(PlacesModel *places READ places CONSTANT)
  Q_PROPERTY(QStringList tabs READ tabs NOTIFY tabsChanged)
  Q_PROPERTY(int currentTab READ currentTab NOTIFY currentTabChanged)
  Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
  Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
  Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
  Q_PROPERTY(int viewMode READ viewMode NOTIFY viewModeChanged)   // 0 list, 1 details, 2 grid
  Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)  // each: {key, width}
  Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)             // 0.6 .. 2.5

public:
  explicit ShellController(QObject *parent = nullptr);
  ~ShellController() override;

  DirectoryModel *directory() const;
  PlacesModel *places() const;
  QStringList tabs() const;
  int currentTab() const;
  bool canGoBack() const;
  bool canGoForward() const;
  bool canPaste() const;
  int viewMode() const;
  QVariantList columns() const;
  double zoom() const;
  HermesCore *core() const;

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

  // Called from the queued operations_changed hook on the GUI thread.
  void recordOperationCompletion(int kind, int errorCount);
  void refreshDirectory();
  int operationsChangedCount() const;
  void resetOperationsChangedCount();

public slots:
  void setPaths(const QStringList &paths);
  void navigate(const QString &path);
  void openEntry(const QString &path);
  void newTab(const QString &path);
  void closeTab(int index);
  void selectTab(int index);
  void up();
  void back();
  void forward();

signals:
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

private:
  struct TabState {
    QString path;
    QStringList history;
    int historyIndex = -1;
  };

  bool goTo(const QString &path, bool record, int historyIndex = -2);
  void emitHistory();

  HermesCore *m_core = nullptr;
  DirectoryModel *m_directory = nullptr;
  PlacesModel *m_places = nullptr;
  OpenUriClient *m_openUri = nullptr;
  QVector<TabState> m_tabs;
  QStringList m_clipboard;
  bool m_clipboardMove = false;
  HermesConflictPolicy m_conflictPolicy = HERMES_CONFLICT_RENAME;
  int m_viewMode = 0;
  double m_zoom = 1.0;
  QVariantList m_columns;
  std::atomic<int> m_operationsChangedCount{0};
  std::atomic<int> m_currentTab{-1};
};
