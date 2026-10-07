#pragma once

#include "core/OrderedJson.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

class QNetworkRequest;

// Client identification sent to Discord. Snapcord presents itself like the official web client running in Chrome,
// since unusual client properties are a common reason for anti-abuse systems to flag third-party clients. Field
// names, values and their order follow what the web client sends.
namespace ClientProperties {

// Refreshes the build number of the current web client (cached for a day), then calls `done`. `done` only waits
// for the download when no build number was ever fetched; otherwise it runs right away and the refresh continues
// in the background. Called before logging in or connecting.
void ensureBuildNumber(std::function<void()> done);

// Properties sent in the Gateway IDENTIFY payload.
OrderedJson identifyProperties();

// Base64-encoded properties for the X-Super-Properties HTTP header.
QByteArray superPropertiesHeader();

QByteArray userAgent();

QString systemLocale();

// Language of the Discord account (user settings), sent as X-Discord-Locale. Defaults to the system locale.
void setDiscordLocale(const QString& locale);

// Origin of the web client, which browsers send when opening its WebSockets.
QString origin();

// Sets the headers the web client sends with its API requests. `referer` is the page the request comes from.
void applyApiHeaders(QNetworkRequest& request, const QByteArray& verb,
                     const QString& referer = QStringLiteral("https://discord.com/channels/@me"));

// Whether the window is focused and whether a call is running. They decide client_app_state and keep the
// analytics heartbeat session alive, like in the official client.
void setActivity(bool focused, bool rtcConnected);

struct HeartbeatSession
{
    QString id;
    qint64 createdAtMs = 0;
};

enum class HeartbeatSessionUpdate { Unchanged, Touched, Created };

// Renews the analytics heartbeat session while the app is in use. A new one starts after 30 minutes of inactivity.
HeartbeatSessionUpdate touchHeartbeatSession();
std::optional<HeartbeatSession> heartbeatSession();

QString clientLaunchId();

} // namespace ClientProperties
