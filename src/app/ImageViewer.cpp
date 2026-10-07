#include "ImageViewer.h"

#include "ImageCache.h"
#include "core/ClientProperties.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWebEngineHttpRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MinZoom = 0.25;
constexpr double MaxZoom = 8.0;

QString youtubeVideoId(const QUrl& url)
{
    const QString host = url.host().toLower();
    if (host == u"youtu.be" || host.endsWith(u".youtu.be")) {
        const QString id = url.path().mid(1).section(u'/', 0, 0);
        return id.contains(u'?') ? id.section(u'?', 0, 0) : id;
    }
    if (host.contains(u"youtube.com") || host.contains(u"youtube-nocookie.com")) {
        const QString path = url.path();
        if (path.startsWith(u"/embed/"))
            return path.mid(7).section(u'/', 0, 0);
        if (path.startsWith(u"/shorts/"))
            return path.mid(8).section(u'/', 0, 0);
        if (path.startsWith(u"/live/"))
            return path.mid(6).section(u'/', 0, 0);
        return QUrlQuery(url).queryItemValue(QStringLiteral("v"));
    }
    return {};
}

// YouTube Error 153 happens when the embed has no HTTP Referer (typical in desktop WebViews).
// Load the player inside a page that claims discord.com as origin — same idea as Discord's client.
QString youtubeEmbedHtml(const QString& videoId)
{
    return QStringLiteral(
               R"(<!DOCTYPE html>
<html><head>
<meta charset="utf-8"/>
<meta name="referrer" content="strict-origin-when-cross-origin"/>
<style>
  html,body{margin:0;height:100%;background:#000;overflow:hidden}
  iframe{border:0;position:absolute;inset:0;width:100%;height:100%}
</style>
</head><body>
<iframe
  src="https://www.youtube.com/embed/%1?autoplay=1&rel=0&modestbranding=1&origin=https%%3A%%2F%%2Fdiscord.com"
  allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; fullscreen"
  referrerpolicy="strict-origin-when-cross-origin"
  allowfullscreen></iframe>
</body></html>)")
        .arg(videoId);
}

} // namespace

void ImageViewer::open(const QUrl& url, ImageCache* cache, QWidget* parent, Mode mode)
{
    if (url.isEmpty())
        return;
    ImageViewer viewer(url, cache, parent, mode);
    viewer.exec();
}

ImageViewer::ImageViewer(const QUrl& url, ImageCache* cache, QWidget* parent, Mode mode)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
    , m_cache(cache)
    , m_url(url)
    , m_status(new QLabel(tr("Loading…"), this))
    , m_mode(mode)
{
    setObjectName(QStringLiteral("imageViewer"));
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::ArrowCursor);

    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: #dbdee1; font-size: 15px; background: transparent;"));

    auto* openButton = new QPushButton(tr("Open in browser"), this);
    openButton->setObjectName(QStringLiteral("secondaryButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    connect(openButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(m_url);
    });

    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("secondaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(16, 12, 16, 16);
    bar->addStretch();
    bar->addWidget(openButton);
    bar->addWidget(closeButton);
    bar->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 0);
    layout->addStretch(1);
    layout->addWidget(m_status, 0, Qt::AlignCenter);

    if (m_mode == Mode::Video) {
        m_video = new QVideoWidget(this);
        m_video->setMinimumSize(320, 180);
        m_video->hide();
        layout->addWidget(m_video, 8);
    } else if (m_mode == Mode::Web) {
        m_web = new QWebEngineView(this);
        m_web->setMinimumSize(480, 270);
        m_web->setStyleSheet(QStringLiteral("background: #000;"));
        m_web->hide();
        layout->addWidget(m_web, 8);
    }

    layout->addStretch(1);
    layout->addLayout(bar);

    // Cover the parent window (or the whole screen if we somehow have no parent).
    if (parent) {
        const QPoint topLeft = parent->mapToGlobal(QPoint(0, 0));
        setGeometry(QRect(topLeft, parent->size()));
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(screen->availableGeometry());
    }

    switch (m_mode) {
    case Mode::Video:
        setupVideo();
        break;
    case Mode::Web:
        setupWeb();
        break;
    case Mode::Image:
        m_network = new QNetworkAccessManager(this);
        if (m_cache) {
            const QSize previewBounds = size() * int(std::max(1.0, devicePixelRatioF()));
            const QImage preview = m_cache->image(m_url, previewBounds);
            if (!preview.isNull())
                setImage(preview);
        }
        loadFullImage();
        break;
    }
}

