// Headless portal suite: stub portal objects on a private session bus.
// Later tasks append the service and configuration cases here.
#include "../portals/documents_client.h"
#include "../portals/file_chooser_backend.h"
#include "../portals/file_manager1_service.h"
#include "../portals/open_uri_client.h"
#include "../portals/portal_common.h"
#include "../portals/portal_config.h"
#include "../portals/settings_client.h"

#include "../src/core_api.h"
#include "../src/core_host.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
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

const QString kFileManagerService = QStringLiteral("org.freedesktop.FileManager1");
const QString kFileManagerPath = QStringLiteral("/org/freedesktop/FileManager1");
const QString kFileManagerInterface = QStringLiteral("org.freedesktop.FileManager1");
const QString kImplService = QStringLiteral("org.freedesktop.impl.portal.desktop.hermes");
const QString kChooserInterface = QStringLiteral("org.freedesktop.impl.portal.FileChooser");
const QString kCancelledError = QStringLiteral("org.freedesktop.portal.Error.Cancelled");
const QString kOpenHandle = QStringLiteral(
    "/org/freedesktop/portal/desktop/request/hermes_selftest/open");
const QString kSaveHandle = QStringLiteral(
    "/org/freedesktop/portal/desktop/request/hermes_selftest/save");
const QString kAppId = QStringLiteral("org.kde.hermes.selftest");
const QString kChooserTitle = QStringLiteral("Select a file");
const uint kCancelledResponse = 1;

const QByteArray kConfigDirName("xdg-desktop-portal");
const QByteArray kConfigFileName("portals.conf");
const QByteArray kBackupSuffix("hermes-backup");
const QString kEnabledStatus = QStringLiteral("filechooser: enabled");
const QString kDisabledStatus = QStringLiteral("filechooser: disabled");
const QByteArray kEnabledConfig(
    "[preferred]\norg.freedesktop.impl.portal.FileChooser=hermes\n");

QStringList g_openedPaths;
QList<int> g_openedIndexes;
QList<bool> g_openedFocus;

void recordOpenTab(const char *path, int index, bool focus)
{
    g_openedPaths.append(QString::fromUtf8(path));
    g_openedIndexes.append(index);
    g_openedFocus.append(focus);
}

QVariantMap chooserOptions()
{
    const QVariantList filters{QVariantMap{{QStringLiteral("name"), QStringLiteral("Text")},
                                           {QStringLiteral("patterns"),
                                            QStringList{QStringLiteral("*.txt")}}}};
    QVariantMap options;
    options.insert(QStringLiteral("filters"), filters);
    options.insert(QStringLiteral("multiple"), true);
    options.insert(QStringLiteral("current_folder"), QByteArray("/tmp"));
    return options;
}

bool waitForCall(QDBusPendingCallWatcher *watcher)
{
    QSignalSpy finished(watcher, &QDBusPendingCallWatcher::finished);
    return finished.wait(2000) && watcher->isFinished();
}

// Hold a well-known name on the private bus so the real backend's activation
// hits the same conflict a Plasma file manager would cause.
class FileManagerNameOwner
{
public:
    FileManagerNameOwner()
    {
        m_acquired = QDBusConnection::sessionBus().registerService(kFileManagerService);
    }

    ~FileManagerNameOwner()
    {
        QDBusConnection::sessionBus().unregisterService(kFileManagerService);
    }

    bool acquired() const { return m_acquired; }

private:
    bool m_acquired = false;
};

// Assertions may return early; always stop a spawned backend before Qt kills it.
class BoundedProcess
{
public:
    ~BoundedProcess()
    {
        if (process.state() == QProcess::Running) {
            process.terminate();
            if (!process.waitForFinished(2000)) {
                process.kill();
                process.waitForFinished(2000);
            }
        }
    }

    QProcess process;
};
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

// The engine's host table is global; install the recorder only for the slot
// that exercises ShowItems and always put the defaults back afterwards.
class RecordingHostGuard
{
public:
    RecordingHostGuard()
    {
        g_openedPaths.clear();
        g_openedIndexes.clear();
        g_openedFocus.clear();
        m_host = hermes_core_host_defaults;
        m_host.open_tab = recordOpenTab;
        hermes_core_set_host(&m_host);
    }

