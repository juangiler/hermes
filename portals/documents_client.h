// Asynchronous client for org.freedesktop.portal.Documents.
#pragma once

#include "portal_common.h"

#include <QDBusInterface>
#include <QObject>
#include <QString>

class DocumentsClient : public QObject
{
    Q_OBJECT

public:
    explicit DocumentsClient(QObject *parent = nullptr);

    void add(const QString &path, bool writable);

signals:
    void added(const QString &id, const PortalReply &reply);

private:
    QDBusInterface *m_iface;
};
