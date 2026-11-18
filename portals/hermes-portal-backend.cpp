// D-Bus-activatable portal backend. This is a CLI stub until tasks 4-5 attach
// the services and the opt-in configuration to this same long-lived process.
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
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
    } else {
        out << QStringLiteral("filechooser: status requested (stub)\n");
    }
    return 0;
}
