#include "voice/VoiceGateway.h"

#include "core/Log.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QtEndian>

namespace {

enum Opcode {
    Identify = 0,
    SelectProtocol = 1,
    Ready = 2,
    Heartbeat = 3,
    SessionDescription = 4,
    Speaking = 5,
    HeartbeatAck = 6,
    Resume = 7,
    Hello = 8,
    Resumed = 9,
    ClientsConnect = 11,
    ClientDisconnect = 13,
};

constexpr int FirstDaveOpcode = 21;
constexpr int LastDaveOpcode = 31;

// Close codes after which the session can be resumed: abnormal drops and "voice server crashed".
bool canResume(int code)
{
    return code == QWebSocketProtocol::CloseCodeAbnormalDisconnection || code == QWebSocketProtocol::CloseCodeGoingAway
        || code == 4015;
}

} // namespace

VoiceGateway::VoiceGateway(QObject* parent)
    : QObject(parent)
{
    m_heartbeatTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &VoiceGateway::sendHeartbeat);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &VoiceGateway::onTextMessage);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &VoiceGateway::onBinaryMessage);
    connect(&m_socket, &QWebSocket::disconnected, this, &VoiceGateway::onDisconnected);
}

void VoiceGateway::open(const Credentials& credentials, int maxDaveVersion)
{
    m_credentials = credentials;
    m_maxDaveVersion = maxDaveVersion;
    m_lastSequence = -1;
    m_resuming = false;
    m_resumeAttempts = 0;
    m_open = true;
    connectSocket();
}

void VoiceGateway::connectSocket()
{
    QString endpoint = m_credentials.endpoint;
    if (!endpoint.startsWith(u"wss://"))
        endpoint.prepend(u"wss://");
    qCInfo(lcVoice) << "connecting to voice server" << endpoint << (m_resuming ? "(resume)" : "");
    m_socket.open(QUrl(endpoint + QStringLiteral("/?v=8")));
}

void VoiceGateway::close()
{
    m_open = false;
    m_heartbeatTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.close();
}

