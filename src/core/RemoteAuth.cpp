#include "core/RemoteAuth.h"

#include "core/ClientProperties.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkRequest>
#include <QUrl>

Q_LOGGING_CATEGORY(lcRemoteAuth, "snapcord.remoteauth")

namespace {

constexpr auto GatewayUrl = "wss://remote-auth-gateway.discord.gg/?v=2";

QByteArray base64Url(const QByteArray& data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace

RemoteAuth::RemoteAuth(QObject* parent)
    : QObject(parent)
    // The remote auth gateway rejects connections without a discord.com Origin. QWebSocket only sends the
    // origin given to its constructor, not one set as a raw request header.
    , m_socket(ClientProperties::origin())
    , m_rest(this)
{
    m_rest.setReferer(QStringLiteral("https://discord.com/login"));
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RemoteAuth::onTextMessage);
    connect(&m_socket, &QWebSocket::disconnected, this, &RemoteAuth::onDisconnected);
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        qCInfo(lcRemoteAuth) << "connected";
        m_connected = true;
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qCWarning(lcRemoteAuth) << "socket error" << error << m_socket.errorString();
    });
    connect(&m_heartbeat, &QTimer::timeout, this, [this] { send({{QStringLiteral("op"), QStringLiteral("heartbeat")}}); });
}

RemoteAuth::~RemoteAuth()
{
    stop();
}

void RemoteAuth::start()
{
    stop();
    m_active = true;
    m_awaitingLogin = false;
    m_key = std::make_unique<RsaKey>();
    if (!m_key->isValid()) {
        fail(tr("Could not generate the encryption key for the QR code login."));
        return;
    }

    QNetworkRequest request{QUrl(QLatin1String(GatewayUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    m_connected = false;
    m_socket.open(request);
}

void RemoteAuth::stop()
{
    m_active = false;
    m_heartbeat.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
}

void RemoteAuth::send(const QJsonObject& payload)
{
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void RemoteAuth::onTextMessage(const QString& message)
{
    const QJsonObject payload = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString op = payload.value(QStringLiteral("op")).toString();
    qCInfo(lcRemoteAuth) << "received" << op;

    if (op == u"hello") {
        m_heartbeat.start(payload.value(QStringLiteral("heartbeat_interval")).toInt(41250));
        send({
            {QStringLiteral("op"), QStringLiteral("init")},
            {QStringLiteral("encoded_public_key"), QString::fromLatin1(m_key->publicKeySpki().toBase64())},
        });
    } else if (op == u"nonce_proof") {
        const auto nonce = m_key->decrypt(
            QByteArray::fromBase64(payload.value(QStringLiteral("encrypted_nonce")).toString().toLatin1()));
        if (!nonce) {
            fail(tr("The QR code login handshake failed."));
            return;
        }
        send({
            {QStringLiteral("op"), QStringLiteral("nonce_proof")},
            {QStringLiteral("nonce"), QString::fromLatin1(base64Url(*nonce))},
        });
    } else if (op == u"pending_remote_init") {
        const QString fingerprint = payload.value(QStringLiteral("fingerprint")).toString();
        const QByteArray expected =
            base64Url(QCryptographicHash::hash(m_key->publicKeySpki(), QCryptographicHash::Sha256));
        if (fingerprint.toLatin1() != expected) {
            // Someone tampered with the handshake; start over with a fresh key.
            start();
            return;
        }
        emit qrCodeReady(QStringLiteral("https://discord.com/ra/") + fingerprint);
    } else if (op == u"pending_ticket") {
        const auto decrypted = m_key->decrypt(
            QByteArray::fromBase64(payload.value(QStringLiteral("encrypted_user_payload")).toString().toLatin1()));
        if (!decrypted)
            return;
        // Format: "id:discriminator:avatar:username". The avatar is "0" when the user has none.
        const QStringList fields = QString::fromUtf8(*decrypted).split(u':');
        if (fields.size() >= 4) {
            UserPreview user;
            user.id = fields[0];
            user.avatar = fields[2] == u"0" ? QString() : fields[2];
            user.username = fields.mid(3).join(u':');
            emit userScanned(user);
        }
    } else if (op == u"pending_login") {
        m_awaitingLogin = true;
        exchangeTicket(payload.value(QStringLiteral("ticket")).toString());
    } else if (op == u"cancel") {
        // The user declined on the phone; offer a fresh QR code.
        start();
    }
}

void RemoteAuth::onDisconnected()
{
    m_heartbeat.stop();
    if (!m_active || m_awaitingLogin)
        return;

    // The session timed out (close code 4003) or the connection dropped: show a new QR code.
    const auto code = m_socket.closeCode();
    qCInfo(lcRemoteAuth) << "disconnected" << code << m_socket.closeReason();
    if (!m_connected) {
        // The connection never opened (no internet, or Discord refused it): don't retry in a loop.
        fail(tr("Could not connect to Discord. Check your internet connection and try again."));
        return;
    }
    if (code == 4003 || code == QWebSocketProtocol::CloseCodeNormal || code == QWebSocketProtocol::CloseCodeAbnormalDisconnection)
        QTimer::singleShot(1000, this, [this] {
            if (m_active && !m_awaitingLogin)
                start();
        });
    else
        fail(tr("Could not connect to Discord (code %1).").arg(static_cast<int>(code)));
}

void RemoteAuth::exchangeTicket(const QString& ticket)
{
    const QJsonDocument body(QJsonObject{{QStringLiteral("ticket"), ticket}});
    m_rest.post(QStringLiteral("/users/@me/remote-auth/login"), body, [this](const RestClient::Response& response) {
        if (!m_active)
            return;
        if (!response.ok()) {
            const QJsonObject error = response.body.object();
            if (error.contains(QStringLiteral("captcha_key")))
                fail(tr("Discord asked for a captcha, which Snapcord does not support yet. Log in with a token "
                        "instead, or try again in a few minutes."));
            else
                fail(tr("Discord rejected the login (HTTP %1).").arg(response.status));
            return;
        }
        const auto token = m_key->decrypt(QByteArray::fromBase64(
            response.body.object().value(QStringLiteral("encrypted_token")).toString().toLatin1()));
        if (!token) {
            fail(tr("Could not decrypt the login token."));
            return;
        }
        m_active = false;
        m_heartbeat.stop();
        m_socket.close();
        emit loggedIn(QString::fromUtf8(*token));
    });
}

void RemoteAuth::fail(const QString& reason)
{
    stop();
    emit failed(reason);
}
