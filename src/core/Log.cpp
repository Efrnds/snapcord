#include "core/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcGateway, "snapcord.gateway")
Q_LOGGING_CATEGORY(lcVoice, "snapcord.voice")

namespace {

QFile* logFile = nullptr;
QMutex logMutex;
QtMessageHandler previousHandler = nullptr;

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    static const char* const levels[] = {"debug", "warning", "critical", "fatal", "info"};
    const QString line = QStringLiteral("%1 [%2] %3: %4\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  QLatin1String(levels[type]),
                                  QLatin1String(context.category ? context.category : "default"), message);
    {
        QMutexLocker lock(&logMutex);
        if (logFile) {
            logFile->write(line.toUtf8());
            logFile->flush();
        }
    }
    if (previousHandler)
        previousHandler(type, context, message);
}

} // namespace

namespace Log {

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/snapcord.log");
}

void installFileHandler()
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString oldPath = QFileInfo(path).absolutePath() + QStringLiteral("/snapcord.old.log");
    QFile::remove(oldPath);
    QFile::rename(path, oldPath);

    logFile = new QFile(path);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete logFile;
        logFile = nullptr;
        return;
    }
    // Snapcord's own categories log at info level; Qt's internals only from warnings up.
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\nsnapcord.*.info=true"));
    previousHandler = qInstallMessageHandler(writeMessage);
}

} // namespace Log
