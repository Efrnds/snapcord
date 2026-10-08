#include "VideoViewer.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <algorithm>
#include <limits>

namespace {

bool discordHost(const QString& host)
{
    const QString name = host.toLower();
    return name == u"cdn.discordapp.com" || name.endsWith(u".cdn.discordapp.com")
        || name == u"media.discordapp.net" || name.endsWith(u".media.discordapp.net");
}

QString clock(qint64 ms)
{
    const qint64 seconds = std::max(qint64(0), ms) / 1000;
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QChar(u'0'));
}

} // namespace

bool VideoViewer::isDiscordFile(const QUrl& url)
{
    if (!url.isValid() || !discordHost(url.host()))
        return false;
    const QString path = url.path().toLower();
    return path.endsWith(u".mp4") || path.endsWith(u".webm");
}

void VideoViewer::open(const QUrl& url, const QRect& frame)
{
    if (!isDiscordFile(url))
        return;
    VideoViewer viewer(url, frame);
    viewer.exec();
}

VideoViewer::VideoViewer(const QUrl& url, const QRect& frame)
    : QDialog(nullptr, Qt::FramelessWindowHint | Qt::Dialog)
    , m_url(url)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_video(new QVideoWidget(this))
    , m_status(new QLabel(tr("Loading…"), this))
    , m_time(new QLabel(QStringLiteral("0:00 / 0:00"), this))
    , m_play(new QPushButton(tr("Pause"), this))
    , m_mute(new QPushButton(tr("Mute"), this))
    , m_position(new QSlider(Qt::Horizontal, this))
    , m_volume(new QSlider(Qt::Horizontal, this))
{
    setObjectName(QStringLiteral("videoViewer"));
    setModal(true);
    setStyleSheet(QStringLiteral(
        "#videoViewer { background: #111214; }"
        "QLabel { color: #dbdee1; background: transparent; }"
        "QSlider::groove:horizontal { height: 4px; background: #3f4147; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: #dbdee1; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #5865f2; border-radius: 2px; }"));

    m_video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_video->setMinimumSize(320, 180);
    m_video->installEventFilter(this);
    m_status->setAlignment(Qt::AlignCenter);

    m_position->setRange(0, 0);
    m_position->setTracking(true);
    m_volume->setRange(0, 100);
    m_volume->setValue(100);
    m_volume->setFixedWidth(96);
    m_volume->setToolTip(tr("Volume"));
    m_position->setToolTip(tr("Progress"));

    for (QPushButton* button : {m_play, m_mute}) {
        button->setObjectName(QStringLiteral("secondaryButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    auto* openButton = new QPushButton(tr("Open in browser"), this);
    openButton->setObjectName(QStringLiteral("secondaryButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setAutoDefault(false);
    connect(openButton, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_url); });

    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("secondaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(16, 8, 16, 16);
    bar->setSpacing(8);
    bar->addWidget(m_play);
    bar->addWidget(m_position, 1);
    bar->addWidget(m_time);
    bar->addWidget(m_mute);
    bar->addWidget(m_volume);
    bar->addWidget(openButton);
    bar->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_video, 1);
    layout->addWidget(m_status);
    layout->addLayout(bar);

    if (frame.isValid())
        setGeometry(frame);
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        setGeometry(screen->availableGeometry());

    m_audio->setVolume(1.0);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);

    connect(m_play, &QPushButton::clicked, this, &VideoViewer::togglePlayback);
    connect(m_mute, &QPushButton::clicked, this, [this] {
        m_audio->setMuted(!m_audio->isMuted());
        syncMuteButton();
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        m_audio->setVolume(value / 100.0);
        if (value > 0 && m_audio->isMuted())
            m_audio->setMuted(false);
        syncMuteButton();
    });
    connect(m_position, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_position, &QSlider::sliderReleased, this, [this] {
        m_player->setPosition(m_position->value());
        m_seeking = false;
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_position->setRange(0, int(std::clamp(duration, qint64(0), qint64(std::numeric_limits<int>::max()))));
        m_time->setText(clock(m_player->position()) + QStringLiteral(" / ") + clock(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!m_seeking)
            m_position->setValue(int(std::clamp(position, qint64(0), qint64(m_position->maximum()))));
        m_time->setText(clock(position) + QStringLiteral(" / ") + clock(m_player->duration()));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &VideoViewer::syncPlayButton);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia
            || status == QMediaPlayer::BufferingMedia || status == QMediaPlayer::EndOfMedia)
            m_status->hide();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this] {
        m_status->setText(tr("Could not play this video."));
        m_status->show();
        syncPlayButton();
    });

    m_player->setSource(m_url);
    m_player->play();
}

VideoViewer::~VideoViewer()
{
    releasePlayback();
}

void VideoViewer::releasePlayback()
{
    if (!m_player)
        return;
    m_player->stop();
    m_player->setSource({});
    if (m_audio)
        m_audio->setMuted(true);
}

void VideoViewer::reject()
{
    releasePlayback();
    QDialog::reject();
}

void VideoViewer::togglePlayback()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoViewer::syncPlayButton()
{
    m_play->setText(m_player->playbackState() == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
}

void VideoViewer::syncMuteButton()
{
    m_mute->setText(m_audio->isMuted() || m_volume->value() == 0 ? tr("Unmute") : tr("Mute"));
}

void VideoViewer::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        togglePlayback();
        return;
    }
    QDialog::keyPressEvent(event);
}

void VideoViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget* child = childAt(event->position().toPoint());
        if (!child || child == this) {
            reject();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

bool VideoViewer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_video && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            togglePlayback();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
