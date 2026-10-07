#include "core/SpotifyPresence.h"

#include "core/ClientProperties.h"
#include "core/Log.h"
#include "core/RestClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstdlib>

namespace {

constexpr auto DealerUrl = "wss://dealer.spotify.com/";
constexpr auto ApiBase = "https://api.spotify.com/v1";
constexpr int PingIntervalMs = 30 * 1000;
constexpr int PollIntervalMs = 15 * 1000;
constexpr int MaxRetryDelayMs = 5 * 60 * 1000;
constexpr int ActivityFlags = (1 << 4) | (1 << 5); // SYNC | PLAY, like the official client
constexpr qsizetype MaxTextLength = 128;            // longer activity texts are rejected

QString shortened(const QString& text)
{
    return text.size() > MaxTextLength ? text.left(MaxTextLength - 1) + QChar(0x2026) : text;
}

// Activity images point at Spotify's CDN as "spotify:<image id>" (https://i.scdn.co/image/<image id>).
QString imageAsset(const QJsonArray& images)
{
    const QString url = images.first().toObject().value(u"url").toString();
    const qsizetype slash = url.lastIndexOf(u'/');
    if (!url.contains(u"scdn.co/image/") || slash < 0)
        return {};
    return QStringLiteral("spotify:") + url.mid(slash + 1);
}

} // namespace

SpotifyPresence::SpotifyPresence(RestClient* discord, QObject* parent)
    : QObject(parent)
    , m_discord(discord)
    , m_network(new QNetworkAccessManager(this))
{
    m_pingTimer.setInterval(PingIntervalMs);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] { m_dealer.sendTextMessage(QStringLiteral("{\"type\":\"ping\"}")); });
    m_pollTimer.setInterval(PollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &SpotifyPresence::fetchPlayer);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] { fetchToken(); });
    m_trackEndTimer.setSingleShot(true);
    connect(&m_trackEndTimer, &QTimer::timeout, this, &SpotifyPresence::fetchPlayer);

    connect(&m_dealer, &QWebSocket::connected, this, [this] {
        qCInfo(lcGateway) << "spotify: connected";
        m_failures = 0;
        m_pingTimer.start();
    });
    connect(&m_dealer, &QWebSocket::textMessageReceived, this, &SpotifyPresence::onDealerMessage);
    connect(&m_dealer, &QWebSocket::disconnected, this, &SpotifyPresence::onDealerClosed);
}

void SpotifyPresence::start(const QString& userId)
{
    m_userId = userId;
    m_running = true;
    refreshConnection();
}

void SpotifyPresence::stop()
{
    m_running = false;
    reset();
}

void SpotifyPresence::reset()
{
    m_accountId.clear();
    m_token.clear();
    m_retryTimer.stop();
    m_pollTimer.stop();
    m_pingTimer.stop();
    m_trackEndTimer.stop();
    m_dealer.close();
    setActivity({});
}

void SpotifyPresence::refreshConnection()
{
    if (!m_running)
        return;
    QPointer<SpotifyPresence> self(this);
    m_discord->get(QStringLiteral("/users/@me/connections"), [self](const RestClient::Response& response) {
        if (!self || !self->m_running || !response.ok())
            return;
        QString accountId;
        for (const QJsonValue& value : response.body.array()) {
            const QJsonObject connection = value.toObject();
            // "Display Spotify as your status" is the connection's show_activity switch.
            if (connection.value(u"type").toString() == u"spotify" && !connection.value(u"revoked").toBool()
                && connection.value(u"show_activity").toBool(true)) {
                accountId = connection.value(u"id").toString();
                break;
            }
        }
        if (accountId == self->m_accountId)
            return;
        if (accountId.isEmpty()) {
            qCInfo(lcGateway) << "spotify: no connected account shown as status";
            self->reset();
            return;
        }
        self->reset();
        self->m_accountId = accountId;
        self->fetchToken();
    });
}

