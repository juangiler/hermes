#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

struct HermesCore;

// Exposes org.freedesktop.FileManager1.ShowItems(). With no UI attached every
// call opens the requested paths through the host hook and then cancels.
class FileManager1Service : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    explicit FileManager1Service(QObject *parent = nullptr);
    ~FileManager1Service() override;

    bool registerService(QDBusConnection bus, QString *error);

signals:
    void itemsRequested(const QStringList &uris);

private:
    friend class FileManager1Adaptor;
    void showItems(const QStringList &uris, const QString &startupId);

    HermesCore *m_core = nullptr;
    std::optional<QDBusConnection> m_bus;
    bool m_registered = false;
};
