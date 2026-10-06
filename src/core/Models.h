#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct User
{
    QString id;
    QString username;
    QString globalName;
    QString avatar;

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
    ChannelType type = ChannelType::GuildText;
    int position = 0;
    int userLimit = 0;
    QList<PermissionOverwrite> overwrites;

    bool isVoice() const { return type == ChannelType::GuildVoice || type == ChannelType::GuildStageVoice; }
    static Channel fromJson(const QJsonObject& json, const QString& guildId);
};

struct Role
{
    QString id;
    quint64 permissions = 0;
    int position = 0;

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

struct Guild
{
    QString id;
    QString name;
    QString icon;
    QString ownerId;
    bool unavailable = false;
    QHash<QString, Channel> channels;
    QHash<QString, Role> roles;
    QStringList selfRoleIds;
    QHash<QString, VoiceState> voiceStates; // by user ID
};

// Snowflake IDs compare numerically; this keeps a stable order for equal positions.
bool snowflakeLess(const QString& a, const QString& b);