    ~RecordingHostGuard() { hermes_core_set_host(nullptr); }

private:
    HermesCoreHost m_host;
};

// Owns the in-process services for one slot. The QDBusInterface in each test is
// the caller; nothing external is launched on the private bus.
class PortalServiceStub : public QObject
{
    Q_OBJECT

public:
    explicit PortalServiceStub(QObject *parent = nullptr)
        : QObject(parent),
          fileManager(new FileManager1Service(this)),
          chooser(new FileChooserBackend(this)) {}

    ~PortalServiceStub() override
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kFileManagerPath);
        bus.unregisterObject(kDesktopPath);
        bus.unregisterService(kFileManagerService);
        bus.unregisterService(kImplService);
    }

    bool start(QString *error)
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        return bus.isConnected() && fileManager->registerService(bus, error)
            && chooser->registerService(bus, error);
    }

    FileManager1Service *fileManager = nullptr;
    FileChooserBackend *chooser = nullptr;
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
    void fileManagerShowItemsOpensUrisAndCancels();
    void fileChooserOpenFileReturnsCancelled();
    void fileChooserSaveFileReturnsCancelled();
    void backendKeepsFileChooserWhenFileManager1IsTaken();
    void portalConfigUsesForcedXdgConfigHome();
    void portalConfigEnableCreatesKeyAndBackup();
    void portalConfigEnablePreservesUnrelatedLines();
    void portalConfigEnableReplacesOnlyExistingValue();
    void portalConfigEnableAppendsSectionWithExistingLineEnding();
    void portalConfigEnableIsIdempotent();
    void portalConfigDisableRestoresExactOriginalBytes();
    void portalConfigDisableRemovesConfigWhenOriginalMissing();
    void portalConfigDisableWithoutBackupRemovesOnlyHermesKey();
    void portalConfigDisableWithoutBackupKeepsForeignFileChooserValue();
    void portalConfigStatusTracksEnableAndDisable();
    void portalConfigNeverWritesUnderRealHome();

private:
    QString configPath() const;
    QString backupPath() const;
    void resetPortalConfig();
    void writeConfig(const QByteArray &bytes);
    QByteArray readFile(const QString &path) const;

    QTemporaryDir m_configDir;
    QString m_forcedConfigHome;
    QString m_realHomeConfig;
    QString m_realHomeBackup;
    QByteArray m_realHomeBytes;
    QByteArray m_realHomeBackupBytes;
    bool m_realHomeTracked = false;
    bool m_realHomeExisted = false;
    bool m_realHomeBackupExisted = false;
};

void PortalSelfTest::initTestCase()
{
    qRegisterMetaType<PortalReply>("PortalReply");

    // Record the real home state before forcing XDG_CONFIG_HOME, so the final
    // case can prove no opt-in write escaped the temporary directory.
    const QString home = QString::fromLocal8Bit(qgetenv("HOME"));
    if (!home.isEmpty()) {
        m_realHomeConfig = home + QStringLiteral("/.config/")
            + QString::fromLocal8Bit(kConfigDirName.constData()) + QLatin1Char('/')
            + QString::fromLocal8Bit(kConfigFileName.constData());
        m_realHomeBackup = m_realHomeConfig + QLatin1Char('.')
            + QString::fromLocal8Bit(kBackupSuffix.constData());
        m_realHomeTracked = true;
        m_realHomeExisted = QFile::exists(m_realHomeConfig);
        m_realHomeBackupExisted = QFile::exists(m_realHomeBackup);
        if (m_realHomeExisted)
            m_realHomeBytes = readFile(m_realHomeConfig);
        if (m_realHomeBackupExisted)
            m_realHomeBackupBytes = readFile(m_realHomeBackup);
    }

    QVERIFY(m_configDir.isValid());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toLocal8Bit());
    m_forcedConfigHome = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
    QCOMPARE(m_forcedConfigHome, m_configDir.path());
}

