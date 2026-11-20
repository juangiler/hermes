#include "directory_model.h"
#include "panel_tree.h"
#include "places_model.h"
#include "shell_controller.h"

#include "core_api.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <QThread>
#include <fcntl.h>
#include <unistd.h>

static DirectoryModel *g_wake_model;
// The engine's wake runs off the GUI thread; a timing test needs its timestamp.
static std::atomic<qint64> g_last_wake_ns{0};

static qint64 steadyNs()
{
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      clock::now().time_since_epoch()).count();
}

static void pollTestModel(void *)
{
  g_last_wake_ns.store(steadyNs());
  QMetaObject::invokeMethod(QCoreApplication::instance(), [] {
    if (g_wake_model)
      g_wake_model->pollScan();
  }, Qt::QueuedConnection);
}

class HermesQtUiTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();
  void qmlMainLoadsOffscreen();
  void directoryModelListsSortedEntries();
  void directoryModelReportsRoles();
  void directoryModelPreservesUtf8Names();
  void directoryModelHonoursHiddenToggle();
  void directoryModelSortsByEveryKey();
  void directoryModelReportsFolderSizes();
  void folderSizesDoNotReorderSizeSortedRows();
  void directoryModelSortKeyUsesLegacyDirections();
  void directoryViewExposesModesColumnsAndZoom();
  void toolbarOptionsControlViewModel();
  void directoryPageViewModesFormatSizeAndDate();
  void directoryPageViewsShowEntriesAndScaleWithZoom();
  void directoryModelFailedNavigateKeepsPath();
  void directoryModelNavigationDoesNotBlock();
  void directoryModelMaterializesResultsOffGuiThread();
  void directoryModelAppliesAsyncResults();
  void directoryModelKeepsRowsWhenTheDirectoryDisappears();
  void directoryModelRefreshDoesNotDuplicateRows();
  void directoryModelRefreshesWhenTheDirectoryChanges();
  void hostOpenUrisCreatesTabs();
  void navigationHistoryBackForward();
  void upAndFailedPathKeepState();
  void placesIncludeHomeAndRoot();
  void navigationIgnoresStaleDirectoryResults();
  void closeTabBeforeCurrentKeepsFocusedTab();
  void focusFreeHostTabKeepsValidCursor();
  void hostOpenTabAtIndexInsertsTab();
  void hostHooksAreSafeAcrossControllerLifetime();
  void selectionUsesPathListAndClearsOnNavigation();
  void selectionSupportsRangeAndKeyboard();
  void selectionClearsOnNavigation();
  void selectionClearsWhenRowsSwapAfterSupersededNavigation();
  void failedOperationRefreshPreservesSelection();
  void directoryModelRejectsStaleScanResults();
  void operationsCreateRenameAndTrash();
  void operationsHandleUtf8Names();
  void ambiguousRawNamesRefuseDestructiveOperation();
  void createAndRenameRejectNonLeafNames();
  void operationFailuresAreReported();
  void operationsUseConfiguredConflictPolicy();
  void defaultConflictPolicyRenamesWithoutOverride();
  void copyPasteMoveRoundTrip();
  void successfulMoveClearsClipboard();
  void navigationNotifiesTabLabels();
  void failedHistoryNavigationKeepsCursor();
  void permanentDeleteRequiresConfirmation();
  void tabManagementHasReadableLabels();
  void cliHelpAndVersionNeedNoPlatformPlugin();
  void cliScreenshotWritesAnImage();
  void cliScreenshotUnscannablePathExitsNonZero();
  void panelTreeSplitsRightAndDown();
  void panelTreePlacesTheNewPanelOnTheChosenSide();
  void panelTreeMovesATabIntoTheNewPanel();
  void panelTreeRefusesTheSixteenthPanel();
  void panelTreeClosesAPanelAndTheSiblingTakesTheSpace();
  void panelTreeLayoutSplitsTheArea();
  void panelTreeKeepsTheCurrentTabWhenMovingAnEarlierTab();
  void panelTreeActivatesTheNewPanelOnSplit();
  void panelTreeClosesTheActivePanelIntoTheSiblingSubtree();
  void panelTreeRefusedSplitLeavesTheTabsUntouched();
  void controllerSplitsTheActivePanel();
  void controllerOpensAFolderInASplit();
  void controllerMovesATabIntoANewSplit();
  void controllerRefusesToMoveTheLastTab();
  void controllerClosesAPanelWithItsLastTab();
  void controllerRefusesASeventeenthPanel();
  void controllerNavigatesInTheActivePanelOnly();
  void controllerKeepsTwoPanelsOfTheSameDirectoryIndependent();
  void controllerDropsTheOldScanWhenATabSwitches();
  void controllerKeepsTheRowsWhenAPanelDirectoryDisappears();
  void controllerCyclesThePanels();
  void controllerKeepsAnInactivePanelInStepWhenItsCurrentTabCloses();

private:
  using CorePtr = std::unique_ptr<HermesCore, decltype(&hermes_core_destroy)>;
  static CorePtr makeCore();
  static bool writeFile(const QString &path, const QByteArray &data);
  static QStringList entryNames(DirectoryModel *model);
  static QByteArray fileContents(const QString &path);
  static void waitForOperation(ShellController *controller);
  // panels() is the QML-facing view of the tree; these keep the tests honest
  // about what it actually publishes rather than reaching into the controller.
  static QVariantMap controllerPanel(const ShellController &controller, int id);
  static QStringList panelTabPaths(const ShellController &controller, int id);
  static QString panelCurrentPath(const ShellController &controller, int id);
  static DirectoryModel *panelModel(const ShellController &controller, int id);
  // Build the worker's snapshot seam from a genuine engine listing.
  static DirectoryModel::ScanSnapshot testSnapshot(DirectoryModel *model,
      const QString &path, quint64 generation, const HermesListing &listing);

  QTemporaryDir m_config_dir;
  QTemporaryDir m_data_dir;
};

HermesQtUiTest::CorePtr HermesQtUiTest::makeCore()
{
  return CorePtr(hermes_core_create(pollTestModel, nullptr), &hermes_core_destroy);
}

bool HermesQtUiTest::writeFile(const QString &path, const QByteArray &data)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
    return false;
  file.close();
  return true;
}

QStringList HermesQtUiTest::entryNames(DirectoryModel *model)
{
  QStringList names;
  for (int row = 0; row < model->rowCount(); ++row)
    names.append(model->data(model->index(row), DirectoryModel::NameRole).toString());
  return names;
}

QByteArray HermesQtUiTest::fileContents(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void HermesQtUiTest::waitForOperation(ShellController *controller)
{
  // One engine op publishes exactly one operations_changed completion.
  QTRY_COMPARE_WITH_TIMEOUT(controller->operationsChangedCount(), 1, 2000);
  controller->resetOperationsChangedCount();
}

QVariantMap HermesQtUiTest::controllerPanel(const ShellController &controller, int id)
{
  for (const QVariant &value : controller.panels()) {
    const QVariantMap panel = value.toMap();
    if (panel.value(QStringLiteral("id")).toInt() == id)
      return panel;
  }
  return {};
}

QStringList HermesQtUiTest::panelTabPaths(const ShellController &controller, int id)
{
  QStringList paths;
  for (const QVariant &value : controllerPanel(controller, id)
           .value(QStringLiteral("tabs")).toList())
    paths.append(value.toMap().value(QStringLiteral("path")).toString());
  return paths;
}

QString HermesQtUiTest::panelCurrentPath(const ShellController &controller, int id)
{
  const QVariantMap panel = controllerPanel(controller, id);
  const QVariantList tabs = panel.value(QStringLiteral("tabs")).toList();
  const int current = panel.value(QStringLiteral("currentTab")).toInt();
  if (current < 0 || current >= tabs.size())
    return {};
  return tabs.at(current).toMap().value(QStringLiteral("path")).toString();
}

DirectoryModel *HermesQtUiTest::panelModel(const ShellController &controller, int id)
{
  return qobject_cast<DirectoryModel *>(
      controllerPanel(controller, id).value(QStringLiteral("model")).value<QObject *>());
}

DirectoryModel::ScanSnapshot HermesQtUiTest::testSnapshot(
    DirectoryModel *model, const QString &path, quint64 generation,
    const HermesListing &listing)
{
  DirectoryModel::ScanConfig config = model->currentConfig();
  config.path = path;
  config.previousPath = path;
  config.generation = generation;
  return DirectoryModel::makeSnapshot(config, listing, false, true, false,
      std::make_shared<std::atomic<bool>>(false));
}

void HermesQtUiTest::initTestCase()
{
  QVERIFY(m_config_dir.isValid());
  QVERIFY(m_data_dir.isValid());
  qputenv("XDG_CONFIG_HOME", m_config_dir.path().toUtf8());
  qputenv("XDG_DATA_HOME", m_data_dir.path().toUtf8());
}

void HermesQtUiTest::cleanup()
{
  g_wake_model = nullptr;
}

void HermesQtUiTest::qmlMainLoadsOffscreen()
{
  ShellController controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
}

void HermesQtUiTest::directoryModelListsSortedEntries()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));
  QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("zdir"))));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 3, 5000);
  QVERIFY(model.data(model.index(0), DirectoryModel::IsDirRole).toBool());
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), QStringLiteral("zdir"));
  QCOMPARE(model.data(model.index(1), DirectoryModel::NameRole).toString(), QStringLiteral("a.txt"));
  QCOMPARE(model.data(model.index(2), DirectoryModel::NameRole).toString(), QStringLiteral("b.txt"));

  model.setSort(HERMES_SORT_NAME, true);
  model.refresh();
  QTRY_COMPARE_WITH_TIMEOUT(entryNames(&model),
                            QStringList({QStringLiteral("zdir"),
                                         QStringLiteral("b.txt"),
                                         QStringLiteral("a.txt")}), 5000);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), QStringLiteral("zdir"));
  QCOMPARE(model.data(model.index(1), DirectoryModel::NameRole).toString(), QStringLiteral("b.txt"));
  QCOMPARE(model.data(model.index(2), DirectoryModel::NameRole).toString(), QStringLiteral("a.txt"));
}

void HermesQtUiTest::directoryModelReportsRoles()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString full = dir.filePath(QStringLiteral("a.txt"));
  QVERIFY(writeFile(full, "hello"));
  const QDateTime stamp = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
  QFile file(full);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  QCOMPARE(model.path(), dir.path());

  const QModelIndex row = model.index(0, 0);
  QCOMPARE(model.data(row, DirectoryModel::NameRole).toString(), QStringLiteral("a.txt"));
  QCOMPARE(model.data(row, DirectoryModel::PathRole).toString(), full);
  QCOMPARE(model.data(row, DirectoryModel::SizeRole).toULongLong(), 5ULL);
  QCOMPARE(model.data(row, DirectoryModel::ModifiedRole).toLongLong(), 1700000000LL);
  const int flags = model.data(row, DirectoryModel::FlagsRole).toInt();
  QVERIFY(!(flags & HERMES_ENTRY_DIR));
}

void HermesQtUiTest::directoryModelPreservesUtf8Names()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString name = QString::fromUtf8("caf\xc3\xa9.txt");
  QVERIFY(writeFile(dir.filePath(name), "x"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), name);
  QCOMPARE(model.data(model.index(0), DirectoryModel::PathRole).toString(), dir.filePath(name));
}

