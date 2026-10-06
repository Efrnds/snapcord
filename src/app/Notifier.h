#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QSystemTrayIcon;
class QWidget;
class Session;
class SoundEffects;
struct Message;

// Desktop notifications for direct messages and mentions: a system tray balloon, a short sound and the
// taskbar flashing. Nothing is shown for the channel the user is currently looking at.
class Notifier : public QObject
{
    Q_OBJECT

public:
    Notifier(Session* session, SoundEffects* sounds, QWidget* window, QObject* parent = nullptr);
    ~Notifier() override;

    // Tells the notifier which channel is on screen, to avoid notifying about it.
    void setCurrentChannelProvider(std::function<QString()> provider) { m_currentChannel = std::move(provider); }

    static bool desktopNotificationsEnabled();
    static bool soundEnabled();
    static void setDesktopNotificationsEnabled(bool enabled);
    static void setSoundEnabled(bool enabled);

signals:
    void openChannelRequested(const QString& guildId, const QString& channelId);

private:
    void onMessage(const Message& message);

    Session* m_session;
    SoundEffects* m_sounds;
    QWidget* m_window;
    QSystemTrayIcon* m_tray;
    std::function<QString()> m_currentChannel;
    QString m_lastGuildId;
    QString m_lastChannelId;
};
