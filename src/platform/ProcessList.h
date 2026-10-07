#pragma once

#include <QList>
#include <QString>

// Running processes, for game detection. Names, paths and command lines are lowercase, with '/' as the path
// separator on every system. Anything that cannot be read (another user's process...) comes back empty.
namespace ProcessList {

struct Process
{
    qint64 pid = 0;
    QString name; // executable file name, e.g. "valorant.exe"
};

QList<Process> list();
// Full path of the executable.
QString path(qint64 pid);
QString commandLine(qint64 pid);

} // namespace ProcessList