void SpotifyPresence::fetchToken()
{
    if (!m_running || m_accountId.isEmpty())
        return;
    QPointer<SpotifyPresence> self(this);
    const QString path = QStringLiteral("/users/@me/connections/spotify/%1/access-token").arg(m_accountId);
    m_discord->get(path, [self](const RestClient::Response& response) {
        if (!self || !self->m_running)
            return;
        const QString token = response.body.object().value(u"access_token").toString();
        if (!response.ok() || token.isEmpty()) {
            qCWarning(lcGateway) << "spotify: no access token, status" << response.status;
            // The connection was removed or needs to be renewed in the official client; nothing to retry.
            if (response.status >= 400 && response.status < 500 && response.status != 429)
                return;
            self->retryLater();
            return;
        }
        self->m_token = token;
        if (self->m_dealer.state() == QAbstractSocket::UnconnectedState)
            self->openDealer();
    });
}

void SpotifyPresence::openDealer()
{
    QUrl url{QLatin1String(DealerUrl)};
    url.setQuery(QUrlQuery{{QStringLiteral("access_token"), m_token}});
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    m_dealer.open(request);
}

void SpotifyPresence::onDealerClosed()
{
    m_pingTimer.stop();
    if (!m_running || m_accountId.isEmpty())
        return;
    qCInfo(lcGateway) << "spotify: disconnected, code" << m_dealer.closeCode();
    retryLater();
}

void SpotifyPresence::retryLater()
{
    if (m_retryTimer.isActive())
        return;
    const int delay = std::min(MaxRetryDelayMs, 5000 << std::min(m_failures, 6));
    ++m_failures;
    // While the push connection is down, keep the song up to date by asking.
    if (m_failures >= 3 && !m_token.isEmpty() && !m_pollTimer.isActive())
        m_pollTimer.start();
    m_retryTimer.start(delay);
}

void SpotifyPresence::onDealerMessage(const QString& message)
{
    const QJsonObject json = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString connectionId = json.value(u"headers").toObject().value(u"Spotify-Connection-Id").toString();
    if (!connectionId.isEmpty()) {
        // The first message names this connection; asking for player notifications on it starts the events.
        QPointer<SpotifyPresence> self(this);
        const QString path = QStringLiteral("/me/notifications/player?connection_id=")
                             + QString::fromLatin1(QUrl::toPercentEncoding(connectionId));
        spotifyRequest("PUT", path, [self](int status, const QJsonObject&) {
            if (!self)
                return;
            if (status >= 200 && status < 300) {
                self->m_pollTimer.stop();
            } else {
                qCWarning(lcGateway) << "spotify: player notifications unavailable, status" << status << "- polling instead";
                self->m_pollTimer.start();
            }
            self->fetchPlayer();
        });
        return;
    }
    if (json.value(u"type").toString() != u"message")
        return;
    for (const QJsonValue& payloadValue : json.value(u"payloads").toArray()) {
        // Payloads are usually objects, but may come as JSON text.
        const QJsonObject payload = payloadValue.isString()
                                        ? QJsonDocument::fromJson(payloadValue.toString().toUtf8()).object()
                                        : payloadValue.toObject();
        for (const QJsonValue& eventValue : payload.value(u"events").toArray()) {
            const QJsonObject event = eventValue.toObject();
            const QString type = event.value(u"type").toString();
            const QJsonObject state = event.value(u"event").toObject().value(u"state").toObject();
            if (type == u"PLAYER_STATE_CHANGED" && state.contains(u"item"))
                applyState(state);
            else if (type == u"PLAYER_STATE_CHANGED" || type == u"DEVICE_STATE_CHANGED")
                fetchPlayer();
        }
    }
}

void SpotifyPresence::fetchPlayer()
{
    if (!m_running || m_token.isEmpty())
        return;
    QPointer<SpotifyPresence> self(this);
    spotifyRequest("GET", QStringLiteral("/me/player?additional_types=episode"), [self](int status, const QJsonObject& body) {
        if (!self)
            return;
        if (status == 204)
            self->applyState({}); // no active device
        else if (status == 200)
            self->applyState(body);
    });
}

