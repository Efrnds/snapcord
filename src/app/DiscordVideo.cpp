#include "DiscordVideo.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QWidget>

namespace {

bool discordHost(const QString& host)
{
    const QString name = host.toLower();
    return name == u"cdn.discordapp.com" || name.endsWith(u".cdn.discordapp.com")
        || name == u"media.discordapp.net" || name.endsWith(u".media.discordapp.net");
}

QString playerExecutable()
{
    const QDir dir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    return dir.filePath(QStringLiteral("snapcord-video.exe"));
#else
    return dir.filePath(QStringLiteral("snapcord-video"));
#endif
}

} // namespace

bool DiscordVideo::isDiscordFile(const QUrl& url)
{
    if (!url.isValid() || !discordHost(url.host()))
        return false;
    const QString path = url.path().toLower();
    return path.endsWith(u".mp4") || path.endsWith(u".webm");
}

void DiscordVideo::open(const QUrl& url, QWidget* parent)
{
    if (!isDiscordFile(url))
        return;

    const QString player = playerExecutable();
    if (!QFileInfo::exists(player)) {
        QDesktopServices::openUrl(url);
        return;
    }

    QStringList arguments;
    arguments << url.toString(QUrl::FullyEncoded);
    if (parent) {
        const QRect frame(parent->mapToGlobal(QPoint(0, 0)), parent->size());
        arguments << QString::number(frame.x()) << QString::number(frame.y())
                  << QString::number(frame.width()) << QString::number(frame.height());
    }

    QProcess process;
    process.start(player, arguments);
    if (!process.waitForStarted(3000)) {
        QDesktopServices::openUrl(url);
        return;
    }

    if (parent)
        parent->setEnabled(false);
    QEventLoop loop;
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (parent)
        parent->setEnabled(true);
}
