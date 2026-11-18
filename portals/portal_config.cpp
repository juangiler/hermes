// Opt-in portal role configuration.
//
// QSettings(IniFormat) is deliberately not used: it rewrites and normalises
// whole ini files. Hermes must keep every unrelated byte of portals.conf
// unchanged and restore the original backup byte-for-byte, so the file is
// edited as text with QFile/QSaveFile instead.
#include "portal_config.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLatin1String>
#include <QSaveFile>
#include <QString>
#include <QtGlobal>

namespace {

const QLatin1String kConfigDirName("xdg-desktop-portal");
const QLatin1String kConfigFileName("portals.conf");
const QLatin1String kBackupSuffix(".hermes-backup");
const QByteArray kPreferredSection("[preferred]");
const QByteArray kFileChooserKey("org.freedesktop.impl.portal.FileChooser");
const QByteArray kHermesValue("hermes");

// A missing original needs a distinct representation so disable can remove the
// generated config rather than restoring an empty file that never existed.
const QByteArray kMissingOriginal("# hermes-backup: no original portals.conf\n");

struct Paths
{
    QString directory;
    QString config;
    QString backup;
};

// Read the environment on every call: callers (including tests) may set
// XDG_CONFIG_HOME after Qt has started.
Paths portalPaths()
{
    const QByteArray fromEnv = qgetenv("XDG_CONFIG_HOME");
    const QString base = fromEnv.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.config")
        : QString::fromLocal8Bit(fromEnv.constData());
    Paths paths;
    paths.directory = base + QLatin1Char('/') + kConfigDirName;
    paths.config = paths.directory + QLatin1Char('/') + kConfigFileName;
    paths.backup = paths.config + kBackupSuffix;
    return paths;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool readAll(const QString &path, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("cannot read %1: %2")
                               .arg(path, file.errorString()));
    }
    *bytes = file.readAll();
    return true;
}

bool writeAll(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(error, QStringLiteral("cannot write %1: %2")
                               .arg(path, file.errorString()));
    }
    if (file.write(bytes) != static_cast<qint64>(bytes.size())) {
        return fail(error, QStringLiteral("short write to %1").arg(path));
    }
    if (!file.commit()) {
        return fail(error, QStringLiteral("cannot commit %1: %2")
                               .arg(path, file.errorString()));
    }
    return true;
}

// One physical line, with the line ending kept separate so edits can preserve
// CRLF (or a missing final newline) exactly.
struct Line
{
    QByteArray raw;
    QByteArray body;
    QByteArray ending;
};

QList<Line> splitLines(const QByteArray &content)
{
    QList<Line> lines;
    qsizetype start = 0;
    while (start < content.size()) {
        const qsizetype newline = content.indexOf('\n', start);
        if (newline < 0) {
            const QByteArray body = content.mid(start);
            lines.append(Line{body, body, QByteArray()});
            break;
        }
        QByteArray body = content.mid(start, newline - start);
        QByteArray ending = "\n";
        if (!body.isEmpty() && body.endsWith('\r')) {
            body.chop(1);
            ending = "\r\n";
        }
        lines.append(Line{content.mid(start, newline - start + 1), body, ending});
        start = newline + 1;
    }
    return lines;
}

QByteArray joinLines(const QList<Line> &lines)
{
    QByteArray result;
    for (const Line &line : lines)
        result += line.raw;
    return result;
}

bool isSection(const QByteArray &body)
{
    const QByteArray trimmed = body.trimmed();
    return trimmed.size() >= 2 && trimmed.startsWith('[') && trimmed.endsWith(']');
}

bool isFileChooserKey(const QByteArray &body, qsizetype *equals)
{
    const qsizetype position = body.indexOf('=');
    if (position <= 0 || body.left(position).trimmed() != kFileChooserKey)
        return false;
    if (equals)
        *equals = position;
    return true;
}

struct KeyLocation
{
    qsizetype preferred = -1;
    qsizetype key = -1;
    qsizetype equals = -1;
};

KeyLocation findKey(const QList<Line> &lines)
{
    KeyLocation location;
    bool inPreferred = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QByteArray body = lines.at(i).body;
        if (isSection(body)) {
            inPreferred = body.trimmed() == kPreferredSection;
            if (inPreferred)
                location.preferred = i;
            continue;
        }
        if (inPreferred && isFileChooserKey(body, &location.equals)) {
            location.key = i;
            break;
        }
    }
    return location;
}

// Only a line Hermes itself wrote may be removed on disable. A foreign
// backend's value must survive an opt-out even when no backup exists.
bool isHermesKey(const QList<Line> &lines, const KeyLocation &location)
{
    return location.key >= 0
        && lines.at(location.key).body.mid(location.equals + 1).trimmed() == kHermesValue;
}

QByteArray keyLine(const QByteArray &ending)
{
    return kFileChooserKey + "=" + kHermesValue
        + (ending.isEmpty() ? QByteArray("\n") : ending);
}

// A new section should not mix newline styles into an existing CRLF file.
QByteArray dominantEnding(const QList<Line> &lines)
{
    for (const Line &line : lines) {
        if (line.ending == "\r\n")
            return QByteArray("\r\n");
    }
    return QByteArray("\n");
}

