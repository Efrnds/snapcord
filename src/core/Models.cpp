#include "core/Models.h"

#include <QJsonArray>

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
    return user;
}

Channel Channel::fromJson(const QJsonObject& json, const QString& guildId)
{
    Channel channel;
    channel.id = string(json, u"id");
    channel.guildId = json.contains(u"guild_id") ? string(json, u"guild_id") : guildId;
    channel.parentId = string(json, u"parent_id");
    channel.name = string(json, u"name");
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
    role.permissions = permissionBits(json.value(u"permissions"));
    role.position = json.value(u"position").toInt();
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

bool snowflakeLess(const QString& a, const QString& b)
{
    if (a.size() != b.size())
        return a.size() < b.size();
    return a < b;
}
