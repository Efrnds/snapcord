#include "core/Gateway.h"

#include "core/ClientProperties.h"
#include "core/Log.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>

namespace {

constexpr auto DefaultGatewayUrl = "wss://gateway.discord.gg";
constexpr auto GatewayQuery = "/?encoding=json&v=9&compress=zlib-stream";

// LAZY_USER_NOTES | NO_AFFINE_USER_IDS: skip data Snapcord does not use, keep the classic READY layout.
// AUTO_CALL_CONNECT: receive CALL_CREATE for private calls that were already running when we connected.
constexpr int Capabilities = (1 << 0) | (1 << 1) | (1 << 12);

enum Opcode {
    Dispatch = 0,
    Heartbeat = 1,
    Identify = 2,
    PresenceUpdate = 3,
    VoiceStateUpdate = 4,
    Resume = 6,
    Reconnect = 7,
    RequestGuildMembers = 8,
    InvalidSession = 9,
    Hello = 10,
    HeartbeatAck = 11,
    GuildSubscriptionsBulk = 37,
};

bool isFatalCloseCode(int code)
{
    // 4004: authentication failed; 4010-4014: invalid shard/intents, which would never succeed on retry.
    return code == 4004 || (code >= 4010 && code <= 4014);
}

} // namespace

Gateway::Gateway(QObject* parent)
    : QObject(parent)
{
    m_heartbeatTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &Gateway::sendHeartbeat);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &Gateway::onBinaryMessage);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &Gateway::onTextMessage);
    connect(&m_socket, &QWebSocket::disconnected, this, &Gateway::onDisconnected);
}

void Gateway::start(const QString& token)
{
    m_token = token;
    m_running = true;
    m_sessionId.clear();
    m_sequence.reset();
    m_resuming = false;
    openSocket(QLatin1String(DefaultGatewayUrl));
}

void Gateway::stop()
{
    m_running = false;
    m_heartbeatTimer.stop();
    m_socket.close();
}

void Gateway::openSocket(const QString& baseUrl)
{
    m_zlib.reset();
    m_heartbeatAcked = true;
    QNetworkRequest request(QUrl(baseUrl + QLatin1String(GatewayQuery)));
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    m_socket.open(request);
}