void HermesQtUiTest::directoryModelHonoursHiddenToggle()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("visible.txt")), "v"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral(".hidden.txt")), "h"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  // The legacy app starts with hidden entries off; the Qt model must match.
  QVERIFY(!model.showHidden());
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  QCOMPARE(entryNames(&model), QStringList({QStringLiteral("visible.txt")}));

  model.setShowHidden(true);
  QVERIFY(model.showHidden());
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
  QVERIFY(entryNames(&model).contains(QStringLiteral(".hidden.txt")));

  model.setShowHidden(false);
  QVERIFY(!model.showHidden());
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  QCOMPARE(entryNames(&model), QStringList({QStringLiteral("visible.txt")}));
}

void HermesQtUiTest::directoryModelSortsByEveryKey()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));
  QThread::msleep(1100); // the engine records creation times at whole-second precision
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "aaa"));
  QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));

  const QDateTime old = QDateTime::fromSecsSinceEpoch(1577836800, QTimeZone::UTC);
  const QDateTime newer = QDateTime::fromSecsSinceEpoch(1609459200, QTimeZone::UTC);
  for (const auto &entry : {qMakePair(QStringLiteral("a.txt"), old),
                            qMakePair(QStringLiteral("b.txt"), newer)}) {
    QFile file(dir.filePath(entry.first));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(entry.second, QFileDevice::FileModificationTime));
    const auto permissions = entry.first == QStringLiteral("a.txt")
        ? QFileDevice::ReadOwner | QFileDevice::WriteOwner
        : QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ReadGroup | QFileDevice::ReadOther;
    QVERIFY(file.setPermissions(permissions));
  }

  const QDateTime birthA = QFileInfo(dir.filePath(QStringLiteral("a.txt")))
                               .birthTime(QTimeZone::UTC);
  const QDateTime birthB = QFileInfo(dir.filePath(QStringLiteral("b.txt")))
                               .birthTime(QTimeZone::UTC);
  QString oldestCreated = QStringLiteral("a.txt");
  if (birthA.isValid() && birthB.isValid() && birthB < birthA)
    oldestCreated = QStringLiteral("b.txt");

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QSignalSpy finished(&model, &DirectoryModel::scanFinished);
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 3, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
  QCOMPARE(model.sortKeys(),
           QStringList({QStringLiteral("name"), QStringLiteral("date"),
                        QStringLiteral("type"), QStringLiteral("size"),
                        QStringLiteral("created"), QStringLiteral("ext"),
                        QStringLiteral("perms"), QStringLiteral("owner")}));

  const QStringList fixed = {QStringLiteral("sub"), QStringLiteral("a.txt"),
                             QStringLiteral("b.txt")};
  const QStringList sizeOrder = {QStringLiteral("sub"), QStringLiteral("b.txt"),
                                 QStringLiteral("a.txt")};
  const QStringList createdOrder = {QStringLiteral("sub"), oldestCreated,
                                    oldestCreated == QStringLiteral("a.txt")
                                        ? QStringLiteral("b.txt")
                                        : QStringLiteral("a.txt")};
  for (int key = HERMES_SORT_NAME; key <= HERMES_SORT_OWNER; ++key) {
    model.setSort(static_cast<HermesSortKey>(key), false);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), key + 2, 5000);
    const QStringList expected = key == HERMES_SORT_SIZE ? sizeOrder
        : key == HERMES_SORT_CREATED ? createdOrder : fixed;
    QCOMPARE(entryNames(&model), expected);
  }
}

void HermesQtUiTest::directoryModelReportsFolderSizes()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sub = dir.filePath(QStringLiteral("sub"));
  QVERIFY(QDir().mkpath(sub));
  QVERIFY(writeFile(QDir(sub).filePath(QStringLiteral("inside.txt")), "content"));
  // A zero-byte regular file has a real size of 0, not an unknown folder size.
  QVERIFY(writeFile(dir.filePath(QStringLiteral("empty.txt")), QByteArray()));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);

  auto rowOf = [&model](const QString &name) {
    for (int row = 0; row < model.rowCount(); ++row)
      if (model.data(model.index(row), DirectoryModel::NameRole).toString() == name)
        return row;
    return -1;
  };
  const int folderRow = rowOf(QStringLiteral("sub"));
  const int fileRow = rowOf(QStringLiteral("empty.txt"));
  QVERIFY(folderRow >= 0);
  QVERIFY(fileRow >= 0);
  const QModelIndex folder = model.index(folderRow);
  QCOMPARE(model.data(folder, DirectoryModel::SizeRole).toLongLong(), -1LL);
  QCOMPARE(model.data(model.index(fileRow), DirectoryModel::SizeRole).toLongLong(), 0LL);
  model.setFolderSizes(true);
  QVERIFY(model.folderSizes());
  QTRY_VERIFY_WITH_TIMEOUT(model.data(folder, DirectoryModel::SizeRole).toLongLong() > 0,
                           10000);
  QCOMPARE(model.data(model.index(fileRow), DirectoryModel::SizeRole).toLongLong(), 0LL);
}

void HermesQtUiTest::folderSizesDoNotReorderSizeSortedRows()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Names sort opposite to the eventual sizes, so the engine's size-sort
  // order changes once the folder-size pass publishes its results.
  const QString big = dir.filePath(QStringLiteral("a-big"));
  const QString small = dir.filePath(QStringLiteral("z-small"));
  QVERIFY(QDir().mkpath(big));
  QVERIFY(QDir().mkpath(small));
  QVERIFY(writeFile(QDir(big).filePath(QStringLiteral("inside.txt")),
                    QByteArray(200, 'x')));
  QVERIFY(writeFile(QDir(small).filePath(QStringLiteral("inside.txt")), QByteArray("x")));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("plain.txt")), "plain"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  model.setSort(HERMES_SORT_SIZE, false);
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 3, 5000);
  const QStringList initialOrder = entryNames(&model);

  auto sizeOf = [&model](const QString &name) {
    for (int row = 0; row < model.rowCount(); ++row) {
      if (model.data(model.index(row), DirectoryModel::NameRole).toString() == name)
        return model.data(model.index(row), DirectoryModel::SizeRole).toLongLong();
    }
    return -1LL;
  };
  model.setFolderSizes(true);
  QTRY_VERIFY_WITH_TIMEOUT(sizeOf(QStringLiteral("a-big")) > 0
                           && sizeOf(QStringLiteral("z-small")) > 0, 10000);
  QCOMPARE(entryNames(&model), initialOrder);
}

void HermesQtUiTest::directoryModelSortKeyUsesLegacyDirections()
{
  CorePtr core = makeCore();
  DirectoryModel model(core.get());

  model.setSortKey(HERMES_SORT_DATE);
  QCOMPARE(model.m_key, HERMES_SORT_DATE);
  QVERIFY(model.m_desc);
  model.setSortKey(HERMES_SORT_SIZE);
  QCOMPARE(model.m_key, HERMES_SORT_SIZE);
  QVERIFY(model.m_desc);
  model.setSortKey(HERMES_SORT_NAME);
  QCOMPARE(model.m_key, HERMES_SORT_NAME);
  QVERIFY(!model.m_desc);
  model.setSortKey(HERMES_SORT_NAME);
  QVERIFY(model.m_desc);
}

void HermesQtUiTest::directoryViewExposesModesColumnsAndZoom()
{
  ShellController controller;
  QCOMPARE(controller.viewMode(), 0);
  QCOMPARE(controller.zoom(), 1.0);

  const QVariantList columns = controller.columns();
  QCOMPARE(columns.size(), 3);
  QCOMPARE(columns.at(0).toMap().value(QStringLiteral("key")).toString(),
           QStringLiteral("name"));
  QCOMPARE(columns.at(1).toMap().value(QStringLiteral("key")).toString(),
           QStringLiteral("date"));
  QCOMPARE(columns.at(2).toMap().value(QStringLiteral("key")).toString(),
           QStringLiteral("size"));
  // Legacy defaults: the name column takes the remaining width.
  QCOMPARE(columns.at(1).toMap().value(QStringLiteral("width")).toInt(), 140);
  QCOMPARE(columns.at(2).toMap().value(QStringLiteral("width")).toInt(), 88);

  QSignalSpy modeChanged(&controller, &ShellController::viewModeChanged);
  QSignalSpy zoomChanged(&controller, &ShellController::zoomChanged);
  QVERIFY(modeChanged.isValid());
  QVERIFY(zoomChanged.isValid());

  controller.setViewMode(1);
  QCOMPARE(controller.viewMode(), 1);
  QCOMPARE(modeChanged.count(), 1);
  controller.setZoom(1.5);
  QCOMPARE(controller.zoom(), 1.5);
  QCOMPARE(zoomChanged.count(), 1);

  modeChanged.clear();
  zoomChanged.clear();
  controller.setViewMode(99);
  QCOMPARE(controller.viewMode(), 2);
  controller.setViewMode(-1);
  QCOMPARE(controller.viewMode(), 0);
  controller.setZoom(99.0);
  QCOMPARE(controller.zoom(), 2.5);
  controller.setZoom(0.1);
  QCOMPARE(controller.zoom(), 0.6);
  QCOMPARE(modeChanged.count(), 2);
  QCOMPARE(zoomChanged.count(), 2);
}

