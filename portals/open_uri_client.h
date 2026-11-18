// Asynchronous client for org.freedesktop.portal.OpenURI.
#pragma once

#include "portal_common.h"

#include <QDBusInterface>
#include <QObject>
#include <QString>
#include <QVariantMap>

class OpenUriClient : public QObject
{
    Q_OBJECT

public:
    explicit OpenUriClient(QObject *parent = nullptr);

    void openFile(const QString &path, const QString &parentWindow);

signals:
    void finished(const PortalReply &reply);

private slots:
    void onRequestResponse(uint response, const QVariantMap &results);

private:
    QDBusInterface *m_iface;
    QString m_requestPath;
};