QString PortalSelfTest::configPath() const
{
    return m_forcedConfigHome + QLatin1Char('/')
        + QString::fromLocal8Bit(kConfigDirName.constData()) + QLatin1Char('/')
        + QString::fromLocal8Bit(kConfigFileName.constData());
}

QString PortalSelfTest::backupPath() const
{
    return configPath() + QLatin1Char('.')
        + QString::fromLocal8Bit(kBackupSuffix.constData());
}

void PortalSelfTest::resetPortalConfig()
{
    const QString directory = m_forcedConfigHome + QLatin1Char('/')
        + QString::fromLocal8Bit(kConfigDirName.constData());
    QDir dir(directory);
    if (dir.exists())
        QVERIFY(dir.removeRecursively());
}

void PortalSelfTest::writeConfig(const QByteArray &bytes)
{
    const QString directory = m_forcedConfigHome + QLatin1Char('/')
        + QString::fromLocal8Bit(kConfigDirName.constData());
    QVERIFY(QDir().mkpath(directory));
    QFile file(configPath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(static_cast<qint64>(file.write(bytes)), static_cast<qint64>(bytes.size()));
}

QByteArray PortalSelfTest::readFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
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

void PortalSelfTest::fileManagerShowItemsOpensUrisAndCancels()
{
    RecordingHostGuard host;
    PortalServiceStub stub;
    QString error;
    QVERIFY2(stub.start(&error), qPrintable(error));

    QSignalSpy itemsSpy(stub.fileManager, &FileManager1Service::itemsRequested);
    QVERIFY(itemsSpy.isValid());

    QDBusInterface caller(kFileManagerService, kFileManagerPath, kFileManagerInterface,
                          QDBusConnection::sessionBus());
    QVERIFY(caller.isValid());

    const QStringList uris{QUrl::fromLocalFile(kFirstPath).toString(),
                           QUrl::fromLocalFile(kSecondPath).toString()};
    QDBusPendingCall pending = caller.asyncCall(QStringLiteral("ShowItems"), uris,
                                                QStringLiteral("startup-1"));
    QDBusPendingCallWatcher watcher(pending);
    QVERIFY(waitForCall(&watcher));
    QVERIFY(watcher.isError());
    QCOMPARE(watcher.error().name(), kCancelledError);

    const QStringList expectedPaths{kFirstPath, kSecondPath};
    const QList<int> expectedIndexes{-1, -1};
    const QList<bool> expectedFocus{true, true};
    QCOMPARE(g_openedPaths, expectedPaths);
    QCOMPARE(g_openedIndexes, expectedIndexes);
    QCOMPARE(g_openedFocus, expectedFocus);
    QCOMPARE(itemsSpy.count(), 1);
    QCOMPARE(itemsSpy.at(0).at(0).toStringList(), uris);
}

void PortalSelfTest::fileChooserOpenFileReturnsCancelled()
{
    PortalServiceStub stub;
    QString error;
    QVERIFY2(stub.start(&error), qPrintable(error));

    QDBusInterface caller(kImplService, kDesktopPath, kChooserInterface,
                          QDBusConnection::sessionBus());
    QVERIFY(caller.isValid());

    QDBusPendingCall pending =
        caller.asyncCall(QStringLiteral("OpenFile"),
                         QVariant::fromValue(QDBusObjectPath(kOpenHandle)), kAppId,
                         kParentWindow, kChooserTitle, chooserOptions());
    QDBusPendingCallWatcher watcher(pending);
    QVERIFY(waitForCall(&watcher));
    QVERIFY(!watcher.isError());

    const QList<QVariant> arguments = watcher.reply().arguments();
    QCOMPARE(arguments.size(), 2);
    QCOMPARE(arguments.at(0).toUInt(), kCancelledResponse);
    QVERIFY(arguments.at(1).toMap().isEmpty());
}

void PortalSelfTest::fileChooserSaveFileReturnsCancelled()
{
    PortalServiceStub stub;
    QString error;
    QVERIFY2(stub.start(&error), qPrintable(error));

    QDBusInterface caller(kImplService, kDesktopPath, kChooserInterface,
                          QDBusConnection::sessionBus());
    QVERIFY(caller.isValid());

    QDBusPendingCall pending =
        caller.asyncCall(QStringLiteral("SaveFile"),
                         QVariant::fromValue(QDBusObjectPath(kSaveHandle)), kAppId,
                         kParentWindow, kChooserTitle, chooserOptions());
    QDBusPendingCallWatcher watcher(pending);
    QVERIFY(waitForCall(&watcher));
    QVERIFY(!watcher.isError());

    const QList<QVariant> arguments = watcher.reply().arguments();
    QCOMPARE(arguments.size(), 2);
    QCOMPARE(arguments.at(0).toUInt(), kCancelledResponse);
    QVERIFY(arguments.at(1).toMap().isEmpty());
}

void PortalSelfTest::backendKeepsFileChooserWhenFileManager1IsTaken()
{
    FileManagerNameOwner nameOwner;
    QVERIFY(nameOwner.acquired());

    BoundedProcess child;
    child.process.start(QStringLiteral(HERMES_PORTAL_BACKEND_PATH));
    QVERIFY2(child.process.waitForStarted(2000),
             qPrintable(child.process.errorString()));

    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.interface() != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        bus.interface()->isServiceRegistered(kImplService).value(), 3000);
    QCOMPARE(child.process.state(), QProcess::Running);
    QVERIFY(bus.interface()->isServiceRegistered(kFileManagerService).value());

    child.process.terminate();
    QVERIFY(child.process.waitForFinished(2000));
    QVERIFY(child.process.readAllStandardError().contains("FileManager1"));
}

