#pragma once

#include <QMainWindow>
#include <QTimer>

class ChannelSidebar;
class ImageCache;
class QLabel;
class QStackedWidget;
class ServerRail;
class Session;
class VoiceChannelView;
class VoiceController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session* session, VoiceController* voice, QWidget* parent = nullptr);

signals:
    void logoutRequested();

private:
    QWidget* buildPlaceholderPage(const QString& title, const QString& subtitle);
    void rebuildServerRail();
    void selectGuild(const QString& guildId);
    void refreshChannels();
    void refreshVoiceView();
    void refreshVoicePanel();
    void refreshUserPanel();
    void onChannelClicked(const QString& channelId, bool isVoice);
    void onSpeakingChanged(const QString& userId, bool speaking);
    void showUserMenu(const QString& userId, const QPoint& globalPosition);
    void openSettings();
    QImage userPicture(const QString& userId);
    QPixmap memberAvatar(const QString& userId, bool speaking);

    Session* m_session;
    VoiceController* m_voice;
    ImageCache* m_images;

    ServerRail* m_rail;
    ChannelSidebar* m_sidebar;
    QStackedWidget* m_pages;
    QWidget* m_homePage;
    QWidget* m_textPage;
    VoiceChannelView* m_voiceView;

    QString m_guildId;   // guild shown in the sidebar (empty = direct messages)
    QString m_channelId; // channel shown in the center area
    QTimer m_refreshTimer;
};
