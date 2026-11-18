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
#include <QtTest>
#include <memory>

static void noWake(void *) {}

class HermesQtUiTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void qmlMainLoadsOffscreen();
  void directoryModelListsSortedEntries();
  void directoryModelReportsRoles();
  void directoryModelPreservesUtf8Names();
  void directoryModelFailedNavigateKeepsPath();
  void hostOpenUrisCreatesTabs();
  void navigationHistoryBackForward();
  void upAndFailedPathKeepState();
  void placesIncludeHomeAndRoot();
  void navigationIgnoresStaleDirectoryResults();
  void closeTabBeforeCurrentKeepsFocusedTab();
  void focusFreeHostTabKeepsValidCursor();
  void hostOpenTabAtIndexInsertsTab();
  void directoryModelRejectsStaleScanResults();

private:
  using CorePtr = std::unique_ptr<HermesCore, decltype(&hermes_core_destroy)>;
  static CorePtr makeCore();
  static bool writeFile(const QString &path, const QByteArray &data);

  QTemporaryDir m_config_dir;
};

HermesQtUiTest::CorePtr HermesQtUiTest::makeCore()
{
  return CorePtr(hermes_core_create(noWake, nullptr), &hermes_core_destroy);
}

bool HermesQtUiTest::writeFile(const QString &path, const QByteArray &data)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
    return false;
  file.close();
  return true;
}

void HermesQtUiTest::initTestCase()
{
  QVERIFY(m_config_dir.isValid());
  qputenv("XDG_CONFIG_HOME", m_config_dir.path().toUtf8());
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
  QVERIFY(model.navigate(dir.path()));
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), DirectoryModel::IsDirRole).toBool());
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), QStringLiteral("zdir"));
  QCOMPARE(model.data(model.index(1), DirectoryModel::NameRole).toString(), QStringLiteral("a.txt"));
  QCOMPARE(model.data(model.index(2), DirectoryModel::NameRole).toString(), QStringLiteral("b.txt"));

  model.setSort(HERMES_SORT_NAME, true);
  model.refresh();
  QCOMPARE(model.rowCount(), 3);
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
  QVERIFY(model.navigate(dir.path()));
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
  QVERIFY(model.navigate(dir.path()));
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), name);
  QCOMPARE(model.data(model.index(0), DirectoryModel::PathRole).toString(), dir.filePath(name));
}

void HermesQtUiTest::directoryModelFailedNavigateKeepsPath()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.txt")), "x"));

  CorePtr core = makeCore();
  DirectoryModel model(core.get());
  QVERIFY(model.navigate(dir.path()));
  const int rows = model.rowCount();
  const QString name = model.data(model.index(0), DirectoryModel::NameRole).toString();
  const QString missing = dir.filePath(QStringLiteral("missing"));

  QSignalSpy failed(&model, &DirectoryModel::navigateFailed);
  QSignalSpy changed(&model, &DirectoryModel::pathChanged);
  QVERIFY(!model.navigate(missing));
  QCOMPARE(failed.count(), 1);
  QCOMPARE(failed.takeFirst().at(0).toString(), missing);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(model.path(), dir.path());
  QCOMPARE(model.rowCount(), rows);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), name);
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
  QCOMPARE(controller.directory()->rowCount(), 1);

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
  QVERIFY(model.navigate(current.path()));
  QCOMPARE(model.rowCount(), 2);
  const QString firstName = model.data(model.index(0), DirectoryModel::NameRole).toString();

  // Both calls scan successfully; only the generation/path check can reject them.
  QVERIFY(model.applyScan(current.path(), model.m_generation - 1));
  QCOMPARE(model.path(), current.path());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), firstName);

  QVERIFY(model.applyScan(other.path(), model.m_generation));
  QCOMPARE(model.path(), current.path());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(), firstName);
}

QTEST_MAIN(HermesQtUiTest)
#include "qtui-selftest.moc"
