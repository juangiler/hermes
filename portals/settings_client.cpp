#include "settings_client.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QLatin1String>

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Settings");
}

SettingsClient::SettingsClient(QObject *parent)
    : QObject(parent),
      m_iface(new QDBusInterface(kService, kPath, kInterface,
                                 QDBusConnection::sessionBus(), this))
{
}

void SettingsClient::readOne(const QString &ns, const QString &key)
{
    m_namespace = ns;
    m_key = key;

    QDBusPendingCall call = m_iface->asyncCall(QLatin1String("ReadOne"), ns, key);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (watcher->isError()) {
            emit failed(PortalCommon::fromError(watcher->error()));
            return;
        }

        // Portals wrap the value in an a{sv} envelope with a "v" entry. A D-Bus
        // variant reaches the caller as a QDBusArgument, so demarshal it first.
        QVariant value = watcher->reply().arguments().value(0);
        if (value.canConvert<QDBusVariant>())
            value = value.value<QDBusVariant>().variant();
        if (value.canConvert<QDBusArgument>()) {
            QDBusArgument argument = value.value<QDBusArgument>();
            QVariantMap envelope;
            argument >> envelope;
            value = envelope.value(QLatin1String("v"));
        } else {
            const QVariantMap envelope = value.toMap();
            if (envelope.contains(QLatin1String("v")))
                value = envelope.value(QLatin1String("v"));
        }

        emit settingRead(m_namespace, m_key, value);
    });
}
