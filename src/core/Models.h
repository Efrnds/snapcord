#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// A friend, a block, or a pending request. `type` matches Discord: 1 friend, 2 blocked, 3 incoming, 4 outgoing.
struct Relationship
{
    enum Type { Friend = 1, Blocked = 2, Incoming = 3, Outgoing = 4 };

    QString userId;
    int type = 0;
};

struct User
{
    QString id;
    QString username;
    QString globalName;
    QString avatar;
    // Discord Nitro profile colors (core stays QtGui-free: store raw RGB / hex).
    bool hasAccentColor = false;
    quint32 accentColorRgb = 0; // 0xRRGGBB
    QString bannerColorHex;     // "#rrggbb" when present
    int premiumType = 0;        // 0 none, 1 Nitro Classic, 2 Nitro, 3 Nitro Basic (only known for the current user)

    QString displayName() const { return globalName.isEmpty() ? username : globalName; }
    static User fromJson(const QJsonObject& json);
};

enum class ChannelType {
    GuildText = 0,
    DirectMessage = 1,
    GuildVoice = 2,
    GroupDirectMessage = 3,
    GuildCategory = 4,
    GuildAnnouncement = 5,
    GuildStageVoice = 13,
    GuildForum = 15,
    GuildMedia = 16,
};

struct PermissionOverwrite
{
    enum Type { Role = 0, Member = 1 };

    QString id;
    Type type = Role;
    quint64 allow = 0;
    quint64 deny = 0;
};

struct Channel
{
    QString id;
    QString guildId;
    QString parentId;
    QString name;
    QString topic;
    QString lastMessageId;
    ChannelType type = ChannelType::GuildText;
    int position = 0;
    int userLimit = 0;
    int bitrate = 0;          // voice channels, bits per second
    int rateLimitPerUser = 0; // slowmode, seconds
    bool nsfw = false;
    QList<PermissionOverwrite> overwrites;

    bool isVoice() const { return type == ChannelType::GuildVoice || type == ChannelType::GuildStageVoice; }
    static Channel fromJson(const QJsonObject& json, const QString& guildId);
};

struct Role
{
    QString id;
    QString name;
    quint64 permissions = 0;
    int position = 0;
    int color = 0; // 0xRRGGBB, 0 = no color
    bool mentionable = false;
    bool managed = false; // integration or bot role; Discord does not let members assign it

    static Role fromJson(const QJsonObject& json);
};

struct VoiceState
{
    QString userId;
    QString guildId;
    QString channelId;
    QString sessionId;
    bool mute = false;
    bool deaf = false;
    bool selfMute = false;
    bool selfDeaf = false;
    bool selfStream = false;
    bool selfVideo = false;
    bool suppress = false;

    static VoiceState fromJson(const QJsonObject& json, const QString& guildId);
};

// A direct message or group DM.
struct PrivateChannel
{
    QString id;
    ChannelType type = ChannelType::DirectMessage;
    QString name; // group DMs only; empty means "list the recipients"
    QString icon;
    QStringList recipientIds;
    QString lastMessageId;

    bool isGroup() const { return type == ChannelType::GroupDirectMessage; }
    static PrivateChannel fromJson(const QJsonObject& json);
};

// An ongoing voice call in a private channel.
struct Call
{
    QString channelId;
    QStringList ringing;                    // users being rung
    QHash<QString, VoiceState> voiceStates; // by user ID
};

struct CustomEmoji
{
    QString id;
    QString name;
    bool animated = false;
};

struct Guild
{
    QString id;
    QString name;
    QString icon;
    QString ownerId;
    int premiumTier = 0; // server boost level, 0-3
    bool unavailable = false;
    QHash<QString, Channel> channels;
    QHash<QString, Role> roles;
    QStringList selfRoleIds;
    QHash<QString, VoiceState> voiceStates; // by user ID
    QList<CustomEmoji> emojis;
};

// Read position in a channel: messages after lastAckedId are unread.
struct ReadState
{
    QString lastAckedId;
    int mentionCount = 0;
};

