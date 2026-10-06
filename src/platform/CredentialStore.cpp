#include "platform/CredentialStore.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

namespace {

const wchar_t* const TargetName = L"Snapcord/token";

} // namespace

namespace CredentialStore {

bool isPersistent()
{
    return true;
}

QString loadToken()
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(TargetName, CRED_TYPE_GENERIC, 0, &credential))
        return {};
    const QString token = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                            static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return token;
}

bool saveToken(const QString& token)
{
    QByteArray blob = token.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(TargetName);
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"Snapcord");
    return CredWriteW(&credential, 0) != FALSE;
}

void clearToken()
{
    CredDeleteW(TargetName, CRED_TYPE_GENERIC, 0);
}

} // namespace CredentialStore

#else

// macOS Keychain and Linux Secret Service support arrive with the multi-platform release (Phase 4).
namespace CredentialStore {

bool isPersistent()
{
    return false;
}

QString loadToken()
{
    return {};
}

bool saveToken(const QString&)
{
    return false;
}

void clearToken() {}

} // namespace CredentialStore

#endif
