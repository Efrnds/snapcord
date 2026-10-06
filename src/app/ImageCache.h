#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;
struct User;

// Downloads avatars and guild icons from Discord's CDN, keeping a small decoded cache in memory and the
// files in an on-disk HTTP cache. Images are decoded at the size they are displayed at.
class ImageCache : public QObject
{
    Q_OBJECT

public:
    explicit ImageCache(QObject* parent = nullptr);

    // Returns the image if available; otherwise starts loading it and returns a null image.
    QImage image(const QUrl& url);

    static QUrl avatarUrl(const User& user);
    static QUrl guildIconUrl(const QString& guildId, const QString& iconHash);

signals:
    void imageLoaded(const QUrl& url);

private:
    QNetworkAccessManager* m_network;
    QCache<QUrl, QImage> m_images;
    QSet<QUrl> m_pending;
};