// Per-guild notification preferences (direct messages use an empty guild ID).
struct GuildSettings
{
    bool muted = false;
    bool suppressEveryone = false;
    bool suppressRoles = false;
    QSet<QString> mutedChannels;
};

// Online status of a user. `Unknown` means "no presence received yet".
enum class UserStatus { Unknown, Online, Idle, DoNotDisturb, Invisible, Offline };

UserStatus statusFromString(const QString& text);
QString statusToString(UserStatus status);

// Something a user is doing: a game, music, a stream, or their custom status.
struct Activity
{
    enum Type { Playing = 0, Streaming = 1, Listening = 2, Watching = 3, Custom = 4, Competing = 5, Hang = 6 };

    int type = Playing;
    QString name;
    QString details;
    QString state;
    QString url;
    QString applicationId;
    QString syncId; // Spotify track ID
    QString largeImage;
    QString largeText;
    QString smallImage;
    QString smallText;
    QString emojiName; // custom status emoji: the Unicode character, or the name of a custom emoji
    QString emojiId;
    bool emojiAnimated = false;
    qint64 start = 0; // milliseconds since the epoch, 0 = unknown
    qint64 end = 0;
    int partySize = 0;
    int partyMax = 0;

    bool isSpotify() const { return type == Listening && !syncId.isEmpty() && name == u"Spotify"; }
    static Activity fromJson(const QJsonObject& json);
    // A custom status activity, as sent in presence updates.
    static Activity customStatus(const QString& text, const QString& emojiName, const QString& emojiId);
    QJsonObject toJson() const;
};

struct Presence
{
    UserStatus status = UserStatus::Unknown;
    QList<Activity> activities;

    const Activity* customStatus() const;
    static Presence fromJson(const QJsonObject& json);
};

// The current user's own custom status, from their settings.
struct CustomStatus
{
    QString text;
    QString emojiName;
    QString emojiId;
    QDateTime expiresAt; // invalid = never

    bool isEmpty() const { return text.isEmpty() && emojiName.isEmpty() && emojiId.isEmpty(); }
    bool isActive() const { return !isEmpty() && (!expiresAt.isValid() || expiresAt > QDateTime::currentDateTimeUtc()); }
    static CustomStatus fromJson(const QJsonValue& json);
};

struct ProfileBadge
{
    QString id;
    QString description;
    QString icon;
    QString link;
};

struct ConnectedAccount
{
    QString type; // "steam", "spotify", "github"...
    QString name;
    bool verified = false;
};

// What GET /users/{id}/profile returns: the extended user profile, plus the guild member when asked for.
struct UserProfile
{
    User user;
    QString bio;
    QString pronouns;
    QString banner;  // image hash, empty = no banner image
    int accentColor = -1; // banner color as 0xRRGGBB, -1 = none
    QDateTime premiumSince;
    QList<ProfileBadge> badges;
    QList<ConnectedAccount> connections;
    QStringList mutualGuildIds;
    // Guild-specific part (only when requested with a guild).
    QString guildId;
    QString nick;
    QStringList roleIds;
    QDateTime joinedAt;

    static UserProfile fromJson(const QJsonObject& json);
};

// One row of a guild member list: a group header ("online", "offline" or a hoisted role ID with its
// member count), a member, or a row the Gateway has not sent yet (both IDs empty).
struct MemberListItem
{
    QString groupId;
    int groupCount = 0;
    QString userId;
    QString nick;
    QStringList roleIds;

    bool isGroup() const { return !groupId.isEmpty(); }
    bool isMember() const { return !userId.isEmpty(); }
};

// The member sidebar of a guild channel, kept in sync by GUILD_MEMBER_LIST_UPDATE. Every channel whose
// members can see it the same way shares one list; only the subscribed ranges of rows are filled in.
struct MemberList
{
    QString id;
    QString guildId;
    int memberCount = 0;
    int onlineCount = 0;
    QList<MemberListItem> items;
};

// When an account was created, from its snowflake ID.
QDateTime snowflakeTime(const QString& id);

// Snowflake IDs compare numerically; this keeps a stable order for equal positions.
bool snowflakeLess(const QString& a, const QString& b);
