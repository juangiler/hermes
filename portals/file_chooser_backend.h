#pragma once

#include <QByteArray>
#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>

// Implements the org.freedesktop.impl.portal.FileChooser backend interface.
// With no picker attached, calls return Cancelled immediately; per-handle
// state is kept so a later step can attach a picker to the same object.
class FileChooserBackend : public QObject
{
    Q_OBJECT

public:
    explicit FileChooserBackend(QObject *parent = nullptr);
    ~FileChooserBackend() override;

    bool registerService(QDBusConnection bus, QString *error);

private:
    friend class FileChooserAdaptor;

    struct RequestState
    {
        QString appId;
        QString parentWindow;
        QString title;
        bool save = false;
        bool multiple = false;
        QVariant filters;
        QByteArray currentFolder;
    };

    void recordRequest(const QString &handle, const QString &appId,
                       const QString &parentWindow, const QString &title,
                       bool save, const QVariantMap &options);

    QHash<QString, RequestState> m_requests;
    std::optional<QDBusConnection> m_bus;
    bool m_registered = false;
};
