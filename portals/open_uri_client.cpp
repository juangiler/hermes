#include "open_uri_client.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLatin1String>
#include <QUrl>

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.OpenURI");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
const QString kResponseSignal = QStringLiteral("Response");
const QByteArray kResponseSlot = SLOT(onResponse(uint,QVariantMap));

PortalReply replyFromResponse(uint response, const QVariantMap &results)
{
    PortalReply reply;
    reply.results = results;
    switch (response) {
    case 0:
        reply.status = PortalReply::Status::Ok;
        break;
    case 1:
        reply.status = PortalReply::Status::Cancelled;
        break;
    default:
        reply.status = PortalReply::Status::Error;
        reply.errorName = results.value(QLatin1String("error")).toString();
        break;
    }
    return reply;
}
}

void PortalDetail::OpenUriRequestRelay::onResponse(uint response, const QVariantMap &results)
{
    emit responded(response, results);
}

OpenUriClient::OpenUriClient(QObject *parent)
    : QObject(parent),
      m_iface(new QDBusInterface(kService, kPath, kInterface,
                                 QDBusConnection::sessionBus(), this))
{
}

void OpenUriClient::openFile(const QString &path, const QString &parentWindow)
{
    // The URI travels outwards; the core's uri_to_path helper is for inbound URIs.
    const QString uri = QUrl::fromLocalFile(path).toString();
    QDBusPendingCall call = m_iface->asyncCall(QLatin1String("OpenURI"),
                                               parentWindow, uri, QVariantMap());
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (watcher->isError()) {
            emit finished(PortalCommon::fromError(watcher->error()));
            return;
        }

        const QDBusObjectPath handle =
            watcher->reply().arguments().value(0).value<QDBusObjectPath>();
        if (handle.path().isEmpty()) {
            PortalReply reply;
            reply.status = PortalReply::Status::Error;
            reply.errorName = QStringLiteral("InvalidRequestHandle");
            emit finished(reply);
            return;
        }

        auto *relay = new PortalDetail::OpenUriRequestRelay(this);
        connect(relay, &PortalDetail::OpenUriRequestRelay::responded, this,
                [this, relay](uint response, const QVariantMap &results) {
                    relay->deleteLater();
                    emit finished(replyFromResponse(response, results));
                });

        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.connect(kService, handle.path(), kRequestInterface,
                         kResponseSignal, relay, kResponseSlot.constData())) {
            relay->deleteLater();
            PortalReply reply;
            reply.status = PortalReply::Status::Error;
            reply.errorName = QStringLiteral("RequestSubscribeFailed");
            emit finished(reply);
        }
    });
}
