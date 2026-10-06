#pragma once

#include "core/RestClient.h"
#include "core/RsaKey.h"

#include <QObject>
#include <QTimer>
#include <QWebSocket>

#include <memory>

// QR code login ("remote auth"): the user scans a QR code with the Discord mobile app, which hands the
// account token over to this client. See https://docs.discord.food/remote-authentication/desktop
class RemoteAuth : public QObject
{
    Q_OBJECT

public:
    struct UserPreview
    {
        QString id;
        QString username;
        QString avatar;
    };

    explicit RemoteAuth(QObject* parent = nullptr);
    ~RemoteAuth() override;

    // Starts (or restarts) a login session. A new QR code is announced through qrCodeReady().
    void start();
    void stop();

signals:
    void qrCodeReady(const QString& url);
    void userScanned(const RemoteAuth::UserPreview& user);
    void loggedIn(const QString& token);
    void failed(const QString& reason);

private:
    void onTextMessage(const QString& message);
    void onDisconnected();
    void send(const QJsonObject& payload);
    void exchangeTicket(const QString& ticket);
    void fail(const QString& reason);

    QWebSocket m_socket;
    QTimer m_heartbeat;
    RestClient m_rest;
    std::unique_ptr<RsaKey> m_key;
    bool m_active = false;
    bool m_awaitingLogin = false;
    bool m_connected = false;
};
