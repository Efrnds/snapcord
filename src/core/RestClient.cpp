#include "core/RestClient.h"

#include "core/ClientProperties.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr auto ApiBase = "https://discord.com/api/v9";

} // namespace

RestClient::RestClient(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void RestClient::get(const QString& path, Callback callback)
{
    send("GET", path, {}, false, std::move(callback));
}

void RestClient::post(const QString& path, const QJsonDocument& body, Callback callback)
{
    send("POST", path, body.toJson(QJsonDocument::Compact), true, std::move(callback));
}

void RestClient::patch(const QString& path, const QJsonDocument& body, Callback callback)
{
    send("PATCH", path, body.toJson(QJsonDocument::Compact), true, std::move(callback));
}

void RestClient::put(const QString& path, Callback callback)
{
    send("PUT", path, {}, false, std::move(callback));
}

void RestClient::deleteResource(const QString& path, Callback callback)
{
    send("DELETE", path, {}, false, std::move(callback));
}

bool RestClient::failIfOffline(const Callback& callback)
{
    if (!m_offline)
        return false;
    QTimer::singleShot(0, this, [callback] {
        Response response;
        response.networkError = QStringLiteral("Offline");
        if (callback)
            callback(response);
    });
    return true;
}

QNetworkRequest RestClient::apiRequest(const QString& path) const
{
    QNetworkRequest request(QUrl(QLatin1String(ApiBase) + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setRawHeader("X-Super-Properties", ClientProperties::superPropertiesHeader());
    request.setRawHeader("X-Discord-Locale", ClientProperties::systemLocale().toLatin1());
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", m_token.toUtf8());
    return request;
}

void RestClient::send(const QByteArray& verb, const QString& path, const QByteArray& body, bool hasBody, Callback callback)
{
    if (failIfOffline(callback))
        return;
    QNetworkRequest request = apiRequest(path);
    if (hasBody)
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    finish(m_network->sendCustomRequest(request, verb, body), nullptr, std::move(callback));
}

void RestClient::putToStorage(const QUrl& url, QIODevice* device, qint64 size, ProgressCallback progress, Callback callback)
{
    if (failIfOffline(callback))
        return;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, size);
    finish(m_network->put(request, device), std::move(progress), std::move(callback));
}

void RestClient::finish(QNetworkReply* reply, ProgressCallback progress, Callback callback)
{
    if (progress)
        connect(reply, &QNetworkReply::uploadProgress, this, std::move(progress));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        reply->deleteLater();
        Response response;
        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.body = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && response.status == 0)
            response.networkError = reply->errorString();
        if (callback)
            callback(response);
    });
}
