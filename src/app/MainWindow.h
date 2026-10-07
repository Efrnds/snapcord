#pragma once

#include <QMainWindow>
#include <QTimer>

class ChannelSidebar;
class ChatView;
class ImageCache;
class IncomingCallWindow;
class Notifier;
class QStackedWidget;
class ServerRail;
class Session;
class VoiceChannelView;
class VoiceController;
struct PrivateChannel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session* session, VoiceController* voice, QWidget* parent = nullptr);

    // An empty guildId opens a direct message.
    void showChannel(const QString& guildId, const QString& channelId);

signals:
    void logoutRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    class AppShell;
    QWidget* buildPlaceholderPage(const QString& title, const QString& subtitle);
    void rebuildServerRail();
    void refreshUnreadBadges();
    void selectGuild(const QString& guildId);
    void openChannel(const QString& channelId);
    void refreshChannels();
    void refreshCenter();
    void refreshVoicePanel();
    void refreshUserPanel();
    void onSpeakingChanged(const QString& userId, bool speaking);
    void showUserMenu(const QString& userId, const QPoint& globalPosition);
    void openSettings();
    void updateIncomingCall(const QString& channelId);
    QImage userPicture(const QString& userId);
    QPixmap memberAvatar(const QString& userId, bool speaking);
    QPixmap privateChannelAvatar(const PrivateChannel& channel, int size, bool loadPicture = true);
    QString locationName(const QString& guildId, const QString& channelId) const;

    Session* m_session;
    VoiceController* m_voice;
    ImageCache* m_images;

    AppShell* m_shell = nullptr;
    ServerRail* m_rail;
    ChannelSidebar* m_sidebar;
    QStackedWidget* m_pages;
    QWidget* m_homePage;
    ChatView* m_chatView;
    VoiceChannelView* m_voiceView;
    IncomingCallWindow* m_incomingCall;
    Notifier* m_notifier;

    QString m_guildId;   // guild shown in the sidebar (empty = direct messages)
    QString m_channelId; // channel shown in the center area
    QTimer m_refreshTimer;
    QTimer m_badgeTimer;
};
