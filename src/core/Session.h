#pragma once

#include "core/Message.h"
#include "core/MessageStore.h"
#include "core/Models.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <optional>

class Gateway;
class RestClient;

// Logged-in account state built from Gateway events: the user, their guilds, channels and voice states.
class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject* parent = nullptr);

    void start(const QString& token);
    // Demo mode: never connects; the state comes from these Gateway events (name, payload) instead.
    void startOffline(const QList<std::pair<QString, QJsonObject>>& events);
    void stop();

    QString token() const { return m_token; }
    const User& self() const { return m_self; }
    QString sessionId() const;

    QStringList guildOrder() const { return m_guildOrder; }
    const Guild* guild(const QString& id) const;
    const Channel* channel(const QString& guildId, const QString& channelId) const;
    // Channels the user can see, sorted the way Discord shows them (categories with their children).
    QList<Channel> visibleChannels(const QString& guildId) const;
    QList<VoiceState> voiceStatesInChannel(const QString& guildId, const QString& channelId) const;
    bool canConnect(const QString& guildId, const QString& channelId) const;

    User user(const QString& id) const;

    // Direct messages and group DMs, most recently active first.
    QList<PrivateChannel> privateChannels() const;
    const PrivateChannel* privateChannel(const QString& id) const;
    QString privateChannelName(const PrivateChannel& channel) const;
    const Call* call(const QString& channelId) const;
    bool isRingingSelf(const QString& channelId) const;
    // Rings the other recipients of a private channel's call / stops ringing this user.
    void ringCall(const QString& channelId);
    void declineCall(const QString& channelId);

    // Voice state of this client (opcode 4). An empty channelId disconnects from voice;
    // an empty guildId targets a private channel call.
    void updateVoiceState(const QString& guildId, const QString& channelId, bool selfMute, bool selfDeaf);

    MessageStore* messages() const { return m_messages; }

    // Unread messages and mentions. `guildId` is empty for private channels.
    bool isUnread(const QString& guildId, const QString& channelId) const;
    int mentionCount(const QString& channelId) const;
    bool guildHasUnread(const QString& guildId) const;
    int guildMentionCount(const QString& guildId) const;
    int privateMentionCount() const;
    bool isMuted(const QString& guildId, const QString& channelId) const;
    // Marks a channel as read up to its latest message.
    void markRead(const QString& guildId, const QString& channelId);
    // Tells others that the user is typing (lasts ~10 seconds on their side).
    void sendTyping(const QString& channelId);
    // Custom emojis of every guild, for the emoji picker.
    QList<CustomEmoji> customEmojis(const QString& guildId) const;

    // Status and activities. Unknown users have UserStatus::Unknown; the current user's presence
    // combines their settings (status, custom status) with what their other sessions are doing.
    Presence presence(const QString& userId) const;
    UserStatus selfStatus() const { return m_selfStatus; }
    CustomStatus selfCustomStatus() const { return m_selfCustomStatus; }
    // Asks the Gateway for a guild member's presence if none is known yet (presenceChanged follows).
    void requestPresence(const QString& guildId, const QString& userId);

    // Guild members seen so far (user ID -> nickname, empty when none), for mention suggestions.
    QHash<QString, QString> knownMembers(const QString& guildId) const { return m_guildMembers.value(guildId); }
    // Asks the Gateway for members whose name starts with `query`; they arrive later (usersChanged).
    void searchGuildMembers(const QString& guildId, const QString& query);

    // Member sidebar of a guild channel. `lastRow` is the last row on screen, so the rows around it get
    // filled in while scrolling. Asking again for the same rows sends nothing.
    void subscribeMemberList(const QString& guildId, const QString& channelId, int lastRow = 0);
    // The row ranges asked for, as opcode 37 expects them: [[0, 99], [100, 199], ...].
    static QJsonArray memberListRanges(int lastRow);
    // Null until the Gateway has sent that channel's list.
    const MemberList* memberList(const QString& guildId, const QString& channelId) const;

    // Full profile (bio, banner, badges, mutual servers...). With a guild, it also has the member's
    // roles and join date. Cached for a few minutes. `profile` is null on failure, with an error text.
    using ProfileCallback = std::function<void(const UserProfile* profile, const QString& error)>;
    void fetchProfile(const QString& userId, const QString& guildId, ProfileCallback callback);
    // Demo mode: profiles that never expire and never touch the network.
    void cacheProfile(const UserProfile& profile);

    // Changes to the current user's profile; fields left empty are not touched.
    struct ProfileChanges
    {
        std::optional<QString> globalName;
        std::optional<QString> pronouns;
        std::optional<QString> bio;
        std::optional<int> accentColor; // -1 = back to the default color
        std::optional<QString> avatar;  // data URI of the new picture, empty = remove the picture
    };
    // `error` is empty on success.
    using ResultCallback = std::function<void(const QString& error)>;
    void updateProfile(const ProfileChanges& changes, ResultCallback callback);
    void setStatus(UserStatus status);
    void setCustomStatus(const CustomStatus& status);
    // Activities this client detects itself (the game being played, the Spotify song), shared with the
    // status. `source` keeps them apart ("game", "spotify"); an empty activity removes that source's one.
    void setLocalActivity(const QString& source, const QJsonObject& activity);

    RestClient* rest() const { return m_rest; }

