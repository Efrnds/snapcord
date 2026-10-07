#include "core/Models.h"

#include <QJsonArray>
#include <QVariant>

namespace {

QString string(const QJsonObject& json, QStringView key)
{
    return json.value(key).toString();
}

quint64 permissionBits(const QJsonValue& value)
{
    // Permissions are serialized as strings because they don't fit in a JSON double.
    return value.isString() ? value.toString().toULongLong() : static_cast<quint64>(value.toDouble());
}

} // namespace

User User::fromJson(const QJsonObject& json)
{
    User user;
    user.id = string(json, u"id");
    user.username = string(json, u"username");
    user.globalName = string(json, u"global_name");
    user.avatar = string(json, u"avatar");
    // Discord stores accent as a 24-bit RGB integer (or null).
    if (json.contains(u"accent_color") && !json.value(u"accent_color").isNull()) {
        user.hasAccentColor = true;
        user.accentColorRgb = static_cast<quint32>(json.value(u"accent_color").toVariant().toULongLong()) & 0x00ffffffu;
    }
    if (json.contains(u"banner_color") && !json.value(u"banner_color").isNull()) {
        const QString hex = json.value(u"banner_color").toString();
        if (hex.startsWith(u'#'))
            user.bannerColorHex = hex;
    }
    const QJsonArray themeColors = json.value(u"theme_colors").toArray();
    if (themeColors.size() >= 1 && user.bannerColorHex.isEmpty()) {
        const quint32 rgb = static_cast<quint32>(themeColors.at(0).toVariant().toULongLong()) & 0x00ffffffu;
        user.bannerColorHex = QStringLiteral("#%1").arg(rgb, 6, 16, QLatin1Char('0'));
    }
    if (themeColors.size() >= 2 && !user.hasAccentColor) {
        user.hasAccentColor = true;
        user.accentColorRgb = static_cast<quint32>(themeColors.at(1).toVariant().toULongLong()) & 0x00ffffffu;
    }
    return user;
}

Channel Channel::fromJson(const QJsonObject& json, const QString& guildId)
{
    Channel channel;
    channel.id = string(json, u"id");
    channel.guildId = json.contains(u"guild_id") ? string(json, u"guild_id") : guildId;
    channel.parentId = string(json, u"parent_id");
    channel.name = string(json, u"name");
    channel.topic = string(json, u"topic");
    channel.lastMessageId = string(json, u"last_message_id");
    channel.type = static_cast<ChannelType>(json.value(u"type").toInt());
    channel.position = json.value(u"position").toInt();
    channel.userLimit = json.value(u"user_limit").toInt();
    for (const QJsonValue& value : json.value(u"permission_overwrites").toArray()) {
        const QJsonObject object = value.toObject();
        PermissionOverwrite overwrite;
        overwrite.id = string(object, u"id");
        overwrite.type = object.value(u"type").toInt() == 1 ? PermissionOverwrite::Member : PermissionOverwrite::Role;
        overwrite.allow = permissionBits(object.value(u"allow"));
        overwrite.deny = permissionBits(object.value(u"deny"));
        channel.overwrites.append(overwrite);
    }
    return channel;
}

Role Role::fromJson(const QJsonObject& json)
{
    Role role;
    role.id = string(json, u"id");
    role.name = string(json, u"name");
    role.permissions = permissionBits(json.value(u"permissions"));
    role.position = json.value(u"position").toInt();
    role.color = json.value(u"color").toInt();
    return role;
}

VoiceState VoiceState::fromJson(const QJsonObject& json, const QString& guildId)
{
    VoiceState state;
    state.userId = json.contains(u"user_id") ? string(json, u"user_id")
                                              : string(json.value(u"member").toObject().value(u"user").toObject(), u"id");
    state.guildId = json.contains(u"guild_id") ? string(json, u"guild_id") : guildId;
    state.channelId = string(json, u"channel_id");
    state.sessionId = string(json, u"session_id");
    state.mute = json.value(u"mute").toBool();
    state.deaf = json.value(u"deaf").toBool();
    state.selfMute = json.value(u"self_mute").toBool();
    state.selfDeaf = json.value(u"self_deaf").toBool();
    state.selfStream = json.value(u"self_stream").toBool();
    state.selfVideo = json.value(u"self_video").toBool();
    state.suppress = json.value(u"suppress").toBool();
    return state;
}

