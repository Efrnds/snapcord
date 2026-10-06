#pragma once

#include <QJsonDocument>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Minimal client for the Discord HTTP API.
class RestClient : public QObject
{
    Q_OBJECT

public:
    struct Response
    {
        int status = 0;
        QJsonDocument body;
        QString networkError;

        bool ok() const { return status >= 200 && status < 300; }
    };
    using Callback = std::function<void(const Response&)>;

    explicit RestClient(QObject* parent = nullptr);

    void setToken(const QString& token) { m_token = token; }

    void get(const QString& path, Callback callback);
    void post(const QString& path, const QJsonDocument& body, Callback callback);

private:
    void finish(QNetworkReply* reply, const Callback& callback);

    QNetworkAccessManager* m_network;
    QString m_token;
};