void PortalSelfTest::portalConfigUsesForcedXdgConfigHome()
{
    QCOMPARE(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")), m_configDir.path());
    QVERIFY(configPath().startsWith(m_configDir.path() + QLatin1Char('/')));
    QVERIFY(backupPath().startsWith(m_configDir.path() + QLatin1Char('/')));
}

void PortalSelfTest::portalConfigEnableCreatesKeyAndBackup()
{
    resetPortalConfig();
    QVERIFY(!QFile::exists(configPath()));
    QVERIFY(!QFile::exists(backupPath()));

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QVERIFY(QFile::exists(configPath()));
    QVERIFY(QFile::exists(backupPath()));
    QCOMPARE(readFile(configPath()), kEnabledConfig);
    // A missing original must still be represented, so disable can remove the
    // generated config instead of restoring an empty file.
    QVERIFY(!readFile(backupPath()).isEmpty());
    QCOMPARE(PortalConfig::status(), kEnabledStatus);
}

void PortalSelfTest::portalConfigEnablePreservesUnrelatedLines()
{
    resetPortalConfig();
    const QByteArray original("[preferred]\ndefault=gtk\n");
    writeConfig(original);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(backupPath()), original);
    QCOMPARE(readFile(configPath()),
             QByteArray("[preferred]\n"
                        "org.freedesktop.impl.portal.FileChooser=hermes\ndefault=gtk\n"));
    QVERIFY(readFile(configPath()).contains("default=gtk\n"));
}

void PortalSelfTest::portalConfigEnableReplacesOnlyExistingValue()
{
    resetPortalConfig();
    const QByteArray original("[other]\nfoo=bar\n\n[preferred]\ndefault=gtk\n"
                              "org.freedesktop.impl.portal.FileChooser=gtk\nkeep=yes\n");
    writeConfig(original);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(backupPath()), original);
    QCOMPARE(readFile(configPath()),
             QByteArray("[other]\nfoo=bar\n\n[preferred]\ndefault=gtk\n"
                        "org.freedesktop.impl.portal.FileChooser=hermes\nkeep=yes\n"));
}

