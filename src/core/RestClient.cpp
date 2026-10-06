#include "core/RestClient.h"

#include "core/ClientProperties.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
    QNetworkRequest request(QUrl(QLatin1String(ApiBase) + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setRawHeader("X-Super-Properties", ClientProperties::superPropertiesHeader());
    request.setRawHeader("X-Discord-Locale", ClientProperties::systemLocale().toLatin1());
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", m_token.toUtf8());

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback] { finish(reply, callback); });
}

void RestClient::post(const QString& path, const QJsonDocument& body, Callback callback)
{
    QNetworkRequest request(QUrl(QLatin1String(ApiBase) + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader("X-Super-Properties", ClientProperties::superPropertiesHeader());
    request.setRawHeader("X-Discord-Locale", ClientProperties::systemLocale().toLatin1());
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", m_token.toUtf8());

    QNetworkReply* reply = m_network->post(request, body.toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback] { finish(reply, callback); });
}

void RestClient::finish(QNetworkReply* reply, const Callback& callback)
{
    reply->deleteLater();
    Response response;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = QJsonDocument::fromJson(reply->readAll());
    if (reply->error() != QNetworkReply::NoError && response.status == 0)
        response.networkError = reply->errorString();
    if (callback)
        callback(response);
}
