// Headless portal suite: stub portal objects on a private session bus.
// Later tasks append the service and configuration cases here.
#include "../portals/documents_client.h"
#include "../portals/open_uri_client.h"
#include "../portals/portal_common.h"
#include "../portals/settings_client.h"

#include <QtCore/QList>
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
const QString kRequestPathBase =
    QStringLiteral("/org/freedesktop/portal/desktop/request/hermes_selftest/");
const QString kAccentNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kAccentKey = QStringLiteral("accent-color");
const QString kColorSchemeKey = QStringLiteral("color-scheme");
const QString kOtherNamespace = QStringLiteral("org.example.portal-test");
const QString kDocumentId = QStringLiteral("selftest-document-id");
const QString kFilePath = QStringLiteral("/tmp/hermes-portal-open-uri.txt");
const QString kFirstPath = QStringLiteral("/tmp/hermes-portal-first.txt");
const QString kSecondPath = QStringLiteral("/tmp/hermes-portal-second.txt");
const QString kParentWindow = QStringLiteral("hermes_selftest");
const QString kUriResult = QStringLiteral("uri");
const uint kAccentValue = 1;
const uint kColorSchemeValue = 2;
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
        const uint value = key == kColorSchemeKey ? kColorSchemeValue : kAccentValue;
        return QDBusVariant(QVariant::fromValue(
            QVariantMap{{QStringLiteral("v"), QVariant(value)}}));
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

    void sendResponse(const QVariantMap &results)
    {
        emit Response(0, results);
    }

signals:
    void Response(uint response, const QVariantMap &results);
};

class PortalStub;

struct OpenCall
{
    QString parentWindow;
    QString uri;
    QVariantMap options;
};

class StubOpenUriAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.OpenURI")

public:
    StubOpenUriAdaptor(QObject *parent, PortalStub *owner)
        : QDBusAbstractAdaptor(parent), m_owner(owner) {}

public slots:
    QDBusObjectPath OpenURI(const QString &parentWindow, const QString &uri,
                            const QVariantMap &options);

public:
    QString lastParentWindow;
    QString lastUri;
    QVariantMap lastOptions;
    QList<OpenCall> calls;

private:
    PortalStub *m_owner;
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
    QList<QObject *> m_requestObjects;
    QList<QString> m_requestPaths;
    int m_requestCount = 0;

public:
    explicit PortalStub(QObject *parent = nullptr)
        : QObject(parent),
          settings(new StubSettingsAdaptor(this)),
          openUri(new StubOpenUriAdaptor(this, this)),
          documents(new StubDocumentsAdaptor(this)) {}

    ~PortalStub() override
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        for (const QString &path : m_requestPaths)
            bus.unregisterObject(path);
        bus.unregisterObject(kDesktopPath);
        bus.unregisterService(kService);
        for (QObject *object : m_requestObjects)
            delete object;
    }

    bool start()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        return bus.registerService(kService)
            && bus.registerObject(kDesktopPath, this);
    }

    // A real portal returns a distinct request path per call.
    QString addRequest(const QString &uri)
    {
        const QString path = kRequestPathBase + QString::number(++m_requestCount);
        QObject *object = new QObject;
        auto *adaptor = new StubRequestAdaptor(object);
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.registerObject(path, object);
        m_requestObjects.append(object);
        m_requestPaths.append(path);

        QVariantMap results;
        results.insert(kUriResult, uri);
        // Give the client time to subscribe to the returned request path.
        QTimer::singleShot(20, adaptor, [adaptor, results]() {
            adaptor->sendResponse(results);
        });
        return path;
    }

    StubSettingsAdaptor *settings = nullptr;
    StubOpenUriAdaptor *openUri = nullptr;
    StubDocumentsAdaptor *documents = nullptr;
};

QDBusObjectPath StubOpenUriAdaptor::OpenURI(const QString &parentWindow, const QString &uri,
                                            const QVariantMap &options)
{
    lastParentWindow = parentWindow;
    lastUri = uri;
    lastOptions = options;
    calls.append(OpenCall{parentWindow, uri, options});
    return QDBusObjectPath(m_owner->addRequest(uri));
}

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
    void settingsReadsOverlappingRequests();
    void openUriReceivesSuccessResponse();
    void openUriDeliversOverlappingResponses();
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

void PortalSelfTest::settingsReadsOverlappingRequests()
{
    PortalStub stub;
    QVERIFY(stub.start());

    SettingsClient client;
    QSignalSpy readSpy(&client, &SettingsClient::settingRead);
    QSignalSpy failedSpy(&client, &SettingsClient::failed);
    QVERIFY(readSpy.isValid());
    QVERIFY(failedSpy.isValid());

    client.readOne(kAccentNamespace, kAccentKey);
    client.readOne(kOtherNamespace, kColorSchemeKey);

    QTRY_COMPARE_WITH_TIMEOUT(readSpy.count(), 2, 2000);
    QVariantMap seen;
    for (int i = 0; i < readSpy.count(); ++i) {
        seen.insert(readSpy.at(i).at(0).toString() + QStringLiteral("/")
                        + readSpy.at(i).at(1).toString(),
                    readSpy.at(i).at(2));
    }
    QCOMPARE(seen.value(QStringLiteral("org.freedesktop.appearance/accent-color")).toUInt(),
             kAccentValue);
    QCOMPARE(seen.value(QStringLiteral("org.example.portal-test/color-scheme")).toUInt(),
             kColorSchemeValue);
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

void PortalSelfTest::openUriDeliversOverlappingResponses()
{
    PortalStub stub;
    QVERIFY(stub.start());

    OpenUriClient client;
    QSignalSpy finishedSpy(&client, &OpenUriClient::finished);
    QVERIFY(finishedSpy.isValid());

    client.openFile(kFirstPath, kParentWindow);
    client.openFile(kSecondPath, kParentWindow);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 2000);
    QCOMPARE(stub.openUri->calls.size(), 2);
    QList<QString> uris;
    for (int i = 0; i < finishedSpy.count(); ++i) {
        const PortalReply reply = finishedSpy.at(i).at(0).value<PortalReply>();
        QCOMPARE(reply.status, PortalReply::Status::Ok);
        uris.append(reply.results.value(kUriResult).toString());
    }
    QVERIFY(uris.contains(QUrl::fromLocalFile(kFirstPath).toString()));
    QVERIFY(uris.contains(QUrl::fromLocalFile(kSecondPath).toString()));
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
