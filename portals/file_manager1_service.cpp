#include "file_manager1_service.h"

#include "../src/core_api.h"

#include <QDBusAbstractAdaptor>
#include <QUrl>

namespace {
const QString kServiceName = QStringLiteral("org.freedesktop.FileManager1");
const QString kObjectPath = QStringLiteral("/org/freedesktop/FileManager1");
const QString kCancelledError = QStringLiteral("org.freedesktop.portal.Error.Cancelled");
} // namespace

// Hand-written: qt6-tools/qdbusxml2cpp is not available in the build images.
// The wire contract lives in dbus-interfaces/org.freedesktop.FileManager1.xml.
class FileManager1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1Adaptor(FileManager1Service *service)
        : QDBusAbstractAdaptor(service), m_service(service) {}

public slots:
    void ShowItems(const QStringList &uris, const QString &startupId)
    {
        m_service->showItems(uris, startupId);
    }

private:
    FileManager1Service *m_service = nullptr;
};

FileManager1Service::FileManager1Service(QObject *parent)
    : QObject(parent), m_core(hermes_core_create(nullptr, nullptr))
{
    new FileManager1Adaptor(this);
}

FileManager1Service::~FileManager1Service()
{
    if (m_registered && m_bus) {
        m_bus->unregisterObject(kObjectPath);
        m_bus->unregisterService(kServiceName);
    }
    hermes_core_destroy(m_core);
}

bool FileManager1Service::registerService(QDBusConnection bus, QString *error)
{
    if (!bus.isConnected()) {
        if (error)
            *error = QStringLiteral("session bus is not connected");
        return false;
    }
    if (!bus.registerService(kServiceName)) {
        if (error)
            *error = QStringLiteral("could not own ") + kServiceName;
        return false;
    }
    if (!bus.registerObject(kObjectPath, this)) {
        bus.unregisterService(kServiceName);
        if (error)
            *error = QStringLiteral("could not register ") + kObjectPath;
        return false;
    }
    m_bus = bus;
    m_registered = true;
    return true;
}

void FileManager1Service::showItems(const QStringList &uris, const QString &startupId)
{
    Q_UNUSED(startupId);

    // Decode once here so the host hook receives real paths, then hand the
    // canonical file:// URIs to the C ABI, which decodes them a second time.
    QList<QByteArray> converted;
    converted.reserve(uris.size());
    for (const QString &uri : uris) {
        const QByteArray input = uri.toUtf8();
        char path[4096];
        if (hermes_core_uri_to_path(input.constData(), path, sizeof path) != 0) {
            sendErrorReply(kCancelledError, QStringLiteral("invalid file URI"));
            return;
        }
        converted.append(QUrl::fromLocalFile(QString::fromUtf8(path))
                              .toString(QUrl::FullyEncoded)
                              .toUtf8());
    }

    QList<const char *> argv;
    argv.reserve(converted.size());
    for (const QByteArray &uri : converted)
        argv.append(uri.constData());

    if (hermes_core_open_uris(m_core, argv.data(), static_cast<size_t>(argv.size()), true) < 0) {
        sendErrorReply(kCancelledError, QStringLiteral("no file manager UI attached"));
        return;
    }

    emit itemsRequested(uris);
    sendErrorReply(kCancelledError, QStringLiteral("no file manager UI attached"));
}

#include "file_manager1_service.moc"