signals:
    // `statusChanged` is false when only the activities changed (e.g. the next song).
    void presenceChanged(const QString& userId, bool statusChanged);
    void memberListChanged(const QString& guildId);
    // The current user's profile was edited (from this or another client).
    void selfProfileChanged();
    void ready();
    void guildListChanged();
    void guildChanged(const QString& guildId);
    void voiceStatesChanged(const QString& guildId);
    void privateChannelsChanged();
    void callChanged(const QString& channelId);
    void usersChanged();
    void ownVoiceStateChanged(const VoiceState& state);
    // For private calls, guildId is empty and channelId identifies the call.
    void voiceServerUpdated(const QString& guildId, const QString& channelId, const QString& endpoint,
                            const QString& token);
    void connectionStateChanged(bool connected);
    void authenticationFailed();
    void readStateChanged(const QString& guildId, const QString& channelId);
    void typingStarted(const QString& channelId, const QString& userId);
    // A message that deserves a notification: a direct message or a mention.
    void notificationMessage(const Message& message);
    // Connected accounts (Spotify, Steam...) were added, removed or changed.
    void connectionsChanged();

private:
    void onDispatch(const QString& event, const QJsonObject& data);
    void loadReady(const QJsonObject& data);
    void loadGuild(const QJsonObject& data);
    void applyVoiceState(const VoiceState& state);
    void applyCallVoiceState(const VoiceState& state);
    void onMessageCreate(const QJsonObject& data);
    void loadReadStates(const QJsonValue& value);
    void loadGuildSettings(const QJsonObject& json);
    bool mentionsSelf(const Message& message) const;
    QString lastMessageId(const QString& guildId, const QString& channelId) const;
    void loadCall(const QJsonObject& data);
    void storeUser(const QJsonObject& json);
    void storeMember(const QString& guildId, const QJsonObject& member);
    void requestMissingUsers();
    void storePresence(const QJsonObject& json);
    void onMemberListUpdate(const QJsonObject& data);
    MemberListItem memberListItem(const QString& guildId, const QJsonObject& json);
    void loadSessions(const QJsonArray& sessions);
    void sendOwnPresence();
    void patchSettings(const QJsonObject& changes);
    void forgetOwnProfile();

    struct CachedProfile
    {
        UserProfile profile;
        qint64 fetchedAt = 0; // 0 = never expires
    };

    Gateway* m_gateway;
    RestClient* m_rest;
    MessageStore* m_messages;
    QHash<QString, PrivateChannel> m_privateChannels;
    QHash<QString, Call> m_calls; // by private channel ID
    QHash<QString, ReadState> m_readStates; // by channel ID
    QHash<QString, GuildSettings> m_guildSettings; // by guild ID ("" = direct messages)
    qint64 m_lastTypingSent = 0;
    QString m_lastTypingChannel;
    QString m_token;
    User m_self;
    QHash<QString, Guild> m_guilds;
    QStringList m_guildOrder;
    QHash<QString, User> m_users;
    QHash<QString, QSet<QString>> m_missingUsers; // guild ID -> user IDs to request
    QTimer m_missingUsersTimer;
    QHash<QString, Presence> m_presences; // by user ID
    QSet<QString> m_requestedPresences;
    UserStatus m_selfStatus = UserStatus::Online;
    CustomStatus m_selfCustomStatus;
    QList<Activity> m_selfActivities; // from the user's sessions, without the custom status
    QMap<QString, QJsonObject> m_localActivities; // by source, in a stable order
    QTimer m_presenceTimer; // groups activity changes that come close together into one update
    bool m_gatewayReady = false;
    QHash<QString, CachedProfile> m_profiles; // by "userId/guildId"
    // Member lists of every guild opened in this session. The Gateway keeps a guild subscribed (and keeps
    // sending its updates) after the user leaves it, and does not send the list again on coming back.
    struct GuildMemberLists
    {
        QHash<QString, MemberList> lists;        // by list ID, once the Gateway sent it in full
        QHash<QString, QString> channelLists;    // channel ID -> list ID
    };
    QHash<QString, GuildMemberLists> m_memberLists; // by guild ID
    QHash<QString, QHash<QString, QString>> m_guildMembers; // guild ID -> user ID -> nickname
    QString m_listGuildId;   // the channel on screen
    QString m_listChannelId;
    QJsonArray m_listRanges;
};
