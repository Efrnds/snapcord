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
    QNetworkRequest apiRequest(const QString& path) const;
    void finish(QNetworkReply* reply, ProgressCallback progress, Callback callback);
    bool failIfOffline(const Callback& callback);
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, bool hasBody, Callback callback);

    QNetworkAccessManager* m_network;
    QString m_token;
    bool m_offline = false;
};