PrivateChannel PrivateChannel::fromJson(const QJsonObject& json)
{
    PrivateChannel channel;
    channel.id = string(json, u"id");
    channel.type = static_cast<ChannelType>(json.value(u"type").toInt());
    channel.name = string(json, u"name");
    channel.icon = string(json, u"icon");
    channel.lastMessageId = string(json, u"last_message_id");
    // Full user objects normally; only IDs when the READY payload is deduplicated.
    for (const QJsonValue& recipient : json.value(u"recipients").toArray())
        channel.recipientIds.append(string(recipient.toObject(), u"id"));
    for (const QJsonValue& id : json.value(u"recipient_ids").toArray())
        channel.recipientIds.append(id.toString());
    return channel;
}

UserStatus statusFromString(const QString& text)
{
    if (text == u"online")
        return UserStatus::Online;
    if (text == u"idle")
        return UserStatus::Idle;
    if (text == u"dnd")
        return UserStatus::DoNotDisturb;
    if (text == u"invisible")
        return UserStatus::Invisible;
    if (text == u"offline")
        return UserStatus::Offline;
    return UserStatus::Unknown;
}

QString statusToString(UserStatus status)
{
    switch (status) {
    case UserStatus::Online:
        return QStringLiteral("online");
    case UserStatus::Idle:
        return QStringLiteral("idle");
    case UserStatus::DoNotDisturb:
        return QStringLiteral("dnd");
    case UserStatus::Invisible:
        return QStringLiteral("invisible");
    case UserStatus::Offline:
        return QStringLiteral("offline");
    case UserStatus::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

Activity Activity::fromJson(const QJsonObject& json)
{
    Activity activity;
    activity.type = json.value(u"type").toInt();
    activity.name = string(json, u"name");
    activity.details = string(json, u"details");
    activity.state = string(json, u"state");
    activity.url = string(json, u"url");
    activity.applicationId = string(json, u"application_id");
    activity.syncId = string(json, u"sync_id");
    const QJsonObject timestamps = json.value(u"timestamps").toObject();
    // Usually numbers, but some clients send them as strings.
    auto time = [&](QStringView key) {
        const QJsonValue value = timestamps.value(key);
        return value.isString() ? value.toString().toLongLong() : value.toInteger();
    };
    activity.start = time(u"start");
    activity.end = time(u"end");
    const QJsonObject assets = json.value(u"assets").toObject();
    activity.largeImage = string(assets, u"large_image");
    activity.largeText = string(assets, u"large_text");
    activity.smallImage = string(assets, u"small_image");
    activity.smallText = string(assets, u"small_text");
    const QJsonObject emoji = json.value(u"emoji").toObject();
    activity.emojiName = string(emoji, u"name");
    activity.emojiId = string(emoji, u"id");
    activity.emojiAnimated = emoji.value(u"animated").toBool();
    const QJsonArray size = json.value(u"party").toObject().value(u"size").toArray();
    activity.partySize = size.at(0).toInt();
    activity.partyMax = size.at(1).toInt();
    return activity;
}

Activity Activity::customStatus(const QString& text, const QString& emojiName, const QString& emojiId)
{
    Activity activity;
    activity.type = Custom;
    activity.name = QStringLiteral("Custom Status");
    activity.state = text;
    activity.emojiName = emojiName;
    activity.emojiId = emojiId;
    return activity;
}

QJsonObject Activity::toJson() const
{
    QJsonObject json{{QStringLiteral("type"), type}, {QStringLiteral("name"), name}};
    if (!state.isEmpty())
        json.insert(QStringLiteral("state"), state);
    if (!details.isEmpty())
        json.insert(QStringLiteral("details"), details);
    if (!emojiName.isEmpty() || !emojiId.isEmpty()) {
        QJsonObject emoji{{QStringLiteral("name"), emojiName}};
        if (!emojiId.isEmpty()) {
            emoji.insert(QStringLiteral("id"), emojiId);
            emoji.insert(QStringLiteral("animated"), emojiAnimated);
        }
        json.insert(QStringLiteral("emoji"), emoji);
    }
    return json;
}

const Activity* Presence::customStatus() const
{
    for (const Activity& activity : activities) {
        if (activity.type == Activity::Custom)
            return &activity;
    }
    return nullptr;
}

Presence Presence::fromJson(const QJsonObject& json)
{
    Presence presence;
    presence.status = statusFromString(string(json, u"status"));
    for (const QJsonValue& value : json.value(u"activities").toArray())
        presence.activities.append(Activity::fromJson(value.toObject()));
    return presence;
}

CustomStatus CustomStatus::fromJson(const QJsonValue& value)
{
    const QJsonObject json = value.toObject();
    CustomStatus status;
    status.text = string(json, u"text");
    status.emojiName = string(json, u"emoji_name");
    status.emojiId = json.value(u"emoji_id").isString() ? string(json, u"emoji_id") : QString();
    if (json.value(u"expires_at").isString())
        status.expiresAt = QDateTime::fromString(string(json, u"expires_at"), Qt::ISODateWithMs);
    return status;
}

UserProfile UserProfile::fromJson(const QJsonObject& json)
{
    UserProfile profile;
    const QJsonObject userJson = json.value(u"user").toObject();
    profile.user = User::fromJson(userJson);
    const QJsonObject details = json.value(u"user_profile").toObject();
    profile.bio = details.contains(u"bio") ? string(details, u"bio") : string(userJson, u"bio");
    profile.pronouns = string(details, u"pronouns");
    profile.banner = details.value(u"banner").isString() ? string(details, u"banner") : string(userJson, u"banner");
    const QJsonValue accent = details.value(u"accent_color").isDouble() ? details.value(u"accent_color")
                                                                        : userJson.value(u"accent_color");
    if (accent.isDouble())
        profile.accentColor = accent.toInt();
    if (json.value(u"premium_since").isString())
        profile.premiumSince = QDateTime::fromString(string(json, u"premium_since"), Qt::ISODateWithMs);

    for (const QJsonValue& value : json.value(u"badges").toArray()) {
        const QJsonObject badge = value.toObject();
        profile.badges.append({string(badge, u"id"), string(badge, u"description"), string(badge, u"icon"),
                               string(badge, u"link")});
    }
    for (const QJsonValue& value : json.value(u"connected_accounts").toArray()) {
        const QJsonObject account = value.toObject();
        profile.connections.append({string(account, u"type"), string(account, u"name"),
                                    account.value(u"verified").toBool()});
    }
    for (const QJsonValue& value : json.value(u"mutual_guilds").toArray())
        profile.mutualGuildIds.append(string(value.toObject(), u"id"));

    const QJsonObject member = json.value(u"guild_member").toObject();
    if (!member.isEmpty()) {
        profile.nick = string(member, u"nick");
        for (const QJsonValue& role : member.value(u"roles").toArray())
            profile.roleIds.append(role.toString());
        profile.joinedAt = QDateTime::fromString(string(member, u"joined_at"), Qt::ISODateWithMs);
    }
    // A server profile overrides the bio and pronouns where it sets them.
    const QJsonObject memberProfile = json.value(u"guild_member_profile").toObject();
    profile.guildId = string(memberProfile, u"guild_id");
    if (!string(memberProfile, u"bio").isEmpty())
        profile.bio = string(memberProfile, u"bio");
    if (!string(memberProfile, u"pronouns").isEmpty())
        profile.pronouns = string(memberProfile, u"pronouns");
    return profile;
}

QDateTime snowflakeTime(const QString& id)
{
    constexpr qint64 DiscordEpoch = 1420070400000;
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(id.toULongLong() >> 22) + DiscordEpoch, QTimeZone::UTC);
}

bool snowflakeLess(const QString& a, const QString& b)
{
    if (a.size() != b.size())
        return a.size() < b.size();
    return a < b;
}
