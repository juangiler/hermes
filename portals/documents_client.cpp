#include "documents_client.h"

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLatin1String>
#include <QVariantMap>

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.Documents");
}

DocumentsClient::DocumentsClient(QObject *parent)
    : QObject(parent),
      m_iface(new QDBusInterface(kService, kPath, kInterface,
                                 QDBusConnection::sessionBus(), this))
{
}

void DocumentsClient::add(const QString &path, bool writable)
{
    QVariantMap options;
    options.insert(QLatin1String("writable"), writable);

    QDBusPendingCall call = m_iface->asyncCall(QLatin1String("Add"), path, options);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        PortalReply reply;
        QString id;
        if (watcher->isError()) {
            reply = PortalCommon::fromError(watcher->error());
        } else {
            id = watcher->reply().arguments().value(0).toString();
            reply.results.insert(QLatin1String("id"), id);
        }
        emit added(id, reply);
    });
}
