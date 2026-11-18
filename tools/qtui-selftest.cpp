#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QtTest>

class HermesQtUiTest : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase();
  void qmlMainLoadsOffscreen();

private:
  QTemporaryDir m_config_dir;
};

void HermesQtUiTest::initTestCase()
{
  QVERIFY(m_config_dir.isValid());
  qputenv("XDG_CONFIG_HOME", m_config_dir.path().toUtf8());
}

void HermesQtUiTest::qmlMainLoadsOffscreen()
{
  QQmlApplicationEngine engine;
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  QVERIFY(!engine.rootObjects().isEmpty());
}

QTEST_MAIN(HermesQtUiTest)
#include "qtui-selftest.moc"
