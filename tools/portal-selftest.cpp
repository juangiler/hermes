// Headless portal suite: stub portal objects on a private session bus.
// Later tasks append the service and configuration cases here.
#include "../portals/documents_client.h"
#include "../portals/open_uri_client.h"
#include "../portals/portal_common.h"
#include "../portals/settings_client.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusVariant>
#include <QtTest/QtTest>

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kDesktopPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kRequestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/hermes_selftest/1");
const QString kAccentNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kAccentKey = QStringLiteral("accent-color");
const QString kDocumentId = QStringLiteral("selftest-document-id");
const QString kFilePath = QStringLiteral("/tmp/hermes-portal-open-uri.txt");
const QString kParentWindow = QStringLiteral("hermes_selftest");
const uint kAccentValue = 1;
}

// The real portal wraps ReadOne's value in an a{sv} with a "v" entry.
class StubSettingsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")

public:
    explicit StubSettingsAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent) {}

public slots:
    QDBusVariant ReadOne(const QString &ns, const QString &key)
    {
        lastNamespace = ns;
        lastKey = key;
        return QDBusVariant(QVariant::fromValue(
            QVariantMap{{QStringLiteral("v"), QVariant(kAccentValue)}}));
    }

public:
    QString lastNamespace;
    QString lastKey;
};

class StubRequestAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")

public:
    explicit StubRequestAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent) {}

    void sendResponse()
    {
        emit Response(0, QVariantMap());
    }

signals:
    void Response(uint response, const QVariantMap &results);
};

class StubOpenUriAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.OpenURI")

public:
    StubOpenUriAdaptor(QObject *parent, StubRequestAdaptor *request)
        : QDBusAbstractAdaptor(parent), m_request(request) {}

public slots:
    QDBusObjectPath OpenURI(const QString &parentWindow, const QString &uri,
                            const QVariantMap &options)
    {
        lastParentWindow = parentWindow;
        lastUri = uri;
        lastOptions = options;
        // Give the client time to subscribe to the returned request path.
        QTimer::singleShot(20, m_request, [this]() { m_request->sendResponse(); });
        return QDBusObjectPath(kRequestPath);
    }

public:
    QString lastParentWindow;
    QString lastUri;
    QVariantMap lastOptions;

private:
    StubRequestAdaptor *m_request;
};

class StubDocumentsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Documents")

public:
    explicit StubDocumentsAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent) {}

public slots:
    QString Add(const QString &path, const QVariantMap &options)
    {
        lastPath = path;
        lastOptions = options;
        return kDocumentId;
    }

public:
    QString lastPath;
    QVariantMap lastOptions;
};

// Owns the stub objects only for the duration of one test slot.
class PortalStub : public QObject
{
    Q_OBJECT

private:
    QObject *m_requestObject;
    StubRequestAdaptor *m_requestAdaptor;

public:
    explicit PortalStub(QObject *parent = nullptr)
        : QObject(parent),
          m_requestObject(new QObject),
          m_requestAdaptor(new StubRequestAdaptor(m_requestObject)),
          settings(new StubSettingsAdaptor(this)),
          openUri(new StubOpenUriAdaptor(this, m_requestAdaptor)),
          documents(new StubDocumentsAdaptor(this)) {}

    ~PortalStub() override
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kRequestPath);
        bus.unregisterObject(kDesktopPath);
        bus.unregisterService(kService);
        delete m_requestObject;
    }

    bool start()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        return bus.registerService(kService)
            && bus.registerObject(kDesktopPath, this)
            && bus.registerObject(kRequestPath, m_requestObject);
    }

    StubSettingsAdaptor *settings = nullptr;
    StubOpenUriAdaptor *openUri = nullptr;
    StubDocumentsAdaptor *documents = nullptr;
};

// Holding the name without any object stops dbus-daemon from activating the
// host's real portal, so the clients see an absent portal object.
class EmptyNameOwner
{
public:
    EmptyNameOwner()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        m_acquired = bus.registerService(kService);
    }

    ~EmptyNameOwner()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterService(kService);
    }

    bool acquired() const { return m_acquired; }

private:
    bool m_acquired = false;
};

class PortalSelfTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void privateBusIsAvailable();
    void settingsReadEmitsStubValue();
    void openUriReceivesSuccessResponse();
    void documentsAddReturnsDocumentId();
    void clientsReportUnavailableWithoutService();
};