void VoiceGateway::sendJson(int op, const QJsonObject& data)
{
    const QJsonObject payload{{QStringLiteral("op"), op}, {QStringLiteral("d"), data}};
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void VoiceGateway::sendBinary(int op, const QByteArray& payload)
{
    QByteArray message;
    message.reserve(payload.size() + 1);
    message.append(static_cast<char>(op));
    message.append(payload);
    m_socket.sendBinaryMessage(message);
}

void VoiceGateway::selectProtocol(const QString& address, quint16 port, const QString& mode)
{
    sendJson(SelectProtocol, QJsonObject{
                                 {QStringLiteral("protocol"), QStringLiteral("udp")},
                                 {QStringLiteral("data"), QJsonObject{
                                                              {QStringLiteral("address"), address},
                                                              {QStringLiteral("port"), port},
                                                              {QStringLiteral("mode"), mode},
                                                          }},
                             });
}

void VoiceGateway::sendSpeaking(int flags, quint32 ssrc)
{
    sendJson(Speaking, QJsonObject{
                           {QStringLiteral("speaking"), flags},
                           {QStringLiteral("delay"), 0},
                           {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
                       });
}

void VoiceGateway::sendHeartbeat()
{
    m_heartbeatSentAt.start();
    QJsonObject data{{QStringLiteral("t"), QDateTime::currentMSecsSinceEpoch()}};
    if (m_lastSequence >= 0)
        data.insert(QStringLiteral("seq_ack"), m_lastSequence);
    sendJson(Heartbeat, data);
}

void VoiceGateway::onTextMessage(const QString& message)
{
    const QJsonObject payload = QJsonDocument::fromJson(message.toUtf8()).object();
    const int op = payload.value(u"op").toInt(-1);
    const QJsonObject data = payload.value(u"d").toObject();
    if (payload.contains(u"seq"))
        m_lastSequence = payload.value(u"seq").toInt();

    switch (op) {
    case Hello: {
        m_heartbeatTimer.start(qMax(1000, static_cast<int>(data.value(u"heartbeat_interval").toDouble(13750))));
        if (m_resuming) {
            QJsonObject resume{
                {QStringLiteral("server_id"), m_credentials.serverId},
                {QStringLiteral("session_id"), m_credentials.sessionId},
                {QStringLiteral("token"), m_credentials.token},
            };
            if (m_lastSequence >= 0)
                resume.insert(QStringLiteral("seq_ack"), m_lastSequence);
            sendJson(Resume, resume);
        } else {
            sendJson(Identify, QJsonObject{
                                   {QStringLiteral("server_id"), m_credentials.serverId},
                                   {QStringLiteral("user_id"), m_credentials.userId},
                                   {QStringLiteral("session_id"), m_credentials.sessionId},
                                   {QStringLiteral("token"), m_credentials.token},
                                   {QStringLiteral("max_dave_protocol_version"), m_maxDaveVersion},
                               });
        }
        break;
    }
    case Ready: {
        QStringList modes;
        for (const QJsonValue& mode : data.value(u"modes").toArray())
            modes.append(mode.toString());
        qCInfo(lcVoice) << "voice ready, modes" << modes;
        emit ready(static_cast<quint32>(data.value(u"ssrc").toInteger()), data.value(u"ip").toString(),
                   static_cast<quint16>(data.value(u"port").toInt()), modes);
        break;
    }
    case SessionDescription: {
        QByteArray key;
        for (const QJsonValue& byte : data.value(u"secret_key").toArray())
            key.append(static_cast<char>(byte.toInt()));
        qCInfo(lcVoice) << "session description: mode" << data.value(u"mode").toString() << "DAVE version"
                        << data.value(u"dave_protocol_version").toInt();
        emit sessionDescription(data.value(u"mode").toString(), key, data.value(u"dave_protocol_version").toInt());
        break;
    }
    case HeartbeatAck:
        if (m_heartbeatSentAt.isValid())
            emit pingChanged(static_cast<int>(m_heartbeatSentAt.elapsed()));
        break;
    case Resumed:
        m_resumeAttempts = 0;
        emit resumed();
        break;
    case Speaking:
        emit speaking(data.value(u"user_id").toString(), static_cast<quint32>(data.value(u"ssrc").toInteger()),
                      data.value(u"speaking").toInt());
        break;
    case ClientsConnect: {
        QStringList ids;
        for (const QJsonValue& id : data.value(u"user_ids").toArray())
            ids.append(id.toString());
        emit clientsConnected(ids);
        break;
    }
    case ClientDisconnect:
        emit clientDisconnected(data.value(u"user_id").toString());
        break;
    default:
        if (op >= FirstDaveOpcode && op <= LastDaveOpcode) {
            qCInfo(lcVoice) << "DAVE opcode" << op << data;
            emit daveJson(op, data);
        }
        break;
    }
}

void VoiceGateway::onBinaryMessage(const QByteArray& message)
{
    // Server-to-client binary messages: 2-byte sequence number, 1-byte opcode, payload.
    if (message.size() < 3)
        return;
    m_lastSequence = qFromBigEndian<quint16>(message.constData());
    const int op = static_cast<quint8>(message.at(2));
    qCInfo(lcVoice) << "DAVE binary opcode" << op << (message.size() - 3) << "bytes";
    emit daveBinary(op, message.mid(3));
}

void VoiceGateway::onDisconnected()
{
    m_heartbeatTimer.stop();
    if (!m_open)
        return;
    const int code = m_socket.closeCode();
    qCInfo(lcVoice) << "voice server disconnected, code" << code << m_socket.closeReason() << m_socket.errorString();
    if (canResume(code) && m_resumeAttempts < 3) {
        ++m_resumeAttempts;
        m_resuming = true;
        QTimer::singleShot(500 * m_resumeAttempts, this, [this] {
            if (m_open)
                connectSocket();
        });
        return;
    }
    m_open = false;
    emit closed(code);
}
