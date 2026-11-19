#include "shell_controller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <cstring>

#ifndef HERMES_APP_VERSION
#error "HERMES_APP_VERSION must come from src/app.c via CMake"
#endif

int main(int argc, char *argv[])
{
  bool showHelp = false;
  bool showVersion = false;
  for (int i = 1; i < argc; ++i) {
    showHelp = showHelp || std::strcmp(argv[i], "--help") == 0;
    showVersion = showVersion || std::strcmp(argv[i], "--version") == 0;
  }
  if (showHelp) {
    QTextStream(stdout) << QStringLiteral("Usage: hermes-qt [--help] [--version] [PATH|FILE|URI]...\n");
    return 0;
  }
  if (showVersion) {
    QTextStream(stdout) << QStringLiteral("Hermes %1 (Kirigami UI)\n")
                              .arg(QStringLiteral(HERMES_APP_VERSION));
    return 0;
  }

  QGuiApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments();
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
