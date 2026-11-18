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
const QByteArray kResponseSlot = SLOT(onRequestResponse(uint,QVariantMap));
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
    QDBusPendingCall call = m_iface->asyncCall(QLatin1String("OpenFile"),
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

        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!m_requestPath.isEmpty()) {
            bus.disconnect(kService, m_requestPath, kRequestInterface,
                           kResponseSignal, this, kResponseSlot.constData());
        }
        m_requestPath = handle.path();
        if (!bus.connect(kService, m_requestPath, kRequestInterface,
                         kResponseSignal, this, kResponseSlot.constData())) {
            m_requestPath.clear();
            PortalReply reply;
            reply.status = PortalReply::Status::Error;
            reply.errorName = QStringLiteral("RequestSubscribeFailed");
            emit finished(reply);
        }
    });
}

void OpenUriClient::onRequestResponse(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(kService, m_requestPath, kRequestInterface,
                                             kResponseSignal, this, kResponseSlot.constData());
    m_requestPath.clear();

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
    emit finished(reply);
}