void PortalSelfTest::initTestCase()
{
    qRegisterMetaType<PortalReply>("PortalReply");
}

void PortalSelfTest::privateBusIsAvailable()
{
    QVERIFY(QDBusConnection::sessionBus().isConnected());
}

void PortalSelfTest::settingsReadEmitsStubValue()
{
    PortalStub stub;
    QVERIFY(stub.start());

    SettingsClient client;
    QSignalSpy readSpy(&client, &SettingsClient::settingRead);
    QSignalSpy failedSpy(&client, &SettingsClient::failed);
    QVERIFY(readSpy.isValid());
    QVERIFY(failedSpy.isValid());

    client.readOne(kAccentNamespace, kAccentKey);

    QTRY_COMPARE_WITH_TIMEOUT(readSpy.count(), 1, 2000);
    QCOMPARE(readSpy.at(0).at(0).toString(), kAccentNamespace);
    QCOMPARE(readSpy.at(0).at(1).toString(), kAccentKey);
    QCOMPARE(readSpy.at(0).at(2).toUInt(), kAccentValue);
    QCOMPARE(stub.settings->lastNamespace, kAccentNamespace);
    QCOMPARE(stub.settings->lastKey, kAccentKey);
    QCOMPARE(failedSpy.count(), 0);
}

void PortalSelfTest::openUriReceivesSuccessResponse()
{
    PortalStub stub;
    QVERIFY(stub.start());

    OpenUriClient client;
    QSignalSpy finishedSpy(&client, &OpenUriClient::finished);
    QVERIFY(finishedSpy.isValid());

    client.openFile(kFilePath, kParentWindow);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    const PortalReply reply = finishedSpy.at(0).at(0).value<PortalReply>();
    QCOMPARE(reply.status, PortalReply::Status::Ok);
    QCOMPARE(stub.openUri->lastParentWindow, kParentWindow);
    QCOMPARE(stub.openUri->lastUri, QUrl::fromLocalFile(kFilePath).toString());
}

void PortalSelfTest::documentsAddReturnsDocumentId()
{
    PortalStub stub;
    QVERIFY(stub.start());

    DocumentsClient client;
    QSignalSpy addedSpy(&client, &DocumentsClient::added);
    QVERIFY(addedSpy.isValid());

    client.add(kFilePath, true);

    QTRY_COMPARE_WITH_TIMEOUT(addedSpy.count(), 1, 2000);
    QCOMPARE(addedSpy.at(0).at(0).toString(), kDocumentId);
    const PortalReply reply = addedSpy.at(0).at(1).value<PortalReply>();
    QCOMPARE(reply.status, PortalReply::Status::Ok);
    QCOMPARE(stub.documents->lastPath, kFilePath);
    QCOMPARE(stub.documents->lastOptions.value(QStringLiteral("writable")).toBool(), true);
}

void PortalSelfTest::clientsReportUnavailableWithoutService()
{
    EmptyNameOwner owner;
    QVERIFY(owner.acquired());

    SettingsClient settings;
    QSignalSpy settingsSpy(&settings, &SettingsClient::failed);
    settings.readOne(kAccentNamespace, kAccentKey);
    QTRY_COMPARE_WITH_TIMEOUT(settingsSpy.count(), 1, 2000);
    QCOMPARE(settingsSpy.at(0).at(0).value<PortalReply>().status,
             PortalReply::Status::Unavailable);

    OpenUriClient openUri;
    QSignalSpy openUriSpy(&openUri, &OpenUriClient::finished);
    openUri.openFile(kFilePath, kParentWindow);
    QTRY_COMPARE_WITH_TIMEOUT(openUriSpy.count(), 1, 2000);
    QCOMPARE(openUriSpy.at(0).at(0).value<PortalReply>().status,
             PortalReply::Status::Unavailable);

    DocumentsClient documents;
    QSignalSpy documentsSpy(&documents, &DocumentsClient::added);
    documents.add(kFilePath, false);
    QTRY_COMPARE_WITH_TIMEOUT(documentsSpy.count(), 1, 2000);
    QCOMPARE(documentsSpy.at(0).at(1).value<PortalReply>().status,
             PortalReply::Status::Unavailable);
}

QTEST_MAIN(PortalSelfTest)
#include "portal-selftest.moc"
