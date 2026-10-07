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

void RestClient::send(const QByteArray& verb, const QString& path, const QByteArray& body, bool hasBody, Callback callback)
{
    if (m_offline) {
        QTimer::singleShot(0, this, [callback = std::move(callback)] {
            Response response;
            response.networkError = QStringLiteral("Offline");
            if (callback)
                callback(response);
        });
        return;
    }

    QNetworkRequest request(QUrl(QLatin1String(ApiBase) + path));
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", m_token.toUtf8());
    if (hasBody)
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    ClientProperties::applyApiHeaders(request, verb, m_referer);

    QNetworkReply* reply = m_network->sendCustomRequest(request, verb, body);
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
