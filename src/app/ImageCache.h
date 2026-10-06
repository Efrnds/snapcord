#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
struct User;

// Downloads avatars, icons, emojis and attachment previews from Discord's CDN. Images are decoded at the
// size they are displayed at and kept in a bounded memory cache; the files stay in an on-disk HTTP cache.
class ImageCache : public QObject
{
    Q_OBJECT

public:
    explicit ImageCache(QObject* parent = nullptr);

    // Square images (avatars, icons, emojis), scaled to 128x128.
    QImage image(const QUrl& url);
    // Pictures shown at their own aspect ratio, scaled down to fit `bounds`.
    QImage image(const QUrl& url, const QSize& bounds);

    // Demo mode: every image comes from this function instead of the network (a null image = no picture).
    using OfflineSource = std::function<QImage(const QUrl&)>;
    static void setOfflineSource(OfflineSource source);

    static QUrl avatarUrl(const User& user);
    static QUrl guildIconUrl(const QString& guildId, const QString& iconHash);

signals:
    void imageLoaded(const QUrl& url);

private:
    QImage fetch(const QUrl& url, const QSize& bounds, bool square);

    QNetworkAccessManager* m_network;
    QCache<QString, QImage> m_images; // key: URL + requested size
    QSet<QString> m_pending;
};
