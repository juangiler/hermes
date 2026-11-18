#include "directory_model.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <memory>

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

  std::unique_ptr<HermesCore, decltype(&hermes_core_destroy)> core(
      hermes_core_create([](void *) {}, nullptr), &hermes_core_destroy);
  DirectoryModel directory(core.get());
  QQmlApplicationEngine engine;
  // Task 4 replaces this placeholder with the full controller.
  engine.rootContext()->setContextProperty(QStringLiteral("hermes"),
      QVariantMap{{QStringLiteral("directory"), QVariant::fromValue<QObject *>(&directory)}});
  engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
  if (engine.rootObjects().isEmpty()) {
    qCritical() << QStringLiteral("failed to load qrc:/qtui/qml/main.qml");
    return 1;
  }
  return app.exec();
}
