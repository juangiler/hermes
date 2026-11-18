// Asynchronous client for org.freedesktop.portal.OpenURI.
#pragma once

#include "portal_common.h"

#include <QDBusInterface>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace PortalDetail {

// One relay per request keeps overlapping Response signals independent.
class OpenUriRequestRelay : public QObject
{
    Q_OBJECT

public:
    explicit OpenUriRequestRelay(QObject *parent = nullptr)
        : QObject(parent) {}

public slots:
    void onResponse(uint response, const QVariantMap &results);

signals:
    void responded(uint response, const QVariantMap &results);
};

} // namespace PortalDetail

class OpenUriClient : public QObject
{
    Q_OBJECT

public:
    explicit OpenUriClient(QObject *parent = nullptr);

    void openFile(const QString &path, const QString &parentWindow);

signals:
    void finished(const PortalReply &reply);

private:
    QDBusInterface *m_iface;
};
