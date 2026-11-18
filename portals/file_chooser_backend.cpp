#include "file_chooser_backend.h"

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>

namespace {
const QString kImplName = QStringLiteral("org.freedesktop.impl.portal.desktop.hermes");
const QString kDesktopPath = QStringLiteral("/org/freedesktop/portal/desktop");
// org.freedesktop.portal.Request.Response: 1 = user cancelled, 2 = ended
// in some other way. The no-picker path is a cancellation, not an error.
const uint kCancelledResponse = 1;
} // namespace

// Hand-written: see dbus-interfaces/org.freedesktop.impl.portal.FileChooser.xml
// for the authoritative method signatures. No Request.Response is emitted;
// that signal belongs to the frontend org.freedesktop.portal.Request interface.
class FileChooserAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")

public:
    explicit FileChooserAdaptor(FileChooserBackend *backend)
        : QDBusAbstractAdaptor(backend), m_backend(backend) {}

public slots:
    void OpenFile(const QDBusObjectPath &handle, const QString &appId,
                  const QString &parentWindow, const QString &title,
                  const QVariantMap &options, uint &response, QVariantMap &results)
    {
        m_backend->recordRequest(handle.path(), appId, parentWindow, title, false, options);
        response = kCancelledResponse;
        results = QVariantMap();
    }

    void SaveFile(const QDBusObjectPath &handle, const QString &appId,
                  const QString &parentWindow, const QString &title,
                  const QVariantMap &options, uint &response, QVariantMap &results)
    {
        m_backend->recordRequest(handle.path(), appId, parentWindow, title, true, options);
        response = kCancelledResponse;
        results = QVariantMap();
    }

private:
    FileChooserBackend *m_backend = nullptr;
};

FileChooserBackend::FileChooserBackend(QObject *parent) : QObject(parent)
{
    new FileChooserAdaptor(this);
}

FileChooserBackend::~FileChooserBackend()
{
    if (m_registered && m_bus) {
        m_bus->unregisterObject(kDesktopPath);
        m_bus->unregisterService(kImplName);
    }
}

bool FileChooserBackend::registerService(QDBusConnection bus, QString *error)
{
    if (!bus.isConnected()) {
        if (error)
            *error = QStringLiteral("session bus is not connected");
        return false;
    }
    if (!bus.registerService(kImplName)) {
        if (error)
            *error = QStringLiteral("could not own ") + kImplName;
        return false;
    }
    if (!bus.registerObject(kDesktopPath, this)) {
        bus.unregisterService(kImplName);
        if (error)
            *error = QStringLiteral("could not register ") + kDesktopPath;
        return false;
    }
    m_bus = bus;
    m_registered = true;
    return true;
}

void FileChooserBackend::recordRequest(const QString &handle, const QString &appId,
                                       const QString &parentWindow, const QString &title,
                                       bool save, const QVariantMap &options)
{
    RequestState state;
    state.appId = appId;
    state.parentWindow = parentWindow;
    state.title = title;
    state.save = save;
    state.filters = options.value(QStringLiteral("filters"));
    state.multiple = options.value(QStringLiteral("multiple")).toBool();
    state.currentFolder = options.value(QStringLiteral("current_folder")).toByteArray();
    m_requests.insert(handle, state);
}

#include "file_chooser_backend.moc"
