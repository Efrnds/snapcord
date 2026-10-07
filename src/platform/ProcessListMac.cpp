#include "platform/ProcessList.h"

#include <libproc.h>

#include <vector>

namespace ProcessList {

QList<Process> list()
{
    QList<Process> processes;
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0)
        return processes;
    std::vector<pid_t> pids(size_t(count) + 32);
    const int found = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    for (int i = 0; i < found; ++i) {
        const QString file = path(pids[size_t(i)]);
        if (!file.isEmpty())
            processes.append({pids[size_t(i)], file.mid(file.lastIndexOf(u'/') + 1)});
    }
    return processes;
}

QString path(qint64 pid)
{
    char buffer[PROC_PIDPATHINFO_MAXSIZE];
    const int size = proc_pidpath(pid_t(pid), buffer, sizeof(buffer));
    if (size <= 0)
        return {};
    return QString::fromUtf8(buffer, size).toLower();
}

QString commandLine(qint64)
{
    // Only needed for a handful of games that share an executable; not worth reading KERN_PROCARGS2 for.
    return {};
}

} // namespace ProcessList
