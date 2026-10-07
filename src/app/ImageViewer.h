#pragma once

#include <QDialog>
#include <QUrl>

class ImageCache;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QNetworkAccessManager;
class QVideoWidget;
class QWebEngineView;

// Full-window lightbox for chat images, Discord videos and embeds (YouTube etc. via WebEngine).
class ImageViewer : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Image, Video, Web };

    static void open(const QUrl& url, ImageCache* cache, QWidget* parent, Mode mode = Mode::Image);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    ImageViewer(const QUrl& url, ImageCache* cache, QWidget* parent, Mode mode);
    void setImage(const QImage& image);
    void updateScaledPixmap();
    void loadFullImage();
    void setupVideo();
    void setupWeb();
    QRect mediaRect() const;

    ImageCache* m_cache;
    QNetworkAccessManager* m_network = nullptr;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audio = nullptr;
    QVideoWidget* m_video = nullptr;
    QWebEngineView* m_web = nullptr;
    QUrl m_url;
    QImage m_image;
    QPixmap m_scaled;
    QRect m_imageRect;
    QLabel* m_status;
    double m_zoom = 1.0;
    Mode m_mode = Mode::Image;
};
