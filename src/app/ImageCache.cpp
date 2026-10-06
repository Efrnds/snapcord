#include "ImageCache.h"

#include "core/ClientProperties.h"
#include "core/Models.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

constexpr int SquareSize = 128;
// Memory budget in bytes of decoded pixels. Attachment previews are the largest items (400x300 ≈ 470 KiB).
constexpr int MemoryBudget = 24 * 1024 * 1024;

QString cacheKey(const QUrl& url, const QSize& bounds)
{
    return url.toString() + QStringLiteral("#%1x%2").arg(bounds.width()).arg(bounds.height());
}

ImageCache::OfflineSource& offlineSource()
{
    static ImageCache::OfflineSource source;
    return source;
}

QImage fitImage(QImage image, const QSize& bounds, bool square)
{
    if (square)
        image = image.scaled(bounds, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    else if (image.width() > bounds.width() || image.height() > bounds.height())
        image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

ImageCache::ImageCache(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_images(MemoryBudget)
{
    auto* diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                 + QStringLiteral("/images"));
    diskCache->setMaximumCacheSize(128 * 1024 * 1024);
    m_network->setCache(diskCache);
}

QImage ImageCache::image(const QUrl& url)
{
    return fetch(url, QSize(SquareSize, SquareSize), true);
}

QImage ImageCache::image(const QUrl& url, const QSize& bounds)
{
    return fetch(url, bounds, false);
}

QImage ImageCache::fetch(const QUrl& url, const QSize& bounds, bool square)
{
    if (url.isEmpty())
        return {};
    const QString key = cacheKey(url, bounds);
    if (const QImage* cached = m_images.object(key))
        return *cached;
    if (m_pending.contains(key))
        return {};

    if (const OfflineSource& source = offlineSource()) {
        const QImage image = source(url);
        if (image.isNull())
            return {};
        auto* fitted = new QImage(fitImage(image, bounds, square));
        const QImage result = *fitted;
        m_images.insert(key, fitted, static_cast<int>(fitted->sizeInBytes()));
        return result;
    }

    m_pending.insert(key);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, key, bounds, square] {
        reply->deleteLater();
        m_pending.remove(key);
        QImage image;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll()))
            return;
        image = fitImage(image, bounds, square);
        m_images.insert(key, new QImage(image), static_cast<int>(image.sizeInBytes()));
        emit imageLoaded(url);
    });
    return {};
}

void ImageCache::setOfflineSource(OfflineSource source)
{
    offlineSource() = std::move(source);
}

QUrl ImageCache::avatarUrl(const User& user)
{
    if (user.id.isEmpty())
        return {};
    if (user.avatar.isEmpty()) {
        // Default avatars are picked from the user ID for accounts on the new username system.
        const quint64 index = (user.id.toULongLong() >> 22) % 6;
        return QUrl(QStringLiteral("https://cdn.discordapp.com/embed/avatars/%1.png").arg(index));
    }
    return QUrl(QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.png?size=%3")
                    .arg(user.id, user.avatar)
                    .arg(SquareSize));
}

QUrl ImageCache::guildIconUrl(const QString& guildId, const QString& iconHash)
{
    if (iconHash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/icons/%1/%2.png?size=%3").arg(guildId, iconHash).arg(SquareSize));
}
