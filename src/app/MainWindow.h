#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class ChannelSidebar;
class ChatView;
class ImageCache;
class IncomingCallWindow;
class Notifier;
class ProfilePopup;
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
    // Profile popouts. `guildId` adds the member's roles and server join date; empty in direct messages.
    void showProfile(const QString& userId, const QString& guildId, const QPoint& globalPosition);
    void showOwnProfile();

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
    void connectProfilePopup(ProfilePopup* popup);
    void openProfileEditor();
    void openCustomStatus();
    void updateIncomingCall(const QString& channelId);
    QImage userPicture(const QString& userId);
    QPixmap memberAvatar(const QString& userId);
    // `showStatus` adds the recipient's status dot (one-to-one conversations in the sidebar).
    QPixmap privateChannelAvatar(const PrivateChannel& channel, int size, bool loadPicture = true, bool showStatus = false);
    QString locationName(const QString& guildId, const QString& channelId) const;

    Session* m_session;
    VoiceController* m_voice;
    ImageCache* m_images;
    QPointer<ProfilePopup> m_pendingProfile;

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
