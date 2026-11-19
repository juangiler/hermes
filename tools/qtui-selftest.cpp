#include "directory_model.h"
#include "places_model.h"
#include "shell_controller.h"

#include "core_api.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrl>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <functional>
#include <memory>
#include <thread>
#include <QThread>
#include <fcntl.h>
#include <unistd.h>

static DirectoryModel *g_wake_model;

static void pollTestModel(void *)
{
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
  void directoryModelSortKeyUsesLegacyDirections();
  void directoryViewExposesModesColumnsAndZoom();
  void directoryPageViewModesFormatSizeAndDate();
  void directoryPageViewsShowEntriesAndScaleWithZoom();
  void directoryModelFailedNavigateKeepsPath();
  void directoryModelNavigationDoesNotBlock();
  void directoryModelAppliesAsyncResults();
  void directoryModelKeepsRowsWhenTheDirectoryDisappears();
  void directoryModelRefreshDoesNotDuplicateRows();
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

private:
  using CorePtr = std::unique_ptr<HermesCore, decltype(&hermes_core_destroy)>;
  static CorePtr makeCore();
  static bool writeFile(const QString &path, const QByteArray &data);
  static QStringList entryNames(DirectoryModel *model);
  static QByteArray fileContents(const QString &path);
  static void waitForOperation(ShellController *controller);

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
  model.setShowHidden(false);
  QVERIFY(!model.showHidden());
  QVERIFY(model.navigate(dir.path()));
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
  QCOMPARE(entryNames(&model), QStringList({QStringLiteral("visible.txt")}));

  model.setShowHidden(true);
  QVERIFY(model.showHidden());
  QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
  QVERIFY(entryNames(&model).contains(QStringLiteral(".hidden.txt")));
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
  QCOMPARE(controller.tabs(), paths);

  controller.selectTab(2);
  QCOMPARE(controller.currentTab(), 2);
  QCOMPARE(controller.directory()->path(), paths.at(2));

  controller.closeTab(0);
  QCOMPARE(controller.tabs(), QStringList({paths.at(1), paths.at(2), paths.at(3)}));
  QCOMPARE(controller.currentTab(), 1);
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
  QTRY_COMPARE(controller.tabs().size(), 1);
  QVERIFY(controller.currentTab() >= 0);

  const QString child = dir.filePath(QStringLiteral("child"));
  QVERIFY(QDir().mkpath(child));
  controller.navigate(child);
  QCOMPARE(controller.directory()->path(), child);
  QVERIFY(controller.currentTab() >= 0 && controller.currentTab() < controller.tabs().size());
  QCOMPARE(controller.tabs().at(controller.currentTab()), child);
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
  QCOMPARE(controller.tabs(), QStringList({inserted, first}));
  QCOMPARE(controller.currentTab(), 1);
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
  QVERIFY(model->applyScan(second.path(), model->m_generation, listing));
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

  // Only the generation/path guard may reject this successfully fetched result.
  HermesListing listing{};
  const QByteArray currentPath = current.path().toUtf8();
  QCOMPARE(hermes_core_scan_sorted(core.get(), currentPath.constData(),
                                   HERMES_SORT_NAME, false, &listing), 0);
  QVERIFY(!model.applyScan(current.path(), model.m_generation - 1, listing));
  QCOMPARE(model.path(), current.path());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), firstName);

  QVERIFY(!model.applyScan(other.path(), model.m_generation, listing));
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
  QSignalSpy changed(&controller, &ShellController::tabsChanged);

  controller.navigate(second.path());
  QCOMPARE(changed.count(), 1);
  QCOMPARE(controller.tabs().constFirst(), second.path());
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

QTEST_MAIN(HermesQtUiTest)
#include "qtui-selftest.moc"
