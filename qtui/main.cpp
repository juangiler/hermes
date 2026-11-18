#include "shell_controller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
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

  installShellCoreHost();
  int result = 0;
  {
    ShellController controller;
    QStringList paths;
    for (const QString &arg : args.mid(1)) {
      if (!arg.startsWith(QStringLiteral("--")))
        paths.append(arg);
    }
    controller.setPaths(paths);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
      qCritical() << QStringLiteral("failed to load qrc:/qtui/qml/main.qml");
      result = 1;
    } else {
      result = app.exec();
    }
  }
  hermes_core_set_host(nullptr);
  return result;
}