void HermesQtUiTest::toolbarOptionsControlViewModel()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("visible.txt")), "v"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral(".hidden.txt")), "h"));
  QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));

  ShellController controller;
  controller.navigate(dir.path());
  DirectoryModel *model = controller.directory();
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 5000);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();
  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);

  auto find = [window](const char *name) {
    return window->findChild<QObject *>(QString::fromLatin1(name));
  };
  // Repeater delegates are visual children of Row's content item, not QObject
  // children of the window.
  auto findVisual = [window](const QString &name) -> QObject * {
    QList<QQuickItem *> queue{window->contentItem()};
    while (!queue.isEmpty()) {
      QQuickItem *item = queue.takeFirst();
      if (item->objectName() == name)
        return item;
      queue.append(item->childItems());
    }
    return nullptr;
  };
  auto click = [](QObject *button) {
    return QMetaObject::invokeMethod(button, "click");
  };
  QObject *detailsButton = find("detailsViewButton");
  QVERIFY2(detailsButton, "detailsViewButton");
  QObject *listButton = find("listViewButton");
  QVERIFY2(listButton, "listViewButton");
  QVERIFY(click(detailsButton));
  QCOMPARE(controller.viewMode(), 1);
  QCoreApplication::processEvents();
  auto view = [page](const char *name) {
    return page->findChild<QObject *>(QString::fromLatin1(name));
  };
  QVERIFY(view("detailsView")->property("visible").toBool());
  QVERIFY(!view("listView")->property("visible").toBool());
  QVERIFY(click(listButton));
  QCOMPARE(controller.viewMode(), 0);

  // The controller must proxy the model state for a single toolbar API.
  QVERIFY(controller.property("showHidden").isValid());
  QVERIFY(!controller.property("showHidden").toBool());
  QObject *hiddenButton = find("showHiddenButton");
  QVERIFY2(hiddenButton, "showHiddenButton");
  QVERIFY(click(hiddenButton));
  QVERIFY(controller.property("showHidden").toBool());
  QVERIFY(model->showHidden());
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 5000);
  QVERIFY(entryNames(model).contains(QStringLiteral(".hidden.txt")));
  QVERIFY(click(hiddenButton));
  QVERIFY(!controller.property("showHidden").toBool());
  QVERIFY(!model->showHidden());
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 5000);
  QVERIFY(!entryNames(model).contains(QStringLiteral(".hidden.txt")));

  QVERIFY(controller.property("sortKey").isValid());
  QObject *sortButton = findVisual(QStringLiteral("sortButton_size"));
  QVERIFY2(sortButton, "sortButton_size");
  QVERIFY(click(sortButton));
  QCOMPARE(controller.property("sortKey").toString(), QStringLiteral("size"));
  QCOMPARE(model->property("sortKey").toString(), QStringLiteral("size"));
  for (const QString &key : model->sortKeys()) {
    const QString name = QStringLiteral("sortButton_") + key;
    QVERIFY2(findVisual(name), qPrintable(name));
  }

  QObject *sizesButton = find("folderSizesButton");
  QVERIFY2(sizesButton, "folderSizesButton");
  QVERIFY(click(sizesButton));
  QVERIFY(controller.property("folderSizes").toBool());
  QVERIFY(model->folderSizes());

  QObject *zoomIn = find("zoomInButton");
  QObject *zoomOut = find("zoomOutButton");
  QVERIFY2(zoomIn, "zoomInButton");
  QVERIFY2(zoomOut, "zoomOutButton");
  const double before = controller.zoom();
  QVERIFY(click(zoomIn));
  QVERIFY(controller.zoom() > before);
  QVERIFY(controller.zoom() <= 2.5);
  for (int i = 0; i < 20; ++i)
    QVERIFY(click(zoomIn));
  QVERIFY(qFuzzyCompare(controller.zoom(), 2.5));
  for (int i = 0; i < 30; ++i)
    QVERIFY(click(zoomOut));
  QVERIFY(qFuzzyCompare(controller.zoom(), 0.6));
}

void HermesQtUiTest::directoryPageViewModesFormatSizeAndDate()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  ShellController controller;
  controller.navigate(dir.path());
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());

  QObject *page = engine.rootObjects().constFirst()->findChild<QObject *>(
      QStringLiteral("directoryPage"));
  QVERIFY(page);
  for (const char *name : {"listView", "detailsView", "gridView"}) {
    QObject *view = page->findChild<QObject *>(QString::fromLatin1(name));
    QVERIFY2(view, name);
  }

  // QML functions are exposed through the meta-object with QVariant arguments.
  QVariant result;
  QVERIFY(QMetaObject::invokeMethod(page, "sizeText", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, QVariant(-1.0))));
  const QString unknown = result.toString();
  QVERIFY(!unknown.isEmpty());
  QVERIFY(unknown != QStringLiteral("0 B"));
  QVERIFY(QMetaObject::invokeMethod(page, "sizeText", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, QVariant(0.0))));
  QCOMPARE(result.toString(), QStringLiteral("0 B"));

  QVERIFY(QMetaObject::invokeMethod(page, "dateText", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, QVariant(0.0))));
  QVERIFY(result.toString().isEmpty());
  QVERIFY(QMetaObject::invokeMethod(page, "dateText", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, QVariant(1700000000.0))));
  QVERIFY(!result.toString().isEmpty());
  QVERIFY(result.toString().contains(QLatin1Char(':')));
}

void HermesQtUiTest::directoryPageViewsShowEntriesAndScaleWithZoom()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("c.txt")), "c"));
  QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("zdir"))));

  ShellController controller;
  controller.navigate(dir.path());
  DirectoryModel *model = controller.directory();
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 4, 5000);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();

  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);
  auto findView = [page](const char *name) {
    return qobject_cast<QQuickItem *>(page->findChild<QObject *>(QString::fromLatin1(name)));
  };
  QQuickItem *list = findView("listView");
  QQuickItem *details = findView("detailsView");
  QQuickItem *grid = findView("gridView");
  QVERIFY(list);
  QVERIFY(details);
  QVERIFY(grid);

  // Real rows are needed here: loading an empty directory never instantiates
  // the delegates, so the view contents were only checked by reading the QML.
  QVERIFY(list->property("visible").toBool());
  QCOMPARE(list->property("count").toInt(), 4);
  QVERIFY(!details->property("visible").toBool());
  QVERIFY(!grid->property("visible").toBool());

  const double baseRow = page->property("rowHeight").toDouble();
  const double baseCell = grid->property("cellWidth").toDouble();
  QVERIFY(baseRow > 0);
  QVERIFY(baseCell > 0);

  controller.setZoom(1.5);
  QCoreApplication::processEvents();
  QVERIFY(qFuzzyCompare(page->property("rowHeight").toDouble(), baseRow * 1.5));
  QVERIFY(qFuzzyCompare(grid->property("cellWidth").toDouble(), baseCell * 1.5));
  const double fixedWidth = page->property("fixedColumnWidth").toDouble();
  QVERIFY(qFuzzyCompare(fixedWidth, (140.0 + 88.0) * 1.5));

  controller.setViewMode(1);
  QCoreApplication::processEvents();
  QVERIFY(details->property("visible").toBool());
  QVERIFY(!list->property("visible").toBool());
  QVERIFY(!grid->property("visible").toBool());
  QCOMPARE(details->property("count").toInt(), 4);

  // Delegates are visual children of the view's content item, not QObject
  // children, so walk the visual tree to reach the instantiated columns.
  QList<QQuickItem *> texts;
  std::function<void(QQuickItem *)> collectTexts = [&texts, &collectTexts](QQuickItem *item) {
    for (QQuickItem *child : item->childItems()) {
      if (QString::fromLatin1(child->metaObject()->className()).contains("QQuickText"))
        texts.append(child);
      collectTexts(child);
    }
  };
  collectTexts(details);
  QCOMPARE(texts.size(), 12);  // four rows, one Text per configured column
  const double nameWidth = details->width() - fixedWidth;
  auto countWidths = [&texts](double expected) {
    int matches = 0;
    for (QQuickItem *text : texts) {
      if (qFuzzyCompare(text->width(), expected))
        ++matches;
    }
    return matches;
  };
  QCOMPARE(countWidths(nameWidth), 4);
  QCOMPARE(countWidths(140.0 * 1.5), 4);
  QCOMPARE(countWidths(88.0 * 1.5), 4);

  controller.setViewMode(2);
  QCoreApplication::processEvents();
  QVERIFY(grid->property("visible").toBool());
  QVERIFY(!list->property("visible").toBool());
  QVERIFY(!details->property("visible").toBool());
  QCOMPARE(grid->property("count").toInt(), 4);

  controller.setViewMode(0);
  QCoreApplication::processEvents();
  QVERIFY(list->property("visible").toBool());
  QVERIFY(!details->property("visible").toBool());
  QVERIFY(!grid->property("visible").toBool());
}

void HermesQtUiTest::directoryModelFailedNavigateKeepsPath()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "x"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  const int rows = model.rowCount();
  const QString name = model.data(model.index(0), DirectoryModel::NameRole).toString();
  const QString missing = dir.filePath(QStringLiteral("missing"));

  QSignalSpy failed(&model, &DirectoryModel::navigateFailed);
  QSignalSpy changed(&model, &DirectoryModel::pathChanged);
  QVERIFY(model.navigate(missing));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
  QCOMPARE(failed.count(), 1);
  QCOMPARE(failed.takeFirst().at(0).toString(), missing);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(model.path(), dir.path());
  QCOMPARE(model.rowCount(), rows);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), name);
}

void HermesQtUiTest::directoryModelNavigationDoesNotBlock()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for (int i = 0; i < 5000; ++i)
    QVERIFY(writeFile(dir.filePath(QStringLiteral("file-%1").arg(i)), "x"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QCOMPARE(model.rowCount(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(model.rowCount() == 5000, 10000);
}

void HermesQtUiTest::directoryModelMaterializesResultsOffGuiThread()
{
  const int count = 60000;
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for (int i = 0; i < count; ++i) {
    const QString name = QStringLiteral("file-%1-with-a-long-name-to-sort.txt")
        .arg(i, 6, 10, QLatin1Char('0'));
    QVERIFY(writeFile(dir.filePath(name), "x"));
  }

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;

  // The measurement window ends on scanFinished(), not on the QTRY poll, and
  // both endpoints are captured in the same GUI-thread slot. A later engine
  // wake therefore cannot move the start of the window.
  QVector<qint64> ticks;
  QTimer timer;
  timer.setInterval(5);
  QObject::connect(&timer, &QTimer::timeout, [&] {
    ticks.append(steadyNs());
  });
  qint64 finishNs = 0;
  qint64 wakeAtFinish = 0;
  QMetaObject::Connection finishConn;
  finishConn = QObject::connect(&model, &DirectoryModel::scanFinished, &model, [&] {
    finishNs = steadyNs();
    wakeAtFinish = g_last_wake_ns.load();
    timer.stop();
    QObject::disconnect(finishConn);
  });
  timer.start();
  QVERIFY(model.navigate(dir.path()));
  QTRY_VERIFY_WITH_TIMEOUT(model.rowCount() == count, 30000);
  QVERIFY(finishNs > 0);
  QVERIFY(wakeAtFinish > 0);

  int ticksInWindow = 0;
  qint64 lastTick = 0;
  qint64 maxGap = 0;
  for (qint64 tick : ticks) {
    if (lastTick > 0)
      maxGap = qMax(maxGap, tick - lastTick);
    lastTick = tick;
    if (wakeAtFinish < tick && tick <= finishNs)
      ++ticksInWindow;
  }
  const auto ms = [](qint64 ns) { return ns / 1000000; };
  qInfo() << "scan responsiveness: ticks" << ticks.size()
          << "in wake-to-finish window" << ticksInWindow
          << "window ms" << ms(finishNs - wakeAtFinish)
          << "max gap ms" << ms(maxGap)
          << "rows" << model.rowCount();

  QVERIFY2(ticksInWindow >= 3, qPrintable(QStringLiteral(
      "GUI thread stalled through result materialization: only %1 timer ticks "
      "between the engine wake and scanFinished").arg(ticksInWindow)));
  QVERIFY2(ms(maxGap) < 80, qPrintable(QStringLiteral(
      "GUI thread stalled for %1 ms between timer ticks").arg(ms(maxGap))));
  QCOMPARE(model.path(), dir.path());
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
           QStringLiteral("file-000000-with-a-long-name-to-sort.txt"));
  QCOMPARE(model.data(model.index(count - 1), DirectoryModel::NameRole).toString(),
           QStringLiteral("file-059999-with-a-long-name-to-sort.txt"));
}

void HermesQtUiTest::directoryModelAppliesAsyncResults()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QSignalSpy finished(&model, &DirectoryModel::scanFinished);
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
           QStringLiteral("a.txt"));
}

void HermesQtUiTest::directoryModelKeepsRowsWhenTheDirectoryDisappears()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("kept.txt")), "x"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  const QString previousPath = model.path();
  QSignalSpy failed(&model, &DirectoryModel::navigateFailed);
  QVERIFY(dir.remove());
  model.refresh();
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
           QStringLiteral("kept.txt"));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
  QCOMPARE(model.path(), previousPath);
}