QByteArray replaceValue(const Line &line, qsizetype equals)
{
    qsizetype valueStart = equals + 1;
    while (valueStart < line.body.size()
           && (line.body.at(valueStart) == ' ' || line.body.at(valueStart) == '\t')) {
        ++valueStart;
    }
    return line.body.left(valueStart) + kHermesValue + line.ending;
}

QByteArray editedConfig(const QByteArray &content, bool enable)
{
    QList<Line> lines = splitLines(content);
    const KeyLocation location = findKey(lines);

    if (!enable) {
        if (isHermesKey(lines, location))
            lines.removeAt(location.key);
        return joinLines(lines);
    }

    if (location.key >= 0) {
        lines[location.key].raw = replaceValue(lines.at(location.key), location.equals);
    } else if (location.preferred >= 0) {
        const QByteArray ending = lines.at(location.preferred).ending;
        if (ending.isEmpty())
            lines[location.preferred].raw += QByteArray("\n");
        const QByteArray insertion = keyLine(ending);
        lines.insert(location.preferred + 1,
                     Line{insertion, kFileChooserKey + "=" + kHermesValue, ending});
    } else {
        const QByteArray ending = dominantEnding(lines);
        QByteArray addition;
        if (!lines.isEmpty() && !lines.constLast().raw.endsWith('\n'))
            addition += ending;
        addition += kPreferredSection + ending + keyLine(ending);
        lines.append(Line{addition, addition, QByteArray()});
    }
    return joinLines(lines);
}

bool fileChooserEnabled(const QByteArray &content)
{
    const QList<Line> lines = splitLines(content);
    return isHermesKey(lines, findKey(lines));
}

// Once the enabled file has been edited, the whole backup is no longer safe to
// restore: only the line Hermes wrote may change. A foreign value that enable
// replaced goes back in that line's place; otherwise the line is removed.
QByteArray disableDiverged(const QByteArray &content, const QByteArray &original)
{
    QList<Line> lines = splitLines(content);
    const KeyLocation location = findKey(lines);
    if (!isHermesKey(lines, location))
        return content;

    if (original != kMissingOriginal) {
        const QList<Line> originalLines = splitLines(original);
        const KeyLocation originalLocation = findKey(originalLines);
        if (originalLocation.key >= 0) {
            const QByteArray body = originalLines.at(originalLocation.key).body;
            const QByteArray ending = lines.at(location.key).ending;
            lines[location.key].raw = body + ending;
            return joinLines(lines);
        }
    }
    lines.removeAt(location.key);
    return joinLines(lines);
}

} // namespace

bool PortalConfig::enableFileChooser(QString *error)
{
    const Paths paths = portalPaths();
    if (!QDir().mkpath(paths.directory))
        return fail(error, QStringLiteral("cannot create %1").arg(paths.directory));

    // Back up the original bytes once; later enables must not overwrite it.
    if (!QFile::exists(paths.backup)) {
        QByteArray original;
        if (QFile::exists(paths.config)) {
            if (!readAll(paths.config, &original, error))
                return false;
        } else {
            original = kMissingOriginal;
        }
        if (!writeAll(paths.backup, original, error))
            return false;
    }

    QByteArray content;
    if (QFile::exists(paths.config) && !readAll(paths.config, &content, error))
        return false;
    const QByteArray updated = editedConfig(content, true);
    if (updated != content && !writeAll(paths.config, updated, error))
        return false;
    return true;
}

bool PortalConfig::disableFileChooser(QString *error)
{
    const Paths paths = portalPaths();
    if (QFile::exists(paths.backup)) {
        QByteArray original;
        if (!readAll(paths.backup, &original, error))
            return false;

        const bool hasConfig = QFile::exists(paths.config);
        QByteArray content;
        if (hasConfig && !readAll(paths.config, &content, error))
            return false;

        // Only the exact bytes enableFileChooser() produced may be replaced by
        // the backup. Any later edit keeps its place and every other byte.
        const QByteArray basis = original == kMissingOriginal ? QByteArray() : original;
        if (!hasConfig || content == editedConfig(basis, true)) {
            if (original == kMissingOriginal) {
                if (hasConfig && !QFile::remove(paths.config))
                    return fail(error, QStringLiteral("cannot remove %1").arg(paths.config));
            } else if (!writeAll(paths.config, original, error)) {
                return false;
            }
        } else {
            const QByteArray updated = disableDiverged(content, original);
            if (updated != content && !writeAll(paths.config, updated, error))
                return false;
        }
        if (!QFile::remove(paths.backup))
            return fail(error, QStringLiteral("cannot remove %1").arg(paths.backup));
        return true;
    }

    if (!QFile::exists(paths.config))
        return true;
    QByteArray content;
    if (!readAll(paths.config, &content, error))
        return false;
    const QByteArray updated = editedConfig(content, false);
    if (updated != content && !writeAll(paths.config, updated, error))
        return false;
    return true;
}

QString PortalConfig::status()
{
    const Paths paths = portalPaths();
    QByteArray content;
    if (!QFile::exists(paths.config))
        return QStringLiteral("filechooser: disabled");
    if (!readAll(paths.config, &content, nullptr))
        return QStringLiteral("filechooser: disabled");
    return fileChooserEnabled(content) ? QStringLiteral("filechooser: enabled")
                                       : QStringLiteral("filechooser: disabled");
}
