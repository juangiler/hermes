#pragma once
#include "core_api.h"
#include "core_host.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

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

public:
  explicit ShellController(QObject *parent = nullptr);
  ~ShellController() override;

  DirectoryModel *directory() const;
  PlacesModel *places() const;
  QStringList tabs() const;
  int currentTab() const;
  bool canGoBack() const;
  bool canGoForward() const;
  HermesCore *core() const;

  // The engine's open_tab hook arrives on this method, off the GUI thread.
  void openTabFromHost(const QString &path, int index, bool focus);

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

private:
  struct TabState {
    QString path;
    QStringList history;
    int historyIndex = -1;
  };

  void goTo(const QString &path, bool record);
  void emitHistory();

  HermesCore *m_core = nullptr;
  DirectoryModel *m_directory = nullptr;
  PlacesModel *m_places = nullptr;
  OpenUriClient *m_openUri = nullptr;
  QVector<TabState> m_tabs;
  int m_currentTab = -1;
};