void ImageViewer::setupVideo()
{
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);

    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString&) {
        m_status->setText(tr("Could not play video. Try Open in browser."));
        m_status->show();
        if (m_video)
            m_video->hide();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::LoadedMedia
            || status == QMediaPlayer::EndOfMedia) {
            m_status->hide();
            if (m_video)
                m_video->show();
        }
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) {
            m_status->hide();
            if (m_video)
                m_video->show();
        }
    });

    m_player->setSource(m_url);
    m_player->play();
}

void ImageViewer::setupWeb()
{
    QWebEngineSettings* settings = m_web->settings();
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    connect(m_web, &QWebEngineView::loadStarted, this, [this] {
        m_status->setText(tr("Loading…"));
        m_status->show();
    });
    connect(m_web, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            m_status->setText(tr("Could not load embed. Try Open in browser."));
            m_status->show();
            m_web->hide();
            return;
        }
        m_status->hide();
        m_web->show();
    });

    const QString youtubeId = youtubeVideoId(m_url);
    if (!youtubeId.isEmpty()) {
        // Base URL gives the iframe a real HTTPS Referer — required since YouTube Error 153.
        m_web->setHtml(youtubeEmbedHtml(youtubeId), QUrl(QStringLiteral("https://discord.com/")));
        return;
    }

    // Other embeds: force a Referer so providers that check it still accept the player.
    QWebEngineHttpRequest request(m_url);
    request.setHeader(QByteArrayLiteral("Referer"), QByteArrayLiteral("https://discord.com"));
    m_web->load(request);
}

void ImageViewer::loadFullImage()
{
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QImage image;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll())) {
            if (m_image.isNull())
                m_status->setText(tr("Could not load image."));
            return;
        }
        setImage(image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    });
}

void ImageViewer::setImage(const QImage& image)
{
    if (image.isNull())
        return;
    m_image = image;
    m_status->hide();
    m_zoom = 1.0;
    updateScaledPixmap();
    update();
}

void ImageViewer::updateScaledPixmap()
{
    if (m_image.isNull()) {
        m_scaled = {};
        m_imageRect = {};
        return;
    }

    const QSize area = size() - QSize(48, 96); // leave room for the bottom bar
    if (area.width() < 32 || area.height() < 32)
        return;

    QSize fitted = m_image.size();
    fitted.scale(area, Qt::KeepAspectRatio);
    fitted = QSize(std::max(1, int(fitted.width() * m_zoom)), std::max(1, int(fitted.height() * m_zoom)));

    const qreal dpr = devicePixelRatioF();
    m_scaled = QPixmap::fromImage(m_image.scaled(fitted * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_scaled.setDevicePixelRatio(dpr);
    m_imageRect = QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height() - 56) / 2), fitted);
}

QRect ImageViewer::mediaRect() const
{
    if (m_mode == Mode::Video && m_video && m_video->isVisible())
        return m_video->geometry();
    if (m_mode == Mode::Web && m_web && m_web->isVisible())
        return m_web->geometry();
    return m_imageRect;
}

void ImageViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        if (m_mode == Mode::Video && m_player) {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            else
                m_player->play();
            return;
        }
        if (m_mode == Mode::Web)
            return; // let the page handle space (YouTube play/pause)
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ImageViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Click on the dimmed backdrop (not the media) closes the viewer.
        if (!mediaRect().contains(event->position().toPoint())) {
            reject();
            return;
        }
        if (m_mode == Mode::Video && m_player) {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            else
                m_player->play();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    if (m_mode != Mode::Image || m_image.isNull()) {
        QDialog::wheelEvent(event);
        return;
    }
    const double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    m_zoom = std::clamp(m_zoom * step, MinZoom, MaxZoom);
    updateScaledPixmap();
    update();
}

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_mode == Mode::Image)
        updateScaledPixmap();
}

void ImageViewer::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 210));
    if (m_mode == Mode::Image && !m_scaled.isNull() && m_imageRect.isValid())
        painter.drawPixmap(m_imageRect, m_scaled);
}
