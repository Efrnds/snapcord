#pragma once

#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QWidget>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class UserPanel;
class VoicePanel;

// Channel column: server name on top, channels grouped by category (with the people in each voice
// channel listed under it), then the voice connection panel and the user panel at the bottom.
class ChannelSidebar : public QWidget
{
    Q_OBJECT

public:
    enum class ItemKind { Category, TextChannel, VoiceChannel, VoiceMember, DirectMessage };

    struct Member
    {
        QString userId;
        QString name;
        QColor nameColor;
        QPixmap avatar;
        bool speaking = false;
        bool muted = false;
        bool deafened = false;
    };

    explicit ChannelSidebar(QWidget* parent = nullptr);

    void setTitle(const QString& title);

    // Rebuilding keeps the scroll position and collapsed categories.
    void beginRebuild();
    // `canCreate` shows a "+" on hover that asks to create a channel inside the category.
    void addCategory(const QString& id, const QString& name, bool canCreate = false);
    // `unread` shows the channel in bold with a pill; `mentions` adds a red counter; muted channels are dimmed.
    void addChannel(const QString& id, const QString& name, ItemKind kind, bool unread = false, int mentions = 0,
                    bool muted = false);
    // A direct message or group DM; `inCall` shows the call indicator. Call members can be added under it.
    void addDirectMessage(const QString& id, const QString& name, const QPixmap& avatar, bool inCall, int mentions = 0);
    void addVoiceMember(const Member& member); // listed under the last added voice channel or DM
    void endRebuild();

    void setSelectedChannel(const QString& channelId);
    // The Friends entry above the direct-message list. Hidden while a server is open.
    void setFriendsVisible(bool visible);
    void setFriendsSelected(bool selected);
    // Updates one member row in place (cheaper than a rebuild, since speaking changes are frequent).
    void setMemberSpeaking(const QString& userId, bool speaking);

    UserPanel* userPanel() const { return m_userPanel; }
    VoicePanel* voicePanel() const { return m_voicePanel; }

signals:
    void channelClicked(const QString& channelId, ChannelSidebar::ItemKind kind);
    void friendsSelected();
    void memberContextMenuRequested(const QString& userId, const QPoint& globalPosition);
    void memberClicked(const QString& userId, const QPoint& globalPosition);
    // Right click on a category or channel; an empty `id` means the empty space below the list.
    void channelContextMenuRequested(const QString& id, ChannelSidebar::ItemKind kind, const QPoint& globalPosition);
    // The "+" of a category was clicked.
    void createChannelRequested(const QString& categoryId);

private:
    void onItemClicked(QTreeWidgetItem* item);

    QLabel* m_title;
    QPushButton* m_friends;
    QTreeWidget* m_tree;
    VoicePanel* m_voicePanel;
    UserPanel* m_userPanel;
    QTreeWidgetItem* m_currentCategory = nullptr;
    QTreeWidgetItem* m_currentVoiceChannel = nullptr;
    QSet<QString> m_collapsedCategories;
    QString m_selectedChannel;
    int m_savedScroll = 0;
};
