#include "ImageCache.h"

#include "core/ClientProperties.h"
#include "core/Models.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

constexpr int ImageSize = 128;
// Memory cache budget in bytes of decoded pixels (128x128 RGBA images are 64 KiB each).
constexpr int MemoryBudget = 8 * 1024 * 1024;

} // namespace

ImageCache::ImageCache(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_images(MemoryBudget)
{
    auto* diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                 + QStringLiteral("/images"));
    diskCache->setMaximumCacheSize(32 * 1024 * 1024);
    m_network->setCache(diskCache);
}

QImage ImageCache::image(const QUrl& url)
{
    if (url.isEmpty())
        return {};
    if (const QImage* cached = m_images.object(url))
        return *cached;
    if (m_pending.contains(url))
        return {};

    m_pending.insert(url);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        m_pending.remove(url);
        QImage image;
        if (reply->error() == QNetworkReply::NoError && image.loadFromData(reply->readAll())) {
            if (image.width() != ImageSize)
                image = image.scaled(ImageSize, ImageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            m_images.insert(url, new QImage(image), static_cast<int>(image.sizeInBytes()));
            emit imageLoaded(url);
        }
    });
    return {};
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
                    .arg(ImageSize));
}

QUrl ImageCache::guildIconUrl(const QString& guildId, const QString& iconHash)
{
    if (iconHash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/icons/%1/%2.png?size=%3").arg(guildId, iconHash).arg(ImageSize));
}
