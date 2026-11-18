// Headless portal suite. Later tasks append the client, service and config
// cases here; the harness runs this binary under dbus-run-session.
#include <QtDBus/QDBusConnection>
#include <QtTest/QtTest>

class PortalSelfTest : public QObject
{
    Q_OBJECT

private slots:
    void privateBusIsAvailable();
};

void PortalSelfTest::privateBusIsAvailable()
{
    QVERIFY(QDBusConnection::sessionBus().isConnected());
}

QTEST_MAIN(PortalSelfTest)
#include "portal-selftest.moc"
