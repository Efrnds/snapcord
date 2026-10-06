#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QWebSocket>

// WebSocket signaling connection to a Discord voice server (voice gateway v8).
class VoiceGateway : public QObject
{
    Q_OBJECT

public:
    struct Credentials
    {
        QString endpoint;
        QString serverId; // guild ID (or channel ID for private calls)
        QString userId;
        QString sessionId;
        QString token;
    };

    explicit VoiceGateway(QObject* parent = nullptr);

    void open(const Credentials& credentials, int maxDaveVersion);
    void close();

    void selectProtocol(const QString& address, quint16 port, const QString& mode);
    void sendSpeaking(int flags, quint32 ssrc);
    void sendJson(int op, const QJsonObject& data);
    void sendBinary(int op, const QByteArray& payload);

signals:
    void ready(quint32 ssrc, const QString& ip, quint16 port, const QStringList& modes);
    void sessionDescription(const QString& mode, const QByteArray& secretKey, int daveProtocolVersion);
    void resumed();
    void speaking(const QString& userId, quint32 ssrc, int flags);
    void clientsConnected(const QStringList& userIds);
    void clientDisconnected(const QString& userId);
    void daveJson(int op, const QJsonObject& data);
    void daveBinary(int op, const QByteArray& payload);
    void pingChanged(int milliseconds);
    // The connection is gone for good; `code` is the WebSocket close code.
    void closed(int code);

private:
    void onTextMessage(const QString& message);
    void onBinaryMessage(const QByteArray& message);
    void onDisconnected();
    void sendHeartbeat();
    void connectSocket();

    QWebSocket m_socket;
    QTimer m_heartbeatTimer;
    QElapsedTimer m_heartbeatSentAt;
    Credentials m_credentials;
    int m_maxDaveVersion = 0;
    int m_lastSequence = -1;
    bool m_resuming = false;
    bool m_open = false;
    int m_resumeAttempts = 0;
};