void Gateway::send(int op, const QJsonValue& data)
{
    const QJsonObject payload{{QStringLiteral("op"), op}, {QStringLiteral("d"), data}};
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Gateway::updateVoiceState(const QString& guildId, const QString& channelId, bool selfMute, bool selfDeaf)
{
    send(VoiceStateUpdate, QJsonObject{
                               {QStringLiteral("guild_id"), guildId.isEmpty() ? QJsonValue() : QJsonValue(guildId)},
                               {QStringLiteral("channel_id"), channelId.isEmpty() ? QJsonValue() : QJsonValue(channelId)},
                               {QStringLiteral("self_mute"), selfMute},
                               {QStringLiteral("self_deaf"), selfDeaf},
                               {QStringLiteral("self_video"), false},
                           });
}

void Gateway::requestGuildMembers(const QString& guildId, const QStringList& userIds, bool presences)
{
    QJsonObject request{
        {QStringLiteral("guild_id"), QJsonArray{guildId}},
        {QStringLiteral("user_ids"), QJsonArray::fromStringList(userIds)},
    };
    if (presences)
        request.insert(QStringLiteral("presences"), true);
    send(RequestGuildMembers, request);
}

void Gateway::searchGuildMembers(const QString& guildId, const QString& query, int limit)
{
    send(RequestGuildMembers, QJsonObject{
                                  {QStringLiteral("guild_id"), QJsonArray{guildId}},
                                  {QStringLiteral("query"), query},
                                  {QStringLiteral("limit"), limit},
                                  {QStringLiteral("presences"), true},
                              });
}

void Gateway::updatePresence(const QString& status, const QJsonArray& activities)
{
    send(PresenceUpdate, QJsonObject{
                             {QStringLiteral("status"), status},
                             {QStringLiteral("since"), 0},
                             {QStringLiteral("activities"), activities},
                             {QStringLiteral("afk"), false},
                         });
}

void Gateway::updateGuildSubscriptions(const QString& guildId, const QJsonObject& subscription)
{
    send(GuildSubscriptionsBulk, QJsonObject{{QStringLiteral("subscriptions"), QJsonObject{{guildId, subscription}}}});
}

void Gateway::onBinaryMessage(const QByteArray& message)
{
    const auto decompressed = m_zlib.feed(message);
    if (!decompressed)
        return;
    if (decompressed->isEmpty()) {
        // Corrupted compression stream; the only way out is a fresh connection.
        m_socket.abort();
        return;
    }
    handlePayload(QJsonDocument::fromJson(*decompressed).object());
}

void Gateway::onTextMessage(const QString& message)
{
    handlePayload(QJsonDocument::fromJson(message.toUtf8()).object());
}

void Gateway::handlePayload(const QJsonObject& payload)
{
    const int op = payload.value(u"op").toInt(-1);
    const QJsonValue data = payload.value(u"d");
    if (payload.value(u"s").isDouble())
        m_sequence = payload.value(u"s").toInt();

    switch (op) {
    case Hello: {
        const int interval = data.toObject().value(u"heartbeat_interval").toInt(41250);
        m_heartbeatTimer.start(interval);
        // The first heartbeat is jittered so that reconnecting clients don't all beat at once.
        QTimer::singleShot(static_cast<int>(interval * QRandomGenerator::global()->generateDouble()), this, [this] {
            if (m_socket.state() == QAbstractSocket::ConnectedState)
                sendHeartbeat();
        });
        if (m_resuming && !m_sessionId.isEmpty()) {
            qCInfo(lcGateway) << "resuming session";
            resume();
        } else {
            qCInfo(lcGateway) << "identifying";
            identify();
        }
        break;
    }
    case Heartbeat:
        sendHeartbeat();
        break;
    case HeartbeatAck:
        m_heartbeatAcked = true;
        break;
    case Reconnect:
        qCInfo(lcGateway) << "server requested a reconnect";
        reconnect(true, 0);
        break;
    case InvalidSession:
        qCInfo(lcGateway) << "invalid session, resumable:" << data.toBool();
        // The session can't be resumed if d is false; wait a moment as Discord recommends, then identify again.
        reconnect(data.toBool(), 1000 + QRandomGenerator::global()->bounded(4000));
        break;
    case Dispatch: {
        const QString event = payload.value(u"t").toString();
        const QJsonObject object = data.toObject();
        if (event == u"READY") {
            m_sessionId = object.value(u"session_id").toString();
            m_resumeUrl = object.value(u"resume_gateway_url").toString();
            m_failedAttempts = 0;
            qCInfo(lcGateway) << "ready," << object.value(u"guilds").toArray().size() << "guilds";
            emit connectionStateChanged(true);
        } else if (event == u"RESUMED") {
            qCInfo(lcGateway) << "resumed";
            m_failedAttempts = 0;
            emit connectionStateChanged(true);
        }
        emit dispatch(event, object);
        break;
    }
    default:
        break;
    }
}

void Gateway::sendHeartbeat()
{
    if (!m_heartbeatAcked) {
        // No ACK since the last heartbeat: the connection is a zombie. Drop it and resume.
        m_socket.abort();
        return;
    }
    m_heartbeatAcked = false;
    send(Heartbeat, m_sequence ? QJsonValue(*m_sequence) : QJsonValue());
}

void Gateway::identify()
{
    send(Identify, QJsonObject{
                       {QStringLiteral("token"), m_token},
                       {QStringLiteral("capabilities"), Capabilities},
                       {QStringLiteral("properties"), ClientProperties::identifyProperties()},
                       {QStringLiteral("presence"), QJsonObject{
                                                        {QStringLiteral("status"), QStringLiteral("unknown")},
                                                        {QStringLiteral("since"), 0},
                                                        {QStringLiteral("activities"), QJsonArray()},
                                                        {QStringLiteral("afk"), false},
                                                    }},
                       {QStringLiteral("compress"), false},
                       {QStringLiteral("client_state"), QJsonObject{
                                                            {QStringLiteral("guild_versions"), QJsonObject()},
                                                        }},
                   });
}

void Gateway::resume()
{
    send(Resume, QJsonObject{
                     {QStringLiteral("token"), m_token},
                     {QStringLiteral("session_id"), m_sessionId},
                     {QStringLiteral("seq"), m_sequence ? QJsonValue(*m_sequence) : QJsonValue()},
                 });
}

void Gateway::onDisconnected()
{
    m_heartbeatTimer.stop();
    if (!m_running)
        return;

    emit connectionStateChanged(false);
    const int code = m_socket.closeCode();
    qCInfo(lcGateway) << "disconnected, code" << code << m_socket.closeReason() << m_socket.errorString();
    if (isFatalCloseCode(code)) {
        m_running = false;
        if (code == 4004)
            emit authenticationFailed();
        return;
    }
    // 4007 (invalid sequence) and 4009 (session timed out) require a fresh session.
    const bool canResume = code != 4007 && code != 4009;
    if (!canResume) {
        m_sessionId.clear();
        m_sequence.reset();
    }
    // Back off progressively when the network is down: 1s, 2s, 4s... up to 30s.
    const int delay = qMin(30000, 1000 * (1 << qMin(m_failedAttempts, 5)));
    ++m_failedAttempts;
    m_resuming = canResume && !m_sessionId.isEmpty();
    QTimer::singleShot(delay, this, [this] {
        if (m_running && m_socket.state() == QAbstractSocket::UnconnectedState)
            openSocket(m_resuming && !m_resumeUrl.isEmpty() ? m_resumeUrl : QLatin1String(DefaultGatewayUrl));
    });
}

void Gateway::reconnect(bool canResume, int delayMs)
{
    if (!canResume) {
        m_sessionId.clear();
        m_sequence.reset();
    }
    m_heartbeatTimer.stop();
    // Closing with a non-1000 code keeps the session alive on Discord's side so it can be resumed;
    // onDisconnected() then opens the new connection.
    QTimer::singleShot(delayMs, this, [this] {
        if (m_running)
            m_socket.close(static_cast<QWebSocketProtocol::CloseCode>(4000));
    });
}
