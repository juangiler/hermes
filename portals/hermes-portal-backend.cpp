// D-Bus-activatable portal backend. With no CLI flags it owns the FileManager1
// and FileChooser services for the lifetime of the session; Task 5 implements
// the opt-in configuration flags, which keep their print-and-exit behaviour.
#include "file_chooser_backend.h"
#include "file_manager1_service.h"

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
        QStringLiteral("Hermes desktop portal backend (headless stub)"));
    parser.addHelpOption();
    const QCommandLineOption enableFileChooser(
        QStringLiteral("enable-filechooser"),
        QStringLiteral("Opt in to the Hermes file-chooser portal (stub)."));
    const QCommandLineOption disableFileChooser(
        QStringLiteral("disable-filechooser"),
        QStringLiteral("Opt out of the Hermes file-chooser portal (stub)."));
    const QCommandLineOption status(
        QStringLiteral("status"),
        QStringLiteral("Print portal role status (stub)."));
    parser.addOption(enableFileChooser);
    parser.addOption(disableFileChooser);
    parser.addOption(status);
    parser.process(app);

    QTextStream out(stdout);
    if (parser.isSet(enableFileChooser)) {
        out << QStringLiteral("filechooser: enable requested (stub)\n");
    } else if (parser.isSet(disableFileChooser)) {
        out << QStringLiteral("filechooser: disable requested (stub)\n");
    } else if (parser.isSet(status)) {
        out << QStringLiteral("filechooser: status requested (stub)\n");
    } else {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QString error;
        if (!bus.isConnected()) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: no session bus\n");
            return 1;
        }

        FileManager1Service fileManager;
        if (!fileManager.registerService(bus, &error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error << '\n';
            return 1;
        }
        FileChooserBackend chooser;
        if (!chooser.registerService(bus, &error)) {
            QTextStream(stderr) << QStringLiteral("hermes-portal-backend: ") << error << '\n';
            return 1;
        }
        return app.exec();
    }
    return 0;
}
