#include "platform/ProcessList.h"

#include <QDir>
#include <QFile>

#include <unistd.h>

namespace {

QString normalize(QString text)
{
    return text.replace(u'\\', u'/').toLower();
}

QByteArray readCommandLine(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(64 * 1024);
}

// Games under Wine and Proton run as the Wine loader; the Windows executable is the first argument instead.
QString executable(qint64 pid)
{
    const QByteArray arguments = readCommandLine(pid);
    const QString first = normalize(QString::fromUtf8(arguments.left(arguments.indexOf('\0'))));
    if (first.endsWith(u".exe"))
        return first;
    char buffer[4096];
    const QByteArray link = QStringLiteral("/proc/%1/exe").arg(pid).toLocal8Bit();
    const ssize_t size = readlink(link.constData(), buffer, sizeof(buffer));
    if (size <= 0)
        return {};
    return normalize(QString::fromLocal8Bit(buffer, qsizetype(size)));
}

} // namespace

namespace ProcessList {

QList<Process> list()
{
    QList<Process> processes;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.toLongLong(&numeric);
        if (!numeric)
            continue;
        const QString file = executable(pid);
        if (!file.isEmpty())
            processes.append({pid, file.mid(file.lastIndexOf(u'/') + 1)});
    }
    return processes;
}

QString path(qint64 pid)
{
    return executable(pid);
}

QString commandLine(qint64 pid)
{
    QByteArray arguments = readCommandLine(pid);
    arguments.replace('\0', ' ');
    return normalize(QString::fromUtf8(arguments));
}

} // namespace ProcessList
