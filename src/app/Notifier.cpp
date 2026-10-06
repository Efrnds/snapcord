#include "Notifier.h"

#include "core/Markdown.h"
#include "core/Session.h"
#include "voice/SoundEffects.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWidget>

namespace {

constexpr auto DesktopKey = "notifications/desktop";
constexpr auto SoundKey = "notifications/sound";

} // namespace

Notifier::Notifier(Session* session, SoundEffects* sounds, QWidget* window, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_sounds(sounds)
    , m_window(window)
    , m_tray(new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/snapcord.svg")), this))
{
    m_tray->setToolTip(QStringLiteral("Snapcord"));
    m_tray->show();
    connect(m_session, &Session::notificationMessage, this, &Notifier::onMessage);
    // Clicking the balloon (or the tray icon) brings Snapcord back, on the conversation that notified.
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        m_window->showNormal();
        m_window->raise();
        m_window->activateWindow();
        if (!m_lastChannelId.isEmpty())
            emit openChannelRequested(m_lastGuildId, m_lastChannelId);
    });
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_window->showNormal();
            m_window->raise();
            m_window->activateWindow();
        }
    });
}

Notifier::~Notifier()
{
    m_tray->hide();
}

bool Notifier::desktopNotificationsEnabled()
{
    return QSettings().value(QLatin1String(DesktopKey), true).toBool();
}

bool Notifier::soundEnabled()
{
    return QSettings().value(QLatin1String(SoundKey), true).toBool();
}

void Notifier::setDesktopNotificationsEnabled(bool enabled)
{
    QSettings().setValue(QLatin1String(DesktopKey), enabled);
}

void Notifier::setSoundEnabled(bool enabled)
{
    QSettings().setValue(QLatin1String(SoundKey), enabled);
}

void Notifier::onMessage(const Message& message)
{
    const bool looking = m_window->isActiveWindow() && m_currentChannel && m_currentChannel() == message.channelId;
    if (looking || m_session->isMuted(message.guildId, message.channelId))
        return;

    if (soundEnabled())
        m_sounds->play(SoundEffects::Sound::Message);
    QApplication::alert(m_window);
    if (!desktopNotificationsEnabled() || !QSystemTrayIcon::supportsMessages())
        return;

    // Title like Discord: "Author" for DMs, "Author (#channel, Guild)" for guild mentions.
    QString title = message.author.displayName();
    if (!message.guildId.isEmpty()) {
        const Guild* guild = m_session->guild(message.guildId);
        const Channel* channel = m_session->channel(message.guildId, message.channelId);
        title += QStringLiteral(" (#%1, %2)").arg(channel ? channel->name : QString(), guild ? guild->name : QString());
    } else if (const PrivateChannel* channel = m_session->privateChannel(message.channelId); channel && channel->isGroup()) {
        title += QStringLiteral(" (%1)").arg(m_session->privateChannelName(*channel));
    }

    Markdown::Context context;
    context.userName = [this](const QString& id) { return m_session->user(id).displayName(); };
    QString body = Markdown::toPlainText(message.content, context);
    if (body.isEmpty() && !message.attachments.isEmpty())
        body = tr("Sent an attachment");
    if (body.size() > 200)
        body = body.left(200) + u'…';

    m_lastGuildId = message.guildId;
    m_lastChannelId = message.channelId;
    m_tray->showMessage(title, body, QSystemTrayIcon::NoIcon, 5000);
}
