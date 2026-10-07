#pragma once

#include <QDialog>
#include <QUrl>

class ImageCache;
class QLabel;
class QNetworkAccessManager;

// Full-window lightbox for chat images.
class ImageViewer : public QDialog
{
    Q_OBJECT

public:
    static void open(const QUrl& url, ImageCache* cache, QWidget* parent);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    ImageViewer(const QUrl& url, ImageCache* cache, QWidget* parent);
    void setImage(const QImage& image);
    void updateScaledPixmap();
    void loadFullImage();

    ImageCache* m_cache;
    QNetworkAccessManager* m_network;
    QUrl m_url;
    QImage m_image;
    QPixmap m_scaled;
    QRect m_imageRect;
    QLabel* m_status;
    double m_zoom = 1.0;
};