void SpotifyPresence::applyState(const QJsonObject& state)
{
    const QJsonObject item = state.value(u"item").toObject();
    const bool privateSession = state.value(u"device").toObject().value(u"is_private_session").toBool();
    if (!state.value(u"is_playing").toBool() || item.isEmpty() || privateSession) {
        m_trackEndTimer.stop();
        setActivity({});
        return;
    }

    const bool episode = item.value(u"type").toString() == u"episode";
    QString artists;
    QString image;
    QString largeText;
    QJsonObject metadata{{QStringLiteral("type"), episode ? QStringLiteral("episode") : QStringLiteral("track")}};
    if (episode) {
        const QJsonObject show = item.value(u"show").toObject();
        artists = show.value(u"name").toString();
        largeText = artists;
        image = imageAsset(item.value(u"images").toArray());
        if (image.isEmpty())
            image = imageAsset(show.value(u"images").toArray());
    } else {
        QStringList names;
        QJsonArray artistIds;
        for (const QJsonValue& value : item.value(u"artists").toArray()) {
            names.append(value.toObject().value(u"name").toString());
            artistIds.append(value.toObject().value(u"id").toString());
        }
        artists = names.join(QStringLiteral("; "));
        const QJsonObject album = item.value(u"album").toObject();
        largeText = album.value(u"name").toString();
        image = imageAsset(album.value(u"images").toArray());
        metadata.insert(QStringLiteral("album_id"), album.value(u"id").toString());
        metadata.insert(QStringLiteral("artist_ids"), artistIds);
    }
    const QString contextUri = state.value(u"context").toObject().value(u"uri").toString();
    if (!contextUri.isEmpty())
        metadata.insert(QStringLiteral("context_uri"), contextUri);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = now - state.value(u"progress_ms").toInteger();
    const qint64 end = start + item.value(u"duration_ms").toInteger();

    QJsonObject assets;
    if (!image.isEmpty())
        assets.insert(QStringLiteral("large_image"), image);
    if (!largeText.isEmpty())
        assets.insert(QStringLiteral("large_text"), shortened(largeText));
    QJsonObject activity{
        {QStringLiteral("type"), 2},
        {QStringLiteral("name"), QStringLiteral("Spotify")},
        {QStringLiteral("flags"), ActivityFlags},
        {QStringLiteral("details"), shortened(item.value(u"name").toString())},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}}},
        {QStringLiteral("party"), QJsonObject{{QStringLiteral("id"), QStringLiteral("spotify:") + m_userId}}},
        {QStringLiteral("sync_id"), item.value(u"id").toString()},
        {QStringLiteral("metadata"), metadata},
    };
    if (!artists.isEmpty())
        activity.insert(QStringLiteral("state"), shortened(artists));
    if (!assets.isEmpty())
        activity.insert(QStringLiteral("assets"), assets);
    setActivity(activity);

    // Ask again once the song should be over, in case the next song's event never comes.
    if (end > now)
        m_trackEndTimer.start(int(std::min<qint64>(end - now + 3000, 60 * 60 * 1000)));
}

void SpotifyPresence::setActivity(const QJsonObject& activity)
{
    // The same song at the same position (give or take the network delay) is not worth a presence update.
    auto start = [](const QJsonObject& json) {
        return json.value(u"timestamps").toObject().value(u"start").toInteger();
    };
    if (activity.isEmpty() == m_activity.isEmpty() && activity.value(u"sync_id") == m_activity.value(u"sync_id")
        && std::abs(start(activity) - start(m_activity)) < 2000)
        return;
    m_activity = activity;
    if (activity.isEmpty())
        qCInfo(lcGateway) << "spotify: nothing playing";
    else
        qCInfo(lcGateway) << "spotify: playing a song";
    emit activityChanged(activity);
}

void SpotifyPresence::spotifyRequest(const QByteArray& verb, const QString& path, SpotifyCallback callback, bool retried)
{
    QNetworkRequest request(QUrl(QLatin1String(ApiBase) + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    if (verb == "PUT")
        request.setHeader(QNetworkRequest::ContentLengthHeader, 0);
    QNetworkReply* reply = m_network->sendCustomRequest(request, verb, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, verb, path, callback = std::move(callback), retried] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 && !retried) {
            // Spotify tokens last an hour; Discord hands out a fresh one.
            QPointer<SpotifyPresence> self(this);
            const QString tokenPath = QStringLiteral("/users/@me/connections/spotify/%1/access-token").arg(m_accountId);
            m_discord->get(tokenPath, [self, verb, path, callback](const RestClient::Response& response) {
                const QString token = response.body.object().value(u"access_token").toString();
                if (!self || !self->m_running || token.isEmpty())
                    return;
                self->m_token = token;
                self->spotifyRequest(verb, path, callback, true);
            });
            return;
        }
        callback(status, QJsonDocument::fromJson(reply->readAll()).object());
    });
}