void HermesQtUiTest::directoryModelRefreshDoesNotDuplicateRows()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QSignalSpy finished(&model, &DirectoryModel::scanFinished);
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
  model.refresh();
  model.refresh();
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
  QCOMPARE(model.rowCount(), 2);
}

void HermesQtUiTest::directoryModelRefreshesWhenTheDirectoryChanges()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("first.txt")), "x"));

  ShellController controller;
  controller.navigate(dir.path());
  DirectoryModel *model = controller.directory();
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);

  QVERIFY(writeFile(dir.filePath(QStringLiteral("second.txt")), "y"));
  QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() == 2, 10000);
}

void HermesQtUiTest::hostOpenUrisCreatesTabs()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());

  ShellController controller;
  const QByteArray firstUri = QUrl::fromLocalFile(first.path()).toEncoded();
  const QByteArray secondUri = QUrl::fromLocalFile(second.path()).toEncoded();
  const char *uris[] = {firstUri.constData(), secondUri.constData()};
  QCOMPARE(hermes_core_open_uris(controller.core(), uris, 2, true), 2);
  QTRY_COMPARE(controller.tabs().size(), 2);
  QVERIFY(controller.tabs().contains(first.path()));
  QVERIFY(controller.tabs().contains(second.path()));
}

void HermesQtUiTest::navigationHistoryBackForward()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString child = dir.filePath(QStringLiteral("child"));
  QVERIFY(QDir().mkpath(child));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("marker")), "x"));

  ShellController controller;
  controller.navigate(dir.path());
  QCOMPARE(controller.directory()->path(), dir.path());
  QVERIFY(!controller.canGoBack());
  QVERIFY(!controller.canGoForward());

  controller.navigate(child);
  QCOMPARE(controller.directory()->path(), child);
  QVERIFY(controller.canGoBack());
  QVERIFY(!controller.canGoForward());

  controller.back();
  QCOMPARE(controller.directory()->path(), dir.path());
  QVERIFY(!controller.canGoBack());
  QVERIFY(controller.canGoForward());

  controller.forward();
  QCOMPARE(controller.directory()->path(), child);
  QVERIFY(controller.canGoBack());
  QVERIFY(!controller.canGoForward());
}

void HermesQtUiTest::upAndFailedPathKeepState()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString child = dir.filePath(QStringLiteral("child"));
  QVERIFY(QDir().mkpath(child));

  ShellController controller;
  controller.navigate(child);
  QCOMPARE(controller.directory()->path(), child);

  controller.up();
  QCOMPARE(controller.directory()->path(), dir.path());

  controller.navigate(dir.filePath(QStringLiteral("missing")));
  QTRY_COMPARE_WITH_TIMEOUT(controller.directory()->path(), dir.path(), 5000);
  QCOMPARE(controller.directory()->path(), dir.path());
  QVERIFY(!controller.canGoForward());
}

void HermesQtUiTest::placesIncludeHomeAndRoot()
{
  PlacesModel places;
  QStringList paths;
  for (int row = 0; row < places.rowCount(); ++row)
    paths.append(places.data(places.index(row), PlacesModel::PathRole).toString());

  const QString home = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).value(0);
  QVERIFY(!home.isEmpty());
  QVERIFY(paths.contains(home));
  QVERIFY(paths.contains(QStringLiteral("/")));
  QCOMPARE(places.data(places.index(0), PlacesModel::LabelRole).toString(), QStringLiteral("Home"));
  QCOMPARE(places.data(places.index(places.rowCount() - 1), PlacesModel::LabelRole).toString(),
           QStringLiteral("File System"));
}

void HermesQtUiTest::navigationIgnoresStaleDirectoryResults()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  QVERIFY(writeFile(first.filePath(QStringLiteral("only-in-first.txt")), "x"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("b.txt")), "b"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("c.txt")), "c"));

  ShellController controller;
  controller.navigate(first.path());
  QTRY_COMPARE_WITH_TIMEOUT(controller.directory()->rowCount(), 1, 5000);

  controller.navigate(second.path());
  hermes_core_drain(controller.core());
  QTRY_COMPARE(controller.directory()->rowCount(), 3);
  QCOMPARE(controller.directory()->path(), second.path());
  for (int row = 0; row < controller.directory()->rowCount(); ++row) {
    const QString path = controller.directory()->data(
        controller.directory()->index(row), DirectoryModel::PathRole).toString();
    QCOMPARE(QFileInfo(path).absolutePath(), second.path());
  }
}

void HermesQtUiTest::closeTabBeforeCurrentKeepsFocusedTab()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QStringList paths;
  for (const char *name : {"a", "b", "c", "d"}) {
    const QString path = root.filePath(QString::fromLatin1(name));
    QVERIFY(QDir().mkpath(path));
    paths.append(path);
  }

  ShellController controller;
  for (const QString &path : paths)
    controller.newTab(path);
  QCOMPARE(panelTabPaths(controller, 0), paths);

  controller.selectTab(0, 2);
  QCOMPARE(controllerPanel(controller, 0).value(QStringLiteral("currentTab")).toInt(), 2);
  QCOMPARE(controller.directory()->path(), paths.at(2));

  controller.closeTab(0, 0);
  QCOMPARE(panelTabPaths(controller, 0),
           QStringList({paths.at(1), paths.at(2), paths.at(3)}));
  QCOMPARE(controllerPanel(controller, 0).value(QStringLiteral("currentTab")).toInt(), 1);
  QCOMPARE(controller.directory()->path(), paths.at(2));
}

void HermesQtUiTest::focusFreeHostTabKeepsValidCursor()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ShellController controller;
  const QByteArray uri = QUrl::fromLocalFile(dir.path()).toEncoded();
  const char *uris[] = {uri.constData()};
  QCOMPARE(hermes_core_open_uris(controller.core(), uris, 1, false), 1);
  QTRY_COMPARE(controllerPanel(controller, 0).value(QStringLiteral("tabs")).toList().size(), 1);
  QVERIFY(controllerPanel(controller, 0).value(QStringLiteral("currentTab")).toInt() >= 0);

  const QString child = dir.filePath(QStringLiteral("child"));
  QVERIFY(QDir().mkpath(child));
  controller.navigate(child);
  QCOMPARE(controller.directory()->path(), child);
  const int current = controllerPanel(controller, 0).value(QStringLiteral("currentTab")).toInt();
  QVERIFY(current >= 0 && current < panelTabPaths(controller, 0).size());
  QCOMPARE(panelCurrentPath(controller, 0), child);
}

void HermesQtUiTest::hostOpenTabAtIndexInsertsTab()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString first = root.filePath(QStringLiteral("first"));
  const QString inserted = root.filePath(QStringLiteral("inserted"));
  QVERIFY(QDir().mkpath(first));
  QVERIFY(QDir().mkpath(inserted));

  ShellController controller;
  controller.newTab(first);
  controller.openTabFromHost(inserted, 0, false);
  QCOMPARE(panelTabPaths(controller, 0), QStringList({inserted, first}));
  QCOMPARE(controllerPanel(controller, 0).value(QStringLiteral("currentTab")).toInt(), 1);
  QCOMPARE(controller.directory()->path(), first);
}

void HermesQtUiTest::hostHooksAreSafeAcrossControllerLifetime()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  CorePtr core = makeCore();
  QVERIFY(core);
  const QByteArray uri = QUrl::fromLocalFile(dir.path()).toEncoded();
  const char *uris[] = {uri.constData()};
  std::atomic<bool> start{false};
  std::atomic<int> calls{0};
  auto controller = std::make_unique<ShellController>();
  controller->navigate(dir.path());
  QCOMPARE(controller->panels().size(), 1);
  std::thread caller([&] {
    while (!start.load())
      std::this_thread::yield();
    for (int i = 0; i < 200000; ++i)
      if (hermes_core_open_uris(core.get(), uris, 1, false) == 1)
        calls.fetch_add(1);
  });
  start.store(true);
  while (calls.load() < 100)
    std::this_thread::yield();
  controller.reset();
  caller.join();
  hermes_core_destroy(core.release());
}

void HermesQtUiTest::selectionUsesPathListAndClearsOnNavigation()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  const QString separatorName = QStringLiteral("has") + QChar(0x1f) + QStringLiteral("separator");
  QVERIFY(writeFile(first.filePath(separatorName), "x"));

  ShellController controller;
  controller.navigate(first.path());
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();
  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);

  const QString selectedPath = first.filePath(separatorName);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(selectedPath))));
  QVariantList selected = page->property("selectedPaths").toList();
  QCOMPARE(selected.size(), 1);
  QCOMPARE(selected.constFirst().toString(), selectedPath);

  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(first.filePath(QStringLiteral("other"))))));
  QCOMPARE(page->property("selectedPaths").toList().size(), 2);
  controller.navigate(second.path());
  QCOMPARE(panelCurrentPath(controller, controller.activePanel()), second.path());
  QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedPaths").toList().isEmpty(), 5000);
  QVERIFY(page->property("selectedPaths").toList().isEmpty());
}

void HermesQtUiTest::selectionSupportsRangeAndKeyboard()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString subdirectory = QStringLiteral("zdir");
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.txt")), "b"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("c.txt")), "c"));
  QVERIFY(QDir().mkpath(dir.filePath(subdirectory)));
  // A raw byte that is not valid UTF-8: the model reports the replacement
  // character, and the page must pass that exact string through to operations.
  const QByteArray rawPath = dir.path().toUtf8() + "/\xff" "bad.txt";
  const int fd = ::open(rawPath.constData(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  QVERIFY(fd >= 0);
  QCOMPARE(::close(fd), 0);

  ShellController controller;
  controller.navigate(dir.path());
  DirectoryModel *model = controller.directory();
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 5, 5000);

  QStringList paths;
  for (int row = 0; row < model->rowCount(); ++row)
    paths.append(model->data(model->index(row), DirectoryModel::PathRole).toString());
  QStringList modelPaths;
  QVERIFY(QMetaObject::invokeMethod(model, "paths", Qt::DirectConnection,
                                    Q_RETURN_ARG(QStringList, modelPaths)));
  QCOMPARE(modelPaths, paths);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();
  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);
  QQuickItem *list = qobject_cast<QQuickItem *>(
      page->findChild<QObject *>(QStringLiteral("listView")));
  QVERIFY(list);
  // The visible view takes key focus on load; direct calls alone would not
  // prove that the shortcuts can ever reach the page.
  QTRY_VERIFY_WITH_TIMEOUT(list->hasActiveFocus(), 2000);

  auto invokeInt = [page](const char *name, int value) {
    return QMetaObject::invokeMethod(page, name, Q_ARG(QVariant, QVariant(value)));
  };
  auto selected = [page] { return page->property("selectedPaths").toList(); };
  auto expectPaths = [&selected](const QStringList &expected) {
    QVariantList actual;
    for (const QString &path : expected)
      actual.append(path);
    QCOMPARE(selected(), actual);
  };

  QVERIFY(invokeInt("setCurrentIndex", 0));
  QCOMPARE(page->property("currentIndex").toInt(), 0);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(paths.first()))));
  QVERIFY(invokeInt("selectRange", 3));
  // The model lists the subdirectory first, so the range spans the first
  // four rows: subdirectory plus the three regular files.
  expectPaths(paths.mid(0, 4));

  QVERIFY(QMetaObject::invokeMethod(page, "clearSelection"));
  QTest::keyClick(window, Qt::Key_Down);
  QCOMPARE(page->property("currentIndex").toInt(), 1);
  QTest::keyClick(window, Qt::Key_End);
  QCOMPARE(page->property("currentIndex").toInt(), paths.size() - 1);
  QTest::keyClick(window, Qt::Key_Home);
  QCOMPARE(page->property("currentIndex").toInt(), 0);
  QTest::keyClick(window, Qt::Key_Space);
  expectPaths(QStringList(paths.first()));
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  expectPaths(paths);

  QVERIFY(QMetaObject::invokeMethod(page, "clearSelection"));
  int invalidRow = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->data(model->index(row), DirectoryModel::NameRole).toString()
        .contains(QChar(0xfffd))) {
      invalidRow = row;
      break;
    }
  }
  QVERIFY(invalidRow >= 0);
  const QString invalidPath = model->data(model->index(invalidRow),
                                          DirectoryModel::PathRole).toString();
  QVERIFY(invalidPath.contains(QChar(0xfffd)));
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(invalidPath))));
  expectPaths(QStringList(invalidPath));

  const int subdirRow = paths.indexOf(dir.filePath(subdirectory));
  QVERIFY(subdirRow >= 0);
  QVERIFY(invokeInt("setCurrentIndex", subdirRow));
  QCOMPARE(page->property("currentIndex").toInt(), subdirRow);
  QVERIFY(QMetaObject::invokeMethod(page, "activateCurrent"));
  QTRY_COMPARE_WITH_TIMEOUT(model->path(), dir.filePath(subdirectory), 5000);
  // Remove the raw-byte name explicitly: QTemporaryDir cannot enumerate it.
  QCOMPARE(::unlink(rawPath.constData()), 0);
}

