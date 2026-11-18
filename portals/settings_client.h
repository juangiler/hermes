// Asynchronous client for org.freedesktop.portal.Settings.
#pragma once

#include "portal_common.h"

#include <QDBusInterface>
#include <QObject>
#include <QString>
#include <QVariant>

class SettingsClient : public QObject
{
    Q_OBJECT

public:
    explicit SettingsClient(QObject *parent = nullptr);

    void readOne(const QString &ns, const QString &key);

signals:
    void settingRead(const QString &ns, const QString &key, const QVariant &value);
    void failed(const PortalReply &reply);

private:
    QDBusInterface *m_iface;
    QString m_namespace;
    QString m_key;
};
