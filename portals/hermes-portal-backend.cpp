// D-Bus-activatable portal backend. With no CLI flags it owns the FileChooser
// service and, when no other file manager holds the name, FileManager1 for the
// lifetime of the session; the opt-in flags edit only the user's XDG
// portals.conf and then exit.
#include "file_chooser_backend.h"
#include "file_manager1_service.h"
#include "portal_config.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hermes-portal-backend"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Hermes desktop portal backend"));
    parser.addHelpOption();
    const QCommandLineOption enableFileChooser(
        QStringLiteral("enable-filechooser"),
        QStringLiteral("Opt in to the Hermes file-chooser portal."));
    const QCommandLineOption disableFileChooser(
        QStringLiteral("disable-filechooser"),
        QStringLiteral("Opt out of the Hermes file-chooser portal."));
    const QCommandLineOption status(
        QStringLiteral("status"),
        QStringLiteral("Print portal role status."));
    parser.addOption(enableFileChooser);
    parser.addOption(disableFileChooser);
    parser.addOption(status);
    parser.process(app);

    if (parser.isSet(enableFileChooser) && parser.isSet(disableFileChooser)) {
        QTextStream(stderr) << QStringLiteral("hermes-portal-backend: --enable-filechooser and "
                                              "--disable-filechooser are mutually exclusive\n");
        return 2;
    }

    QTextStream out(stdout);
    if (parser.isSet(enableFileChooser)) {
        QString error;
        if (!PortalConfig::enableFileChooser(&error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error << '\n';
            return 1;
        }
        out << PortalConfig::status() << '\n';
    } else if (parser.isSet(disableFileChooser)) {
        QString error;
        if (!PortalConfig::disableFileChooser(&error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error << '\n';
            return 1;
        }
        out << PortalConfig::status() << '\n';
    } else if (parser.isSet(status)) {
        out << PortalConfig::status() << '\n';
    } else {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QString error;
        if (!bus.isConnected()) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: no session bus\n");
            return 1;
        }

        // The opt-in role must survive a desktop file manager already owning
        // FileManager1; losing that optional name is only a warning.
        FileChooserBackend chooser;
        if (!chooser.registerService(bus, &error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error << '\n';
            return 1;
        }
        FileManager1Service fileManager;
        if (!fileManager.registerService(bus, &error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error
                                << QStringLiteral("; continuing without FileManager1\n");
        }
        return app.exec();
    }
    return 0;
}