void PortalSelfTest::portalConfigEnableAppendsSectionWithExistingLineEnding()
{
    resetPortalConfig();
    const QByteArray original("[other]\r\nfoo=bar\r\n");
    writeConfig(original);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(backupPath()), original);
    QCOMPARE(readFile(configPath()),
             QByteArray("[other]\r\nfoo=bar\r\n[preferred]\r\n"
                        "org.freedesktop.impl.portal.FileChooser=hermes\r\n"));

    resetPortalConfig();
    writeConfig(QByteArray("[other]"));
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(configPath()),
             QByteArray("[other]\n[preferred]\n"
                        "org.freedesktop.impl.portal.FileChooser=hermes\n"));
}

void PortalSelfTest::portalConfigEnableIsIdempotent()
{
    resetPortalConfig();
    const QByteArray original("[preferred]\ndefault=gtk\n");
    writeConfig(original);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(backupPath()), original);
    QCOMPARE(readFile(configPath()),
             QByteArray("[preferred]\n"
                        "org.freedesktop.impl.portal.FileChooser=hermes\ndefault=gtk\n"));
}

void PortalSelfTest::portalConfigDisableRestoresExactOriginalBytes()
{
    resetPortalConfig();
    // Mixed line endings and no final newline catch any normalisation.
    const QByteArray original("[preferred]\r\ndefault=gtk\r\n\n[other]\r\nfoo=bar");
    writeConfig(original);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QVERIFY(readFile(configPath()) != original);
    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));
    QVERIFY(!QFile::exists(backupPath()));
    QCOMPARE(readFile(configPath()), original);
    QCOMPARE(PortalConfig::status(), kDisabledStatus);
}

void PortalSelfTest::portalConfigDisableRemovesConfigWhenOriginalMissing()
{
    resetPortalConfig();
    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QVERIFY(QFile::exists(configPath()));

    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));
    QVERIFY(!QFile::exists(configPath()));
    QVERIFY(!QFile::exists(backupPath()));
}

void PortalSelfTest::portalConfigDisableWithoutBackupRemovesOnlyHermesKey()
{
    resetPortalConfig();
    writeConfig(QByteArray("[preferred]\ndefault=gtk\n"
                           "org.freedesktop.impl.portal.FileChooser=hermes\nkeep=yes\n"));
    QVERIFY(!QFile::exists(backupPath()));

    QString error;
    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(configPath()),
             QByteArray("[preferred]\ndefault=gtk\nkeep=yes\n"));
}

void PortalSelfTest::portalConfigDisableWithoutBackupKeepsForeignFileChooserValue()
{
    resetPortalConfig();
    const QByteArray original("[preferred]\n"
                              "org.freedesktop.impl.portal.FileChooser=gtk\n");
    writeConfig(original);
    QVERIFY(!QFile::exists(backupPath()));

    QString error;
    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));
    QCOMPARE(readFile(configPath()), original);
    QVERIFY(!QFile::exists(backupPath()));
}

void PortalSelfTest::portalConfigStatusTracksEnableAndDisable()
{
    resetPortalConfig();
    QCOMPARE(PortalConfig::status(), kDisabledStatus);

    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QCOMPARE(PortalConfig::status(), kEnabledStatus);

    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));
    QCOMPARE(PortalConfig::status(), kDisabledStatus);
}

void PortalSelfTest::portalConfigNeverWritesUnderRealHome()
{
    resetPortalConfig();
    QString error;
    QVERIFY2(PortalConfig::enableFileChooser(&error), qPrintable(error));
    QVERIFY2(PortalConfig::disableFileChooser(&error), qPrintable(error));

    if (m_realHomeTracked) {
        QCOMPARE(QFile::exists(m_realHomeConfig), m_realHomeExisted);
        QCOMPARE(QFile::exists(m_realHomeBackup), m_realHomeBackupExisted);
        if (m_realHomeExisted)
            QCOMPARE(readFile(m_realHomeConfig), m_realHomeBytes);
        if (m_realHomeBackupExisted)
            QCOMPARE(readFile(m_realHomeBackup), m_realHomeBackupBytes);
    }
}

QTEST_MAIN(PortalSelfTest)
#include "portal-selftest.moc"