void HermesQtUiTest::selectionClearsOnNavigation()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  const QString selectedPath = first.filePath(QStringLiteral("file.txt"));
  QVERIFY(writeFile(selectedPath, "x"));

  ShellController controller;
  controller.navigate(first.path());
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();
  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(selectedPath))));
  QCOMPARE(page->property("selectedPaths").toList().size(), 1);

  controller.navigate(second.path());
  QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedPaths").toList().isEmpty(), 5000);
  QVERIFY(page->property("selectedPaths").toList().isEmpty());
}

void HermesQtUiTest::selectionClearsWhenRowsSwapAfterSupersededNavigation()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  const QString selectedPath = first.filePath(QStringLiteral("selected.txt"));
  QVERIFY(writeFile(selectedPath, "x"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("second.txt")), "y"));

  ShellController controller;
  controller.navigate(first.path());
  QTRY_COMPARE_WITH_TIMEOUT(controller.directory()->rowCount(), 1, 5000);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QObject *page = engine.rootObjects().constFirst()->findChild<QObject *>(
      QStringLiteral("directoryPage"));
  QVERIFY(page);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(selectedPath))));
  QCOMPARE(page->property("selectedPaths").toList().size(), 1);

  controller.navigate(second.path());
  controller.refreshDirectory();
  DirectoryModel *model = controller.directory();
  model->cancelScan();

  const QByteArray name("second.txt");
  HermesListing listing{};
  listing.items = static_cast<HermesEntry *>(calloc(1, sizeof(HermesEntry)));
  QVERIFY(listing.items);
  listing.count = 1;
  listing.items[0].name = strdup(name.constData());
  QVERIFY(listing.items[0].name);
  listing.items[0].name_len = static_cast<size_t>(name.size());
  QVERIFY(model->applySnapshot(testSnapshot(model, second.path(),
                                            model->m_generation, listing)));
  hermes_listing_free(&listing);
  QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedPaths").toList().isEmpty(), 5000);
}

void HermesQtUiTest::failedOperationRefreshPreservesSelection()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString selectedPath = dir.filePath(QStringLiteral("file.txt"));
  QVERIFY(writeFile(selectedPath, "x"));

  ShellController controller;
  controller.navigate(dir.path());
  QCOMPARE(panelCurrentPath(controller, 0), dir.path());
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();

  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(selectedPath))));
  QCOMPARE(page->property("selectedPaths").toList().size(), 1);

  controller.recordOperationCompletion(HERMES_OP_DELETE, 1);
  controller.refreshDirectory();
  QCOMPARE(page->property("selectedPaths").toList().size(), 1);
}

void HermesQtUiTest::directoryModelRejectsStaleScanResults()
{
  QTemporaryDir current;
  QTemporaryDir other;
  QVERIFY(current.isValid());
  QVERIFY(other.isValid());
  QVERIFY(writeFile(current.filePath(QStringLiteral("one.txt")), "1"));
  QVERIFY(writeFile(current.filePath(QStringLiteral("two.txt")), "2"));
  QVERIFY(writeFile(other.filePath(QStringLiteral("three.txt")), "3"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  g_wake_model = &model;
  QVERIFY(model.navigate(current.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
  const QString firstName = model.data(model.index(0), DirectoryModel::NameRole).toString();

  // Only the generation/path guard may reject these genuinely scanned results.
  HermesListing listing{};
  const QByteArray currentPath = current.path().toUtf8();
  QCOMPARE(hermes_core_scan_sorted(core.get(), currentPath.constData(),
                                   HERMES_SORT_NAME, false, &listing), 0);
  QVERIFY(listing.count == 2);
  QVERIFY(!model.applySnapshot(testSnapshot(&model, current.path(),
                                            model.m_generation - 1, listing)));
  QCOMPARE(model.path(), current.path());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), firstName);

  QVERIFY(!model.applySnapshot(testSnapshot(&model, other.path(),
                                            model.m_generation, listing)));
  QCOMPARE(model.path(), current.path());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), firstName);
  hermes_listing_free(&listing);
}

void HermesQtUiTest::operationsCreateRenameAndTrash()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  ShellController controller;
  controller.navigate(dir.path());

  const QString folder = dir.filePath(QStringLiteral("folder"));
  QVERIFY(controller.createFolder(QStringLiteral("folder")));
  waitForOperation(&controller);
  QVERIFY(QFileInfo::exists(folder));
  QVERIFY(entryNames(controller.directory()).contains(QStringLiteral("folder")));

  const QString renamed = dir.filePath(QStringLiteral("renamed"));
  QVERIFY(controller.renameEntry(folder, renamed));
  waitForOperation(&controller);
  QVERIFY(!QFileInfo::exists(folder));
  QVERIFY(entryNames(controller.directory()).contains(QStringLiteral("renamed")));

  QVERIFY(controller.removeEntries(QStringList({renamed}), true));
  waitForOperation(&controller);
  QVERIFY(!QFileInfo::exists(renamed));
  QVERIFY(!entryNames(controller.directory()).contains(QStringLiteral("renamed")));
}

void HermesQtUiTest::operationsHandleUtf8Names()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  ShellController controller;
  controller.navigate(dir.path());

  const QString name = QString::fromUtf8("caf\xc3\xa9");
  QVERIFY(controller.createFolder(name));
  waitForOperation(&controller);
  QVERIFY(QFileInfo::exists(dir.filePath(name)));
  QVERIFY(entryNames(controller.directory()).contains(name));

  const QString renamed = name + QStringLiteral("-renamed");
  QVERIFY(controller.renameEntry(dir.filePath(name), dir.filePath(renamed)));
  waitForOperation(&controller);
  QVERIFY(!entryNames(controller.directory()).contains(name));
  QVERIFY(entryNames(controller.directory()).contains(renamed));
  QVERIFY(QFileInfo::exists(dir.filePath(renamed)));
}

void HermesQtUiTest::ambiguousRawNamesRefuseDestructiveOperation()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  // Distinct raw names that both decode to the same displayed U+FFFD spelling.
  const QByteArray rawName("a\xff" "b");
  const QByteArray replacementName = QByteArray("a") + QByteArray::fromHex("efbfbd") + "b";
  const QByteArray rawFirst = first.path().toUtf8() + "/" + rawName;
  const QByteArray replacementFirst = first.path().toUtf8() + "/" + replacementName;
  const QByteArray rawSecond = second.path().toUtf8() + "/" + rawName;
  auto makeRawFile = [](const QByteArray &path) {
    const int fd = ::open(path.constData(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
      return false;
    const bool written = ::write(fd, "x", 1) == 1;
    return ::close(fd) == 0 && written;
  };
  QVERIFY(makeRawFile(rawFirst));
  QVERIFY(makeRawFile(replacementFirst));
  QVERIFY(makeRawFile(rawSecond));

  ShellController controller;
  controller.navigate(first.path());
  DirectoryModel *model = controller.directory();
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 5000);
  QString displayed;
  int sharedRows = 0;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString path = model->data(model->index(row), DirectoryModel::PathRole).toString();
    if (displayed.isEmpty())
      displayed = path;
    if (path == displayed)
      ++sharedRows;
  }
  QCOMPARE(sharedRows, 2);
  QVERIFY(displayed.contains(QChar(0xfffd)));

  QSignalSpy failures(&controller, &ShellController::operationFailed);
  QVERIFY(!controller.removeEntries(QStringList({displayed}), false));
  QCOMPARE(failures.count(), 1);
  QCOMPARE(controller.operationsChangedCount(), 0);
  QCOMPARE(::access(rawFirst.constData(), F_OK), 0);
  QCOMPARE(::access(replacementFirst.constData(), F_OK), 0);

  controller.navigate(second.path());
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);
  const QString unique = model->data(model->index(0), DirectoryModel::PathRole).toString();
  QVERIFY(controller.removeEntries(QStringList({unique}), false));
  waitForOperation(&controller);
  QVERIFY(::access(rawSecond.constData(), F_OK) != 0);

  // QTemporaryDir cannot enumerate these raw byte names during its cleanup.
  QCOMPARE(::unlink(rawFirst.constData()), 0);
  QCOMPARE(::unlink(replacementFirst.constData()), 0);
}

void HermesQtUiTest::createAndRenameRejectNonLeafNames()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString dir = root.filePath(QStringLiteral("inside"));
  QVERIFY(QDir().mkpath(dir));
  ShellController controller;
  controller.navigate(dir);

  QVERIFY(!controller.createFolder(QStringLiteral("../escaped")));
  QVERIFY(!controller.createFolder(QStringLiteral("nested/name")));
  QVERIFY(!controller.createFolder(QStringLiteral(".")));
  QVERIFY(!controller.createFolder(QStringLiteral("..")));
  QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("escaped"))));

  const QString source = dir + QStringLiteral("/source");
  QVERIFY(writeFile(source, "x"));
  QVERIFY(!controller.renameEntry(source, dir + QStringLiteral("/../escaped")));
  QVERIFY(!controller.renameEntry(source, dir + QStringLiteral("/nested/name")));
  QVERIFY(!controller.renameEntry(source, dir + QStringLiteral("/.")));
  QVERIFY(QFileInfo::exists(source));
  QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("escaped"))));

  const QString outsideSource = root.filePath(QStringLiteral("outside"));
  QVERIFY(writeFile(outsideSource, "outside"));
  QVERIFY(!controller.renameEntry(outsideSource, root.filePath(QStringLiteral("renamed"))));
  QVERIFY(QFileInfo::exists(outsideSource));
}

