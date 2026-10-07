#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>

#include <functional>

class QNetworkAccessManager;
class RestClient;

// "Listening to Spotify", like the official client: with the Spotify account connected to Discord, its access
// token (from Discord) opens Spotify's push connection ("dealer"), which tells whenever playback changes.
// Without push notifications, it falls back to asking Spotify every few seconds.
class SpotifyPresence : public QObject
{
    Q_OBJECT

public:
    SpotifyPresence(RestClient* discord, QObject* parent = nullptr);

    // `userId` is the Discord user, which names the listening party.
    void start(const QString& userId);
    void stop();
    // The account's connections changed (Spotify connected, removed, or "display on profile" toggled).
    void refreshConnection();

signals:
    // The song being played, as a presence activity; empty when nothing plays.
    void activityChanged(const QJsonObject& activity);

private:
    using SpotifyCallback = std::function<void(int status, const QJsonObject& body)>;

    void reset();
    void fetchToken();
    void openDealer();
    void onDealerMessage(const QString& message);
    void onDealerClosed();
    void fetchPlayer();
    void applyState(const QJsonObject& state);
    void setActivity(const QJsonObject& activity);
    void spotifyRequest(const QByteArray& verb, const QString& path, SpotifyCallback callback, bool retried = false);
    void retryLater();

    RestClient* m_discord;
    QNetworkAccessManager* m_network;
    QWebSocket m_dealer;
    QTimer m_pingTimer;
    QTimer m_retryTimer;
    QTimer m_pollTimer;    // only when push notifications are not available
    QTimer m_trackEndTimer; // checks again when the song should be over, in case an event was missed
    bool m_running = false;
    QString m_userId;
    QString m_accountId; // Spotify account ID, from the Discord connection
    QString m_token;
    int m_failures = 0;
    QJsonObject m_activity;
};
