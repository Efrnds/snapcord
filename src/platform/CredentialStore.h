#pragma once

#include <QString>

// Stores the account token in the operating system's credential vault (Windows Credential Manager).
// On platforms without an implementation yet, nothing is persisted and the user logs in on every start.
namespace CredentialStore {

bool isPersistent();
QString loadToken();
bool saveToken(const QString& token);
void clearToken();

} // namespace CredentialStore