void HermesQtUiTest::operationFailuresAreReported()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  ShellController controller;
  controller.navigate(dir.path());
  QSignalSpy failures(&controller, SIGNAL(operationFailed(QString)));
  QVERIFY(failures.isValid());

  QVERIFY(!controller.createFolder(QString()));
  QCOMPARE(failures.count(), 1);

  QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("already"))));
  QVERIFY(controller.createFolder(QStringLiteral("already")));
  QTRY_COMPARE_WITH_TIMEOUT(controller.operationsChangedCount(), 1, 2000);
  QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 2, 2000);
  QVERIFY(failures.takeLast().at(0).toString().contains(QStringLiteral("failure")));
}

void HermesQtUiTest::operationsUseConfiguredConflictPolicy()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sourceDir = dir.filePath(QStringLiteral("src"));
  const QString destDir = dir.filePath(QStringLiteral("dest"));
  QVERIFY(QDir().mkpath(sourceDir));
  QVERIFY(QDir().mkpath(destDir));
  const QString source = sourceDir + QStringLiteral("/existing.txt");
  const QString original = destDir + QStringLiteral("/existing.txt");
  QVERIFY(writeFile(source, "source"));
  QVERIFY(writeFile(original, "original"));

  ShellController controller;
  controller.navigate(destDir);
  controller.setConflictPolicy(HERMES_CONFLICT_RENAME);
  const QStringList paths({source});
  QVERIFY(controller.copyEntries(paths, destDir, false));
  waitForOperation(&controller);
  QVERIFY(controller.copyEntries(paths, destDir, false));
  waitForOperation(&controller);

  // CF_RENAME moves each conflicting file aside; neither source nor the
  // original destination content may be silently overwritten.
  QCOMPARE(fileContents(destDir + QStringLiteral("/existing (2).txt")), QByteArray("original"));
  QCOMPARE(fileContents(destDir + QStringLiteral("/existing (3).txt")), QByteArray("source"));
  QCOMPARE(fileContents(original), QByteArray("source"));
  QCOMPARE(fileContents(source), QByteArray("source"));
}

void HermesQtUiTest::defaultConflictPolicyRenamesWithoutOverride()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString sourceDir = dir.filePath(QStringLiteral("src"));
  const QString destDir = dir.filePath(QStringLiteral("dest"));
  QVERIFY(QDir().mkpath(sourceDir));
  QVERIFY(QDir().mkpath(destDir));
  const QString source = sourceDir + QStringLiteral("/existing.txt");
  const QString original = destDir + QStringLiteral("/existing.txt");
  QVERIFY(writeFile(source, "source"));
  QVERIFY(writeFile(original, "original"));

  ShellController controller;
  QVERIFY(controller.copyEntries(QStringList({source}), destDir, false));
  waitForOperation(&controller);
  QCOMPARE(fileContents(destDir + QStringLiteral("/existing (2).txt")), QByteArray("original"));
  QCOMPARE(fileContents(original), QByteArray("source"));
}

void HermesQtUiTest::copyPasteMoveRoundTrip()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString destDir = dir.filePath(QStringLiteral("dest"));
  QVERIFY(QDir().mkpath(destDir));
  const QString source = dir.filePath(QStringLiteral("file.txt"));
  QVERIFY(writeFile(source, "payload"));

  ShellController controller;
  controller.navigate(dir.path());
  controller.copySelection(QStringList({source}), false);
  controller.navigate(destDir);
  controller.pasteIntoCurrentDirectory();
  waitForOperation(&controller);
  QCOMPARE(fileContents(destDir + QStringLiteral("/file.txt")), QByteArray("payload"));
  QVERIFY(QFileInfo::exists(source));

  controller.copySelection(QStringList({source}), true);
  controller.pasteIntoCurrentDirectory();
  waitForOperation(&controller);
  QVERIFY(!QFileInfo::exists(source));
  QCOMPARE(fileContents(destDir + QStringLiteral("/file.txt")), QByteArray("payload"));
}

void HermesQtUiTest::successfulMoveClearsClipboard()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString destDir = dir.filePath(QStringLiteral("dest"));
  QVERIFY(QDir().mkpath(destDir));
  const QString source = dir.filePath(QStringLiteral("file.txt"));
  QVERIFY(writeFile(source, "original"));

  ShellController controller;
  controller.navigate(dir.path());
  controller.copySelection(QStringList({source}), true);
  controller.navigate(destDir);
  controller.pasteIntoCurrentDirectory();
  waitForOperation(&controller);
  QVERIFY(!QFileInfo::exists(source));

  QVERIFY(writeFile(source, "reused"));
  controller.pasteIntoCurrentDirectory();
  QCoreApplication::processEvents();
  QCOMPARE(controller.operationsChangedCount(), 0);
  QCOMPARE(fileContents(source), QByteArray("reused"));
  QCOMPARE(fileContents(destDir + QStringLiteral("/file.txt")), QByteArray("original"));
}

void HermesQtUiTest::navigationNotifiesTabLabels()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  ShellController controller;
  controller.navigate(first.path());
  QSignalSpy changed(&controller, &ShellController::panelsChanged);

  controller.navigate(second.path());
  QCOMPARE(changed.count(), 1);
  // The label belongs to the active panel's showing tab and follows its path.
  const QVariantList tabs = controllerPanel(controller, controller.activePanel())
      .value(QStringLiteral("tabs")).toList();
  QCOMPARE(tabs.size(), 1);
  const QVariantMap tab = tabs.constFirst().toMap();
  QCOMPARE(tab.value(QStringLiteral("path")).toString(), second.path());
  QCOMPARE(tab.value(QStringLiteral("label")).toString(), QFileInfo(second.path()).fileName());
}

void HermesQtUiTest::failedHistoryNavigationKeepsCursor()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString child = root.filePath(QStringLiteral("child"));
  QVERIFY(QDir().mkpath(child));
  ShellController controller;
  controller.navigate(root.path());
  controller.navigate(child);
  controller.navigate(root.path());
  QVERIFY(QDir(child).removeRecursively());
  QVERIFY(controller.canGoBack());

  controller.back();
  QTRY_COMPARE_WITH_TIMEOUT(controller.directory()->path(), root.path(), 5000);
  QCOMPARE(controller.directory()->path(), root.path());
  QVERIFY(controller.canGoBack());
  QVERIFY(!controller.canGoForward());
}

void HermesQtUiTest::permanentDeleteRequiresConfirmation()
{
  ShellController controller;
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("file.txt"));
  QVERIFY(writeFile(path, "x"));
  controller.navigate(dir.path());
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QCoreApplication::processEvents();
  QObject *page = window->findChild<QObject *>(QStringLiteral("directoryPage"));
  QVERIFY(page);
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(path))));
  QObject *button = window->findChild<QObject *>(QStringLiteral("deleteButton"));
  QVERIFY(button);
  QVERIFY(QMetaObject::invokeMethod(button, "click"));
  QObject *dialog = window->findChild<QObject *>(QStringLiteral("deleteConfirmDialog"));
  QVERIFY(dialog);
  QVERIFY(dialog->property("visible").toBool());

  QObject *confirm = window->findChild<QObject *>(QStringLiteral("confirmDeleteButton"));
  QVERIFY(confirm);
  QVERIFY(QMetaObject::invokeMethod(confirm, "click"));
  QTRY_COMPARE_WITH_TIMEOUT(controller.operationsChangedCount(), 1, 2000);
  QTRY_VERIFY(!dialog->property("visible").toBool());
  QVERIFY(!QFileInfo::exists(path));
  QVERIFY(page->property("selectedPaths").toList().isEmpty());

  const QString second = dir.filePath(QStringLiteral("second.txt"));
  QVERIFY(writeFile(second, "x"));
  QVERIFY(QMetaObject::invokeMethod(page, "toggleSelection",
                                    Q_ARG(QVariant, QVariant(second))));
  QVERIFY(QMetaObject::invokeMethod(button, "click"));
  QVERIFY(dialog->property("visible").toBool());
  dialog->setProperty("pending", true);
  controller.recordOperationCompletion(HERMES_OP_DELETE, 1);
  QObject *errorDialog = window->findChild<QObject *>(QStringLiteral("operationErrorDialog"));
  QVERIFY(errorDialog);
  QVERIFY(errorDialog->property("visible").toBool());
  QVERIFY(dialog->property("visible").toBool());
  QCOMPARE(page->property("selectedPaths").toList().size(), 1);
}

void HermesQtUiTest::tabManagementHasReadableLabels()
{
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString first = root.filePath(QStringLiteral("first"));
  const QString second = root.filePath(QStringLiteral("second"));
  QVERIFY(QDir().mkpath(first));
  QVERIFY(QDir().mkpath(second));
  ShellController controller;
  controller.navigate(first);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
  QObject *window = engine.rootObjects().constFirst();
  // Show the window before driving the toolbar. An unshown window leaves the
  // Kirigami page disabled on KF6 6.13, and a disabled button ignores
  // programmatic clicks, so the tab would never be created there.
  if (QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window)) {
    quickWindow->resize(800, 600);
    quickWindow->show();
  }
  QCoreApplication::processEvents();

  QObject *newTab = window->findChild<QObject *>(QStringLiteral("newTabButton"));
  QVERIFY(newTab);
  QVERIFY(QMetaObject::invokeMethod(newTab, "click"));
  QCOMPARE(controller.tabs().size(), 2);
  QCoreApplication::processEvents();
  QObject *paste = window->findChild<QObject *>(QStringLiteral("pasteButton"));
  QVERIFY(paste);
  QVERIFY(!paste->property("enabled").toBool());
  QObject *repeater = window->findChild<QObject *>(QStringLiteral("tabRepeater"));
  QVERIFY(repeater);
  QCOMPARE(repeater->property("count").toInt(), 2);
  QQuickItem *label = nullptr;
  QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Qt::DirectConnection,
                                    Q_RETURN_ARG(QQuickItem *, label), Q_ARG(int, 0)));
  QVERIFY(label);
  QCOMPARE(label->objectName(), QStringLiteral("tabLabel_0"));
  QCOMPARE(label->property("text").toString(), QStringLiteral("first"));
  QObject *close = label->findChild<QObject *>(QStringLiteral("closeTabButton_0"));
  QVERIFY(close);
  QVERIFY(QMetaObject::invokeMethod(close, "click"));
  QCOMPARE(controller.tabs().size(), 1);
}

void HermesQtUiTest::cliHelpAndVersionNeedNoPlatformPlugin()
{
  const QString executable = QCoreApplication::applicationDirPath()
      + QStringLiteral("/hermes-qt");
  for (const QString &flag : {QStringLiteral("--help"), QStringLiteral("--version")}) {
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("plugin-does-not-exist"));
    process.setProcessEnvironment(environment);
    process.start(executable, QStringList({flag}));
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitCode(), 0);
    const QByteArray output = process.readAllStandardOutput();
    if (flag == QStringLiteral("--version"))
      QVERIFY(output.contains(QByteArray(HERMES_APP_VERSION)));
    else
      QVERIFY(output.contains("Usage:"));
  }
}

