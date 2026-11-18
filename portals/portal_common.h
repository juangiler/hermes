// Shared reply type for the portal clients.
#pragma once

#include <QDBusError>
#include <QLatin1String>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

struct PortalReply
{
    enum class Status { Ok, Unavailable, Cancelled, Error };

    Status status = Status::Ok;
    QString errorName;
    QVariantMap results;
};

Q_DECLARE_METATYPE(PortalReply)

namespace PortalCommon {

// Qt names some of these errors differently from the D-Bus names, so match
// both the enum and the wire name (e.g. InterfaceError vs UnknownInterface).
inline bool isUnavailable(const QDBusError &error)
{
    if (error.type() == QDBusError::ServiceUnknown
        || error.type() == QDBusError::UnknownObject
        || error.type() == QDBusError::UnknownInterface) {
        return true;
    }
    const QString name = error.name();
    return name.endsWith(QLatin1String(".ServiceUnknown"))
        || name.endsWith(QLatin1String(".UnknownObject"))
        || name.endsWith(QLatin1String(".InterfaceError"))
        || name.endsWith(QLatin1String(".UnknownInterface"));
}

inline PortalReply fromError(const QDBusError &error)
{
    PortalReply reply;
    reply.status = isUnavailable(error) ? PortalReply::Status::Unavailable
                                        : PortalReply::Status::Error;
    reply.errorName = error.name();
    return reply;
}

} // namespace PortalCommon
