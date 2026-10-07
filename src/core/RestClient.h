#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QObject>
#include <QString>

#include <functional>

#include <QNetworkRequest>

class QIODevice;
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
    using ProgressCallback = std::function<void(qint64 sent, qint64 total)>;

    explicit RestClient(QObject* parent = nullptr);

    void setToken(const QString& token) { m_token = token; }
    // Page of the web client the requests appear to come from (the Referer header).
    void setReferer(const QString& referer) { m_referer = referer; }
    // An offline client fails every request right away, without touching the network (demo mode).
    void setOffline(bool offline) { m_offline = offline; }
    bool isOffline() const { return m_offline; }

    void get(const QString& path, Callback callback);
    void post(const QString& path, const QJsonDocument& body, Callback callback);
    void patch(const QString& path, const QJsonDocument& body, Callback callback);
    void put(const QString& path, Callback callback);
    void deleteResource(const QString& path, Callback callback);
    // Uploads a file's bytes to a storage URL handed out by the API, without the API's headers. `device`
    // must stay open until the callback runs.
    void putToStorage(const QUrl& url, QIODevice* device, qint64 size, ProgressCallback progress, Callback callback);

private:
    struct Request
    {
        QByteArray verb;
        QString path;
        QByteArray body;
        bool hasBody = false;
        Callback callback;
        int attempt = 0;
    };

    void send(Request request);
    void dispatch(Request request);
    void finish(QNetworkReply* reply, Request request, ProgressCallback progress);

    QNetworkAccessManager* m_network;
    QString m_token;
    QString m_referer = QStringLiteral("https://discord.com/channels/@me");
    bool m_offline = false;
};
