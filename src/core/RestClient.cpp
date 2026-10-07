#include "core/RestClient.h"

#include "core/ClientProperties.h"
#include "core/Log.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr auto ApiBase = "https://discord.com/api/v9";

// When Discord answers 429 (rate limited) it says how long to wait. The request is retried after that, like
// the official client, instead of being sent again right away. A few attempts are allowed before giving up,
// so a genuinely stuck request still fails instead of looping forever.
constexpr int MaxRateLimitRetries = 3;
constexpr int MaxRetryDelayMs = 60 * 1000;

} // namespace

RestClient::RestClient(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void RestClient::get(const QString& path, Callback callback)
{
    send({"GET", path, {}, false, std::move(callback), 0});
}

void RestClient::post(const QString& path, const QJsonDocument& body, Callback callback)
{
    send({"POST", path, body.toJson(QJsonDocument::Compact), true, std::move(callback), 0});
}

void RestClient::patch(const QString& path, const QJsonDocument& body, Callback callback)
{
    send({"PATCH", path, body.toJson(QJsonDocument::Compact), true, std::move(callback), 0});
}

void RestClient::put(const QString& path, Callback callback)
{
    send({"PUT", path, {}, false, std::move(callback), 0});
}

void RestClient::deleteResource(const QString& path, Callback callback)
{
    send({"DELETE", path, {}, false, std::move(callback), 0});
}

void RestClient::send(Request request)
{
    if (m_offline) {
        QTimer::singleShot(0, this, [callback = std::move(request.callback)] {
            Response response;
            response.networkError = QStringLiteral("Offline");
            if (callback)
                callback(response);
        });
        return;
    }
    dispatch(std::move(request));
}

void RestClient::dispatch(Request request)
{
    QNetworkRequest networkRequest(QUrl(QLatin1String(ApiBase) + request.path));
    if (!m_token.isEmpty())
        networkRequest.setRawHeader("Authorization", m_token.toUtf8());
    if (request.hasBody)
        networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    ClientProperties::applyApiHeaders(networkRequest, request.verb, m_referer);

    finish(m_network->sendCustomRequest(networkRequest, request.verb, request.body), std::move(request), nullptr);
}

void RestClient::putToStorage(const QUrl& url, QIODevice* device, qint64 size, ProgressCallback progress, Callback callback)
{
    if (m_offline) {
        send({"PUT", {}, {}, false, std::move(callback), 0});
        return;
    }
    // The storage bucket is another site, so a browser sends only its own headers there, with discord.com as
    // Origin and, cross-site, just the origin as Referer.
    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    networkRequest.setRawHeader("Origin", ClientProperties::origin().toLatin1());
    networkRequest.setRawHeader("Referer", ClientProperties::origin().toLatin1() + '/');
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    networkRequest.setHeader(QNetworkRequest::ContentLengthHeader, size);
    // Not retried on 429: the body was read from `device` already. The storage service does not rate limit like
    // the API anyway.
    Request request{"PUT", url.toString(), {}, false, std::move(callback), MaxRateLimitRetries};
    finish(m_network->put(networkRequest, device), std::move(request), std::move(progress));
}

void RestClient::finish(QNetworkReply* reply, Request request, ProgressCallback progress)
{
    if (progress)
        connect(reply, &QNetworkReply::uploadProgress, this, std::move(progress));
    connect(reply, &QNetworkReply::finished, this, [this, reply, request = std::move(request)]() mutable {
        reply->deleteLater();
        Response response;
        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bodyBytes = reply->readAll();
        response.body = QJsonDocument::fromJson(bodyBytes);
        if (reply->error() != QNetworkReply::NoError && response.status == 0)
            response.networkError = reply->errorString();

        if (response.status == 429 && request.attempt < MaxRateLimitRetries) {
            // retry_after is in seconds (a fractional value); the header is the fallback.
            double retryAfter = response.body.object().value(u"retry_after").toDouble(-1.0);
            if (retryAfter < 0)
                retryAfter = reply->rawHeader("Retry-After").toDouble();
            const int delayMs = qBound(0, static_cast<int>(retryAfter * 1000.0) + 50, MaxRetryDelayMs);
            qCInfo(lcGateway) << "rate limited on" << request.verb << request.path << "- retrying in" << delayMs << "ms";
            ++request.attempt;
            QTimer::singleShot(delayMs, this, [this, request = std::move(request)]() mutable { dispatch(std::move(request)); });
            return;
        }

        if (request.callback)
            request.callback(response);
    });
}
