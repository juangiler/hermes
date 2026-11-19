#include "directory_model.h"
#include "shell_controller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <cstring>

#ifndef HERMES_APP_VERSION
#error "HERMES_APP_VERSION must come from src/app.c via CMake"
#endif

namespace {

// Backstop for a scan that neither applies nor reports failure. A debug run
// must always finish; a genuinely slow scan has the whole budget to complete.
constexpr int kScanWatchdogMs = 30000;

// One window grab is only valid after the scene graph has rendered a frame;
// grabbing any earlier yields an empty image. The screenshot additionally waits
// for the first scan so the captured frame shows the directory's entries.
class FrameGrabber : public QObject
{
public:
  FrameGrabber(QQuickWindow *window, DirectoryModel *model)
      : m_window(window), m_model(model)
  {
    connect(window, &QQuickWindow::afterRendering, this, &FrameGrabber::onAfterRendering);
    connect(model, &DirectoryModel::navigateFailed, this,
            [this](const QString &path) { fail(QStringLiteral("cannot scan ") + path); });
    window->installEventFilter(this);

    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
      fail(QStringLiteral("timed out waiting for the first directory scan"));
    });
  }

  void grabOnce(const QString &file)
  {
    m_shotFile = file;
    connect(m_model, &DirectoryModel::scanFinished, this, [this] {
      m_scanDone = true;
      m_watchdog.stop();
      // Applying rows invalidates the scene, but force a frame so a render is
      // guaranteed even when the offscreen platform would otherwise idle.
      m_window->update();
    });
    m_watchdog.start(kScanWatchdogMs);
  }

  void recordTo(const QString &dir)
  {
    QDir().mkpath(dir);
    m_recordDir = dir;
  }

  bool failed() const { return m_failed; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    if (watched == m_window && event->type() == QEvent::MouseButtonPress
        && !m_recordDir.isEmpty())
      m_recordPending = true;
    return QObject::eventFilter(watched, event);
  }

private:
  // A debug surface that cannot produce its output must report failure, never
  // idle in the event loop waiting for a signal that will not arrive.
  void fail(const QString &message)
  {
    if (m_failed)
      return;
    m_failed = true;
    m_watchdog.stop();
    m_shotFile.clear();
    m_recordDir.clear();
    qCritical() << message;
    QCoreApplication::quit();
  }

  void onAfterRendering()
  {
    // grabWindow() may schedule another render; do not re-enter this callback.
    if (m_grabbing)
      return;
    if (!m_shotFile.isEmpty()) {
      if (!m_scanDone)
        return;
      m_grabbing = true;
      const QImage image = m_window->grabWindow();
      const QString file = m_shotFile;
      m_shotFile.clear();
      m_grabbing = false;
      if (image.save(file))
        QCoreApplication::quit();
      else
        fail(QStringLiteral("cannot write screenshot ") + file);
      return;
    }
    if (!m_recordDir.isEmpty() && m_recordPending) {
      m_recordPending = false;
      m_grabbing = true;
      const QImage image = m_window->grabWindow();
      const QString file = m_recordDir + QStringLiteral("/frame-%1.png")
                               .arg(++m_recordCount, 4, 10, QLatin1Char('0'));
      m_grabbing = false;
      if (!image.save(file))
        fail(QStringLiteral("cannot write recorded frame ") + file);
    }
  }

  QQuickWindow *m_window;
  DirectoryModel *m_model;
  QTimer m_watchdog;
  QString m_shotFile;
  QString m_recordDir;
  int m_recordCount = 0;
  bool m_scanDone = false;
  bool m_recordPending = false;
  bool m_grabbing = false;
  bool m_failed = false;
};

} // namespace

int main(int argc, char *argv[])
{
  bool showHelp = false;
  bool showVersion = false;
  QString screenshotFile;
  for (int i = 1; i < argc; ++i) {
    showHelp = showHelp || std::strcmp(argv[i], "--help") == 0;
    showVersion = showVersion || std::strcmp(argv[i], "--version") == 0;
    if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
      screenshotFile = QString::fromLocal8Bit(argv[++i]);
  }
  if (showHelp) {
    QTextStream(stdout) << QStringLiteral("Usage: hermes-qt [--help] [--version] "
                                          "[--screenshot FILE] [PATH|FILE|URI]...\n");
    return 0;
  }
  if (showVersion) {
    QTextStream(stdout) << QStringLiteral("Hermes %1 (Kirigami UI)\n")
                              .arg(QStringLiteral(HERMES_APP_VERSION));
    return 0;
  }

  const QString recordDir = QString::fromLocal8Bit(qgetenv("HERMES_RECORD"));

  QGuiApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments();
  installShellCoreHost();
  int result = 0;
  {
    ShellController controller;
    QStringList paths;
    for (int i = 1; i < args.size(); ++i) {
      const QString &arg = args.at(i);
      if (arg.startsWith(QStringLiteral("--"))) {
        if (arg == QStringLiteral("--screenshot"))
          ++i; // the output filename is not a path to open
        continue;
      }
      paths.append(arg);
    }
    if (!screenshotFile.isEmpty() && paths.isEmpty())
      paths.append(QDir::homePath());
    controller.setPaths(paths);

    const bool debugMode = !screenshotFile.isEmpty() || !recordDir.isEmpty();
    // setPaths() leaves the model without a path when the argument cannot be
    // opened as a directory, and an immediate scan failure restores it too.
    // No scan will follow, so do not start the event loop at all.
    if (debugMode && controller.directory()->path().isEmpty()) {
      qCritical() << QStringLiteral("cannot scan directory for debug mode:")
                  << (paths.isEmpty() ? QStringLiteral("<none>") : paths.constFirst());
      result = 1;
    } else {
      QQmlApplicationEngine engine;
      engine.rootContext()->setContextProperty(QStringLiteral("hermes"), &controller);
      engine.load(QUrl(QStringLiteral("qrc:/qtui/qml/main.qml")));
      if (engine.rootObjects().isEmpty()) {
        qCritical() << QStringLiteral("failed to load qrc:/qtui/qml/main.qml");
        result = 1;
      } else if (!debugMode) {
        result = app.exec();
      } else {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window) {
          qCritical() << QStringLiteral("debug mode requires a QQuickWindow root object");
          result = 1;
        } else {
          FrameGrabber grabber(window, controller.directory());
          window->resize(1280, 800);
          window->show();
          if (!screenshotFile.isEmpty())
            grabber.grabOnce(screenshotFile);
          if (!recordDir.isEmpty())
            grabber.recordTo(recordDir);
          result = app.exec();
          if (grabber.failed())
            result = 1;
        }
      }
    }
  }
  hermes_core_set_host(nullptr);
  return result;
}
