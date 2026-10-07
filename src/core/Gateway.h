#pragma once

#include "core/OrderedJson.h"
#include "core/ZlibStream.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>

#include <optional>

// Connection to the main Discord Gateway: heartbeating, identify/resume, reconnection and event dispatch.
class Gateway : public QObject
{
    Q_OBJECT

public:
    explicit Gateway(QObject* parent = nullptr);

    void start(const QString& token);
    void stop();

    void send(int op, const QJsonValue& data);

    // Opcode 4: joins, moves between or leaves (empty channelId) voice channels.
    void updateVoiceState(const QString& guildId, const QString& channelId, bool selfMute, bool selfDeaf);
    // Opcode 8: asks for member objects of specific users (e.g. people in voice channels we don't know yet).
    // With `presences`, the reply also carries their status and activities.
    void requestGuildMembers(const QString& guildId, const QStringList& userIds, bool presences = false);
    // Opcode 8 with a query: up to `limit` members whose username or nickname starts with `query`.
    void searchGuildMembers(const QString& guildId, const QString& query, int limit);
    // Opcode 3: this session's status ("online", "idle", "dnd", "invisible") and activities.
    void updatePresence(const QString& status, const QJsonArray& activities);
    // Opcode 37: what this session follows in a guild (typing, activities, member list rows of a channel).
    void updateGuildSubscriptions(const QString& guildId, const QJsonObject& subscription);

    QString sessionId() const { return m_sessionId; }

    // Whether the window is focused and a call is running. Like the official client, this is reported in the
    // heartbeats and keeps the analytics heartbeat session alive.
    void setActiveState(bool focused, bool rtcConnected);

signals:
    void dispatch(const QString& event, const QJsonObject& data);
    void authenticationFailed();
    void connectionStateChanged(bool connected);

private:
    void openSocket(const QString& baseUrl);
    void onBinaryMessage(const QByteArray& message);
    void onTextMessage(const QString& message);
    void handlePayload(const QJsonObject& payload);
    void onDisconnected();
    void sendOrdered(int op, const OrderedJson& data);
    void sendHeartbeat();
    void sendQosHeartbeat();
    void syncHeartbeatSession();
    void sendTimeSpentSessionId();
    void identify();
    void resume();
    void reconnect(bool canResume, int delayMs);

    QWebSocket m_socket;
    QTimer m_heartbeatTimer;
    QTimer m_heartbeatSessionTimer;
    ZlibStream m_zlib;
    QString m_token;
    QString m_sessionId;
    QString m_resumeUrl;
    std::optional<int> m_sequence;
    bool m_heartbeatAcked = true;
    bool m_resuming = false; // the next connection resumes the session instead of identifying again
    bool m_running = false;
    bool m_ready = false; // READY or RESUMED received on the current connection

    // Quality-of-service state sent with each heartbeat. A change reaches Discord one heartbeat later, except
    // becoming active, which is merged in and sent right away.
    struct Qos
    {
        bool active = false;
        QStringList reasons;
    };
    std::optional<Qos> m_qos;
    std::optional<Qos> m_upcomingQos;
    int m_failedAttempts = 0;
};
