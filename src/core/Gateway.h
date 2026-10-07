#pragma once

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
    // Opcode 3: this session's status ("online", "idle", "dnd", "invisible") and activities.
    void updatePresence(const QString& status, const QJsonArray& activities);

    QString sessionId() const { return m_sessionId; }

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
    void sendHeartbeat();
    void identify();
    void resume();
    void reconnect(bool canResume, int delayMs);

    QWebSocket m_socket;
    QTimer m_heartbeatTimer;
    ZlibStream m_zlib;
    QString m_token;
    QString m_sessionId;
    QString m_resumeUrl;
    std::optional<int> m_sequence;
    bool m_heartbeatAcked = true;
    bool m_resuming = false; // the next connection resumes the session instead of identifying again
    bool m_running = false;
    int m_failedAttempts = 0;
};
