#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTextStream>
#include <QUrl>

int main(int argc, char *argv[])
{
  QGuiApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments();
  if (args.contains(QStringLiteral("--version"))) {
    QTextStream(stdout) << QStringLiteral("Hermes (Kirigami UI) migration scaffold\n");
    return 0;
  }
  if (args.contains(QStringLiteral("--help"))) {
    QTextStream(stdout) << QStringLiteral("Usage: hermes-qt [--help] [--version] [PATH|FILE|URI]...\n");
    return 0;
  }

  QObject hermes_placeholder;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &hermes_placeholder);
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  if (engine.rootObjects().isEmpty()) {
    qCritical() << QStringLiteral("failed to load qrc:/qtui/qml/main.qml");
    return 1;
  }
  return app.exec();
}
