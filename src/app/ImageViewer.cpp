#include "ImageViewer.h"

#include "ImageCache.h"
#include "core/ClientProperties.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MinZoom = 0.25;
constexpr double MaxZoom = 8.0;

} // namespace

void ImageViewer::open(const QUrl& url, ImageCache* cache, QWidget* parent)
{
    if (url.isEmpty())
        return;
    ImageViewer viewer(url, cache, parent);
    viewer.exec();
}

ImageViewer::ImageViewer(const QUrl& url, ImageCache* cache, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
    , m_cache(cache)
    , m_network(new QNetworkAccessManager(this))
    , m_url(url)
    , m_status(new QLabel(tr("Loading…"), this))
{
    setObjectName(QStringLiteral("imageViewer"));
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);

    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: #dbdee1; font-size: 15px; background: transparent;"));

    auto* openButton = new QPushButton(tr("Open in browser"), this);
    openButton->setObjectName(QStringLiteral("secondaryButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    connect(openButton, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_url); });

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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    layout->addWidget(m_status, 0, Qt::AlignCenter);
    layout->addStretch();
    layout->addLayout(bar);

    if (parent) {
        const QPoint topLeft = parent->mapToGlobal(QPoint(0, 0));
        setGeometry(QRect(topLeft, parent->size()));
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        setGeometry(screen->availableGeometry());
    }

    if (m_cache) {
        const QSize previewBounds = size() * int(std::max(1.0, devicePixelRatioF()));
        const QImage preview = m_cache->image(m_url, previewBounds);
        if (!preview.isNull())
            setImage(preview);
    }
    loadFullImage();
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

    const QSize area = size() - QSize(48, 96);
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

void ImageViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ImageViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_imageRect.contains(event->position().toPoint())) {
        reject();
        return;
    }
    QDialog::mousePressEvent(event);
}

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    if (m_image.isNull())
        return;
    const double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    m_zoom = std::clamp(m_zoom * step, MinZoom, MaxZoom);
    updateScaledPixmap();
    update();
}

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateScaledPixmap();
}

void ImageViewer::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 210));
    if (!m_scaled.isNull() && m_imageRect.isValid())
        painter.drawPixmap(m_imageRect, m_scaled);
}