void HermesQtUiTest::cliScreenshotWritesAnImage()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "a"));

  const QString executable = QCoreApplication::applicationDirPath()
      + QStringLiteral("/hermes-qt");
  const QString shot = dir.filePath(QStringLiteral("shot.png"));
  QProcess process;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  process.setProcessEnvironment(environment);
  process.start(executable, QStringList({QStringLiteral("--screenshot"), shot, dir.path()}));
  QVERIFY(process.waitForFinished(10000));
  QCOMPARE(process.exitCode(), 0);

  QVERIFY(QFileInfo::exists(shot));
  QVERIFY(QFileInfo(shot).size() > 0);
  QImage image(shot);
  QVERIFY(!image.isNull());
  QCOMPARE(image.size(), QSize(1280, 800));
}

void HermesQtUiTest::cliScreenshotUnscannablePathExitsNonZero()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString executable = QCoreApplication::applicationDirPath()
      + QStringLiteral("/hermes-qt");
  const QString shot = dir.filePath(QStringLiteral("missing-shot.png"));
  QProcess process;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  process.setProcessEnvironment(environment);
  process.start(executable, QStringList({QStringLiteral("--screenshot"), shot,
                                         dir.filePath(QStringLiteral("missing"))}));
  // A timeout is a failure: the debug surface must not idle forever on a path
  // that can never produce a scan result.
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(1000);
    QFAIL("screenshot mode hung on an unscannable path");
  }
  QVERIFY(process.exitCode() != 0);
  QVERIFY(!QFileInfo::exists(shot));
}

void HermesQtUiTest::panelTreeSplitsRightAndDown()
{
  PanelTree tree;
  QCOMPARE(tree.panels().size(), 1);
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QStringLiteral("/tmp")), 1);
  QCOMPARE(tree.panels().size(), 2);
  QCOMPARE(tree.root()->kind, PanelNode::HSplit);
  QVERIFY(qFuzzyCompare(tree.root()->ratio, 0.5));
  QCOMPARE(tree.panels().at(1).tabs.at(0).path, QStringLiteral("/tmp"));

  // The pinned zone is Bottom; it is the vertical counterpart of Right.
  PanelTree vertical;
  QCOMPARE(vertical.splitPanel(0, PanelTree::Zone::Bottom, -1, QStringLiteral("/var")), 1);
  QCOMPARE(vertical.root()->kind, PanelNode::VSplit);
  QVERIFY(qFuzzyCompare(vertical.root()->ratio, 0.5));
}

void HermesQtUiTest::panelTreePlacesTheNewPanelOnTheChosenSide()
{
  PanelTree left;
  left.splitPanel(0, PanelTree::Zone::Left, -1, QString());
  QCOMPARE(left.panelsInReadingOrder(), QVector<int>({1, 0}));

  PanelTree right;
  right.splitPanel(0, PanelTree::Zone::Right, -1, QString());
  QCOMPARE(right.panelsInReadingOrder(), QVector<int>({0, 1}));

  PanelTree top;
  top.splitPanel(0, PanelTree::Zone::Top, -1, QString());
  QCOMPARE(top.panelsInReadingOrder(), QVector<int>({1, 0}));

  PanelTree bottom;
  bottom.splitPanel(0, PanelTree::Zone::Bottom, -1, QString());
  QCOMPARE(bottom.panelsInReadingOrder(), QVector<int>({0, 1}));
}

void HermesQtUiTest::panelTreeMovesATabIntoTheNewPanel()
{
  PanelTree tree;
  tree.panels()[0].tabs.append(PanelTab{});
  tree.panels()[0].tabs[0].path = QStringLiteral("/first");
  tree.panels()[0].tabs[1].path = QStringLiteral("/second");

  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, 0, QString()), 1);
  QCOMPARE(tree.panels().size(), 2);
  QCOMPARE(tree.panels().at(0).tabs.size(), 1);
  QCOMPARE(tree.panels().at(0).tabs.at(0).path, QStringLiteral("/second"));
  QCOMPARE(tree.panels().at(1).tabs.size(), 1);
  QCOMPARE(tree.panels().at(1).tabs.at(0).path, QStringLiteral("/first"));

  // Moving a panel's only tab would leave it empty, so the split must refuse.
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, 0, QString()), -1);
  QCOMPARE(tree.panels().size(), 2);
}

void HermesQtUiTest::panelTreeRefusesTheSixteenthPanel()
{
  PanelTree tree;
  for (int i = 1; i < PanelTree::MaxPanels; ++i)
    QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QString()), i);
  QCOMPARE(tree.panels().size(), PanelTree::MaxPanels);

  const QVector<int> order = tree.panelsInReadingOrder();
  const QVector<QRect> rects = tree.layout(QSize(800, 600));
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QString()), -1);
  QCOMPARE(tree.panels().size(), PanelTree::MaxPanels);
  QCOMPARE(tree.panelsInReadingOrder(), order);
  QCOMPARE(tree.layout(QSize(800, 600)), rects);
}

void HermesQtUiTest::panelTreeClosesAPanelAndTheSiblingTakesTheSpace()
{
  PanelTree tree;
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QStringLiteral("/new")), 1);
  QCOMPARE(tree.layout(QSize(800, 600)).at(0).width(), 400);
  QCOMPARE(tree.layout(QSize(800, 600)).at(1).width(), 399);

  tree.closePanel(0);
  QCOMPARE(tree.panels().size(), 1);
  QCOMPARE(tree.root()->kind, PanelNode::PanelLeaf);
  QCOMPARE(tree.root()->panelIndex, 0);
  QCOMPARE(tree.layout(QSize(800, 600)).at(0), QRect(0, 0, 800, 600));
  QCOMPARE(tree.panels().at(0).tabs.at(0).path, QStringLiteral("/new"));

  // Removing a leaf deeper in the tree must not disturb the split above it.
  PanelTree deep;
  deep.splitPanel(0, PanelTree::Zone::Right, -1, QStringLiteral("/first"));
  deep.splitPanel(1, PanelTree::Zone::Right, -1, QStringLiteral("/second"));
  QCOMPARE(deep.panelsInReadingOrder(), QVector<int>({0, 1, 2}));
  deep.closePanel(1);
  QCOMPARE(deep.panels().size(), 2);
  QCOMPARE(deep.panelsInReadingOrder(), QVector<int>({0, 1}));
  QCOMPARE(deep.root()->kind, PanelNode::HSplit);
  QCOMPARE(deep.root()->a->panelIndex, 0);
  QCOMPARE(deep.root()->b->panelIndex, 1);
  QCOMPARE(deep.panels().at(1).tabs.at(0).path, QStringLiteral("/second"));
  QCOMPARE(deep.layout(QSize(800, 600)).at(1).width(), 399);
}

void HermesQtUiTest::panelTreeLayoutSplitsTheArea()
{
  PanelTree horizontal;
  horizontal.splitPanel(0, PanelTree::Zone::Right, -1, QString());
  const QVector<QRect> h = horizontal.layout(QSize(800, 600));
  QCOMPARE(h.size(), 2);
  QCOMPARE(h.at(0), QRect(0, 0, 400, 600));
  QCOMPARE(h.at(1), QRect(401, 0, 399, 600));

  PanelTree vertical;
  vertical.splitPanel(0, PanelTree::Zone::Bottom, -1, QString());
  const QVector<QRect> v = vertical.layout(QSize(800, 600));
  QCOMPARE(v.size(), 2);
  QCOMPARE(v.at(0), QRect(0, 0, 800, 300));
  QCOMPARE(v.at(1), QRect(0, 301, 800, 299));
}

void HermesQtUiTest::panelTreeKeepsTheCurrentTabWhenMovingAnEarlierTab()
{
  const auto fill = [](PanelTree &tree, int count) {
    tree.panels()[0].tabs.clear();
    for (int i = 0; i < count; ++i) {
      PanelTab tab;
      tab.path = QStringLiteral("/%1").arg(i);
      tree.panels()[0].tabs.append(tab);
    }
  };

  // The cursor belongs to its tab: moving tab 0 out of [A,B,C,D] leaves C at
  // index 1, so the panel must follow it rather than keep pointing at D.
  PanelTree earlier;
  fill(earlier, 4);
  earlier.panels()[0].currentTab = 2;
  QCOMPARE(earlier.splitPanel(0, PanelTree::Zone::Right, 0, QString()), 1);
  QCOMPARE(earlier.panels().at(0).tabs.size(), 3);
  QCOMPARE(earlier.panels().at(0).currentTab, 1);
  QCOMPARE(earlier.panels().at(0).tabs.at(1).path, QStringLiteral("/2"));
  QCOMPARE(earlier.panels().at(1).tabs.at(0).path, QStringLiteral("/0"));
  QCOMPARE(earlier.panels().at(1).currentTab, 0);

  // Moving the current tab selects its neighbour: the next one when present.
  PanelTree next;
  fill(next, 3);
  next.panels()[0].currentTab = 0;
  QCOMPARE(next.splitPanel(0, PanelTree::Zone::Right, 0, QString()), 1);
  QCOMPARE(next.panels().at(0).currentTab, 0);
  QCOMPARE(next.panels().at(0).tabs.at(0).path, QStringLiteral("/1"));

  // And the previous one when the moved tab was last.
  PanelTree previous;
  fill(previous, 3);
  previous.panels()[0].currentTab = 2;
  QCOMPARE(previous.splitPanel(0, PanelTree::Zone::Right, 2, QString()), 1);
  QCOMPARE(previous.panels().at(0).currentTab, 1);
  QCOMPARE(previous.panels().at(0).tabs.at(1).path, QStringLiteral("/1"));
}

void HermesQtUiTest::panelTreeActivatesTheNewPanelOnSplit()
{
  PanelTree tree;
  QCOMPARE(tree.activePanel(), 0);
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QString()), 1);
  QCOMPARE(tree.activePanel(), 1);
  QCOMPARE(tree.splitPanel(1, PanelTree::Zone::Bottom, -1, QString()), 2);
  QCOMPARE(tree.activePanel(), 2);
}

void HermesQtUiTest::panelTreeClosesTheActivePanelIntoTheSiblingSubtree()
{
  PanelTree tree;
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, -1, QStringLiteral("/right")), 1);
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Bottom, -1, QStringLiteral("/below")), 2);
  QCOMPARE(tree.panelsInReadingOrder(), QVector<int>({0, 2, 1}));
  tree.setActivePanel(0);
  QCOMPARE(tree.activePanel(), 0);

  tree.closePanel(0);
  QCOMPARE(tree.panels().size(), 2);
  QCOMPARE(tree.panels().at(1).tabs.at(0).path, QStringLiteral("/below"));
  // The sibling subtree's first panel becomes index 1 after the shift.
  QCOMPARE(tree.activePanel(), 1);
}

