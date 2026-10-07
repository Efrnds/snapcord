#include "platform/ProcessList.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <QByteArray>
#include <iterator>

namespace {

QString normalize(QString text)
{
    return text.replace(u'\\', u'/').toLower();
}

// Process handle that closes itself.
struct ProcessHandle
{
    explicit ProcessHandle(qint64 pid)
        : handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid)))
    {
    }
    ~ProcessHandle()
    {
        if (handle)
            CloseHandle(handle);
    }
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    HANDLE handle;
};

} // namespace

namespace ProcessList {

QList<Process> list()
{
    QList<Process> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        processes.append({qint64(entry.th32ProcessID), QString::fromWCharArray(entry.szExeFile).toLower()});
    CloseHandle(snapshot);
    return processes;
}

QString path(qint64 pid)
{
    const ProcessHandle process(pid);
    if (!process.handle)
        return {};
    wchar_t buffer[4096];
    DWORD size = DWORD(std::size(buffer));
    if (!QueryFullProcessImageNameW(process.handle, 0, buffer, &size))
        return {};
    return normalize(QString::fromWCharArray(buffer, int(size)));
}

QString commandLine(qint64 pid)
{
    // ProcessCommandLineInformation (Windows 8.1+) reads it without touching the other process's memory.
    using QueryInformation = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<QueryInformation>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    constexpr ULONG ProcessCommandLineInformation = 60;
    if (!query)
        return {};
    const ProcessHandle process(pid);
    if (!process.handle)
        return {};
    ULONG length = 0;
    query(process.handle, ProcessCommandLineInformation, nullptr, 0, &length);
    if (length < sizeof(UNICODE_STRING) || length > 1024 * 1024)
        return {};
    QByteArray buffer(qsizetype(length), Qt::Uninitialized);
    if (query(process.handle, ProcessCommandLineInformation, buffer.data(), length, &length) < 0)
        return {};
    const auto* text = reinterpret_cast<const UNICODE_STRING*>(buffer.constData());
    return normalize(QString::fromWCharArray(text->Buffer, text->Length / int(sizeof(wchar_t))));
}

} // namespace ProcessList
