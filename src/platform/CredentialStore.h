#pragma once

#include <QString>

// Stores the account token in the operating system's credential vault: Windows Credential Manager, the macOS
// Keychain or the Linux Secret Service. If the vault is unavailable, the user logs in on every start.
namespace CredentialStore {

bool isPersistent();
QString loadToken();
bool saveToken(const QString& token);
void clearToken();

} // namespace CredentialStore
