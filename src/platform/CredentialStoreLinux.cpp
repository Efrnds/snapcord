#include "platform/CredentialStore.h"

// libsecret uses "signals" as an identifier, which clashes with Qt's keyword macros.
#undef signals
#include <libsecret/secret.h>

// The Secret Service (GNOME Keyring, KDE Wallet, KeePassXC...) through libsecret. Inside Flatpak, access goes
// through the org.freedesktop.secrets bus name allowed in the manifest.
namespace {

const SecretSchema* schema()
{
    static const SecretSchema definition = {
        "io.github.pedrordgsr.Snapcord.Token",
        SECRET_SCHEMA_NONE,
        {
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    return &definition;
}

bool serviceAvailable = true;

} // namespace

namespace CredentialStore {

bool isPersistent()
{
    return serviceAvailable;
}

QString loadToken()
{
    GError* error = nullptr;
    gchar* password = secret_password_lookup_sync(schema(), nullptr, &error, "account", "token", nullptr);
    if (error) {
        // No keyring daemon running: the session works, but the login won't be remembered.
        serviceAvailable = false;
        g_error_free(error);
        return {};
    }
    if (!password)
        return {};
    const QString token = QString::fromUtf8(password);
    secret_password_free(password);
    return token;
}

bool saveToken(const QString& token)
{
    GError* error = nullptr;
    const QByteArray bytes = token.toUtf8();
    const gboolean stored = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "Snapcord", bytes.constData(),
                                                       nullptr, &error, "account", "token", nullptr);
    if (error) {
        serviceAvailable = false;
        g_error_free(error);
        return false;
    }
    return stored;
}

void clearToken()
{
    GError* error = nullptr;
    secret_password_clear_sync(schema(), nullptr, &error, "account", "token", nullptr);
    if (error)
        g_error_free(error);
}

} // namespace CredentialStore
