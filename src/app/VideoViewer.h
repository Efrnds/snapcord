#pragma once

#include <QDialog>
#include <QRect>
#include <QUrl>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QVideoWidget;

// Plays one direct Discord file, then exits with the process so the decoder is freed.
class VideoViewer : public QDialog
{
    Q_OBJECT

public:
    static bool isDiscordFile(const QUrl& url);
    static void open(const QUrl& url, const QRect& frame);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    VideoViewer(const QUrl& url, const QRect& frame);
    ~VideoViewer() override;

    void releasePlayback();
    void togglePlayback();
    void syncPlayButton();
    void syncMuteButton();
    void reject() override;

    QUrl m_url;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audio = nullptr;
    QVideoWidget* m_video = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_time = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_mute = nullptr;
    QSlider* m_position = nullptr;
    QSlider* m_volume = nullptr;
    bool m_seeking = false;
};