void HermesQtUiTest::panelTreeRefusedSplitLeavesTheTabsUntouched()
{
  PanelTree tree;
  PanelTab only;
  only.path = QStringLiteral("/only");
  only.history = QStringList({QStringLiteral("/older"), QStringLiteral("/only")});
  only.historyIndex = 1;
  only.selectedPaths = QStringList({QStringLiteral("/only/picked")});
  only.currentIndex = 3;
  only.anchorIndex = 2;
  tree.panels()[0].tabs.clear();
  tree.panels()[0].tabs.append(only);
  tree.panels()[0].currentTab = 0;

  const PanelNode *rootBefore = tree.root();
  QCOMPARE(tree.splitPanel(0, PanelTree::Zone::Right, 0, QStringLiteral("/new")), -1);
  QCOMPARE(tree.panels().size(), 1);
  QCOMPARE(tree.root(), rootBefore);
  QCOMPARE(tree.panels().at(0).currentTab, 0);
  QCOMPARE(tree.panels().at(0).tabs.size(), 1);
  const PanelTab &tab = tree.panels().at(0).tabs.at(0);
  QCOMPARE(tab.path, QStringLiteral("/only"));
  QCOMPARE(tab.history, QStringList({QStringLiteral("/older"), QStringLiteral("/only")}));
  QCOMPARE(tab.historyIndex, 1);
  QCOMPARE(tab.selectedPaths, QStringList({QStringLiteral("/only/picked")}));
  QCOMPARE(tab.currentIndex, 3);
  QCOMPARE(tab.anchorIndex, 2);
}

void HermesQtUiTest::controllerSplitsTheActivePanel()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ShellController controller;
  controller.navigate(dir.path());
  QCOMPARE(controller.panels().size(), 1);
  QCOMPARE(controller.activePanel(), 0);

  controller.splitActiveRight();
  QCOMPARE(controller.panels().size(), 2);
  QCOMPARE(controller.activePanel(), 1);
  // The new panel opens the path that was showing when the split was asked for.
  QCOMPARE(panelCurrentPath(controller, 0), dir.path());
  QCOMPARE(panelCurrentPath(controller, 1), dir.path());
  DirectoryModel *model = panelModel(controller, 1);
  QVERIFY(model);
  QTRY_COMPARE_WITH_TIMEOUT(model->path(), dir.path(), 5000);
}

void HermesQtUiTest::controllerOpensAFolderInASplit()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("inside.txt")), "x"));

  ShellController controller;
  controller.openInSplit(dir.path(), true);
  QCOMPARE(controller.panels().size(), 2);
  QCOMPARE(controller.activePanel(), 1);
  QCOMPARE(panelCurrentPath(controller, 1), dir.path());
  DirectoryModel *model = panelModel(controller, 1);
  QVERIFY(model);
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);
  QCOMPARE(entryNames(model), QStringList({QStringLiteral("inside.txt")}));
}

void HermesQtUiTest::controllerMovesATabIntoANewSplit()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());

  ShellController controller;
  controller.navigate(first.path());
  controller.newTab(second.path());
  QCOMPARE(panelTabPaths(controller, 0),
           QStringList({first.path(), second.path()}));

  controller.moveTabToSplit(0, 0, true);
  QCOMPARE(controller.panels().size(), 2);
  QCOMPARE(controller.activePanel(), 1);
  QCOMPARE(panelTabPaths(controller, 0), QStringList({second.path()}));
  QCOMPARE(panelTabPaths(controller, 1), QStringList({first.path()}));
  DirectoryModel *model = panelModel(controller, 1);
  QVERIFY(model);
  QCOMPARE(model->path(), first.path());
}

void HermesQtUiTest::controllerRefusesToMoveTheLastTab()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ShellController controller;
  controller.navigate(dir.path());
  QSignalSpy failures(&controller, &ShellController::operationFailed);
  QVERIFY(failures.isValid());

  controller.moveTabToSplit(0, 0, true);
  QCOMPARE(controller.panels().size(), 1);
  QCOMPARE(panelTabPaths(controller, 0), QStringList({dir.path()}));
  QCOMPARE(failures.count(), 1);
}

void HermesQtUiTest::controllerClosesAPanelWithItsLastTab()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ShellController controller;
  controller.navigate(dir.path());
  controller.splitActiveRight();
  QCOMPARE(controller.panels().size(), 2);
  QCOMPARE(panelTabPaths(controller, 0), QStringList({dir.path()}));

  controller.closeTab(0, 0);
  QCOMPARE(controller.panels().size(), 1);
  QCOMPARE(panelCurrentPath(controller, 0), dir.path());
  DirectoryModel *model = panelModel(controller, 0);
  QVERIFY(model);
  QCOMPARE(model->path(), dir.path());
}

void HermesQtUiTest::controllerRefusesASeventeenthPanel()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ShellController controller;
  controller.navigate(dir.path());
  QSignalSpy failures(&controller, &ShellController::operationFailed);
  QVERIFY(failures.isValid());

  for (int i = 1; i < PanelTree::MaxPanels; ++i)
    controller.splitActiveRight();
  QCOMPARE(controller.panels().size(), PanelTree::MaxPanels);

  failures.clear();
  controller.splitActiveRight();
  QCOMPARE(controller.panels().size(), PanelTree::MaxPanels);
  QCOMPARE(failures.count(), 1);
  QVERIFY(failures.constFirst().at(0).toString().contains(
      QStringLiteral("Too many panels")));
}

void HermesQtUiTest::controllerNavigatesInTheActivePanelOnly()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QTemporaryDir third;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  QVERIFY(third.isValid());

  ShellController controller;
  controller.navigate(first.path());
  controller.openInSplit(second.path(), true);  // the new panel becomes active
  QCOMPARE(panelCurrentPath(controller, 0), first.path());
  QCOMPARE(panelCurrentPath(controller, 1), second.path());

  controller.navigate(third.path());
  QCOMPARE(panelCurrentPath(controller, 0), first.path());
  QCOMPARE(panelCurrentPath(controller, 1), third.path());
  DirectoryModel *firstModel = panelModel(controller, 0);
  DirectoryModel *secondModel = panelModel(controller, 1);
  QVERIFY(firstModel && secondModel);
  QVERIFY(firstModel != secondModel);
  QCOMPARE(firstModel->path(), first.path());
  QCOMPARE(secondModel->path(), third.path());
}

void HermesQtUiTest::controllerKeepsTwoPanelsOfTheSameDirectoryIndependent()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  QVERIFY(writeFile(first.filePath(QStringLiteral("shared.txt")), "x"));

  ShellController controller;
  controller.navigate(first.path());
  controller.splitActiveRight();  // both panels show the same directory
  QCOMPARE(panelCurrentPath(controller, 0), first.path());
  QCOMPARE(panelCurrentPath(controller, 1), first.path());
  DirectoryModel *firstModel = panelModel(controller, 0);
  DirectoryModel *secondModel = panelModel(controller, 1);
  QVERIFY(firstModel && secondModel);
  QVERIFY(firstModel != secondModel);
  QTRY_COMPARE_WITH_TIMEOUT(firstModel->rowCount(), 1, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(secondModel->rowCount(), 1, 5000);

  controller.navigate(second.path());  // only the active panel moves
  QCOMPARE(panelCurrentPath(controller, 0), first.path());
  QCOMPARE(panelCurrentPath(controller, 1), second.path());
  QCOMPARE(firstModel->path(), first.path());
  QCOMPARE(secondModel->path(), second.path());
  QCOMPARE(entryNames(firstModel), QStringList({QStringLiteral("shared.txt")}));
}

void HermesQtUiTest::controllerDropsTheOldScanWhenATabSwitches()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  QVERIFY(writeFile(first.filePath(QStringLiteral("only-in-first.txt")), "x"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("a.txt")), "a"));
  QVERIFY(writeFile(second.filePath(QStringLiteral("b.txt")), "b"));

  ShellController controller;
  controller.navigate(first.path());
  DirectoryModel *model = panelModel(controller, 0);
  QVERIFY(model);
  const quint64 staleGeneration = model->m_generation;

  // The second tab takes over this panel's model while the first scan is live.
  controller.newTabInPanel(0);
  controller.navigate(second.path());
  QCOMPARE(model->path(), second.path());
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 5000);

  // A late result from the tab that is no longer showing must be rejected.
  HermesListing listing{};
  listing.items = static_cast<HermesEntry *>(calloc(1, sizeof(HermesEntry)));
  QVERIFY(listing.items);
  listing.count = 1;
  const QByteArray name("only-in-first.txt");
  listing.items[0].name = strdup(name.constData());
  QVERIFY(listing.items[0].name);
  listing.items[0].name_len = static_cast<size_t>(name.size());
  QVERIFY(!model->applySnapshot(testSnapshot(model, first.path(),
                                             staleGeneration, listing)));
  hermes_listing_free(&listing);
  QCOMPARE(model->path(), second.path());
  QCOMPARE(entryNames(model), QStringList({QStringLiteral("a.txt"),
                                           QStringLiteral("b.txt")}));

  // Switching back re-points the very same model at the first tab's path.
  controller.selectTab(0, 0);
  QCOMPARE(model->path(), first.path());
}

void HermesQtUiTest::controllerKeepsTheRowsWhenAPanelDirectoryDisappears()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("kept.txt")), "x"));

  ShellController controller;
  controller.navigate(dir.path());
  DirectoryModel *model = panelModel(controller, 0);
  QVERIFY(model);
  QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);

  QVERIFY(dir.remove());
  QTest::qWait(600);  // the panel's watcher debounces, then its refresh settles
  QCOMPARE(model->rowCount(), 1);
  QCOMPARE(entryNames(model), QStringList({QStringLiteral("kept.txt")}));
  QCOMPARE(model->path(), dir.path());
}

void HermesQtUiTest::controllerCyclesThePanels()
{
  ShellController controller;
  controller.splitActiveRight();  // panels 0 and 1, panel 1 active
  controller.splitActiveDown();   // splits panel 1 into 1 and 2
  QCOMPARE(controller.panels().size(), 3);
  QCOMPARE(controller.activePanel(), 2);

  // Reading order is 0, 1, 2, so the next panel after the last one wraps.
  controller.cyclePanel(1);
  QCOMPARE(controller.activePanel(), 0);
  controller.cyclePanel(1);
  QCOMPARE(controller.activePanel(), 1);
  controller.cyclePanel(-1);
  QCOMPARE(controller.activePanel(), 0);
  controller.cyclePanel(-1);
  QCOMPARE(controller.activePanel(), 2);
}

void HermesQtUiTest::controllerKeepsAnInactivePanelInStepWhenItsCurrentTabCloses()
{
  QTemporaryDir first;
  QTemporaryDir second;
  QTemporaryDir third;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());
  QVERIFY(third.isValid());

  ShellController controller;
  controller.navigate(first.path());
  controller.openInSplit(second.path(), true);  // panel 1 becomes active
  controller.newTab(third.path());
  controller.selectTab(1, 0);  // panel 1 shows second, with third behind it
  controller.activatePanel(0);

  DirectoryModel *model = panelModel(controller, 1);
  QVERIFY(model);
  QCOMPARE(model->path(), second.path());

  // Closing a tab in a panel that is not showing must still re-point that
  // panel's model; activating it later cannot repair stale rows by itself.
  controller.closeTab(1, 0);
  QCOMPARE(panelCurrentPath(controller, 1), third.path());
  QCOMPARE(model->path(), third.path());

  controller.activatePanel(1);
  QCOMPARE(controller.directory(), model);
  QCOMPARE(controller.directory()->path(), third.path());
}

QTEST_MAIN(HermesQtUiTest)
#include "qtui-selftest.moc"
