#include "core/Message.h"

#include <QJsonArray>

namespace {

QString string(const QJsonObject& json, QStringView key)
{
    return json.value(key).toString();
}

QDateTime timestamp(const QJsonValue& value)
{
    // Discord sends UTC ("+00:00"); messages are shown in the user's time zone.
    return value.isString() ? QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toLocalTime() : QDateTime();
}

Attachment attachmentFromJson(const QJsonObject& json)
{
    Attachment attachment;
    attachment.id = string(json, u"id");
    attachment.filename = string(json, u"filename");
    attachment.url = string(json, u"url");
    attachment.proxyUrl = string(json, u"proxy_url");
    attachment.contentType = string(json, u"content_type");
    attachment.size = json.value(u"size").toInteger();
    attachment.width = json.value(u"width").toInt();
    attachment.height = json.value(u"height").toInt();
    return attachment;
}

Embed embedFromJson(const QJsonObject& json)
{
    Embed embed;
    embed.type = string(json, u"type");
    embed.title = string(json, u"title");
    embed.url = string(json, u"url");
    embed.description = string(json, u"description");
    embed.authorName = string(json.value(u"author").toObject(), u"name");
    embed.providerName = string(json.value(u"provider").toObject(), u"name");
    embed.footer = string(json.value(u"footer").toObject(), u"text");
    if (json.contains(u"color"))
        embed.color = json.value(u"color").toInt();

    // Prefer the full image; fall back to the thumbnail. Keep both the Discord proxy and the
    // original URL — YouTube thumbs often load more reliably from i.ytimg.com than images-ext.
    auto takeImage = [&embed](const QJsonObject& image, bool thumbnail) {
        const QString original = string(image, u"url");
        const QString proxy = string(image, u"proxy_url");
        if (proxy.isEmpty() && original.isEmpty())
            return false;
        embed.imageUrl = proxy.isEmpty() ? original : proxy;
        embed.imageOriginalUrl = original;
        embed.imageWidth = image.value(u"width").toInt();
        embed.imageHeight = image.value(u"height").toInt();
        embed.imageIsThumbnail = thumbnail;
        return true;
    };
    if (!takeImage(json.value(u"image").toObject(), false))
        takeImage(json.value(u"thumbnail").toObject(), embed.type != u"image" && embed.type != u"gifv");

    const QJsonObject video = json.value(u"video").toObject();
    if (!video.isEmpty()) {
        embed.videoUrl = video.contains(u"proxy_url") ? string(video, u"proxy_url") : string(video, u"url");
        if (embed.imageWidth <= 0)
            embed.imageWidth = video.value(u"width").toInt();
        if (embed.imageHeight <= 0)
            embed.imageHeight = video.value(u"height").toInt();
    }
    return embed;
}

QList<Reaction> reactionsFromJson(const QJsonArray& array)
{
    QList<Reaction> reactions;
    for (const QJsonValue& value : array) {
        const QJsonObject json = value.toObject();
        Reaction reaction;
        reaction.emoji = Emoji::fromJson(json.value(u"emoji").toObject());
        reaction.count = json.value(u"count").toInt();
        reaction.me = json.value(u"me").toBool();
        reactions.append(reaction);
    }
    return reactions;
}

} // namespace

Emoji Emoji::fromJson(const QJsonObject& json)
{
    Emoji emoji;
    emoji.id = string(json, u"id");
    emoji.name = string(json, u"name");
    emoji.animated = json.value(u"animated").toBool();
    return emoji;
}

QStringList roleIdsOf(const QJsonObject& member)
{
    QStringList ids;
    for (const QJsonValue& role : member.value(u"roles").toArray())
        ids.append(role.toString());
    return ids;
}

Message Message::fromJson(const QJsonObject& json)
{
    Message message;
    message.id = string(json, u"id");
    message.channelId = string(json, u"channel_id");
    message.guildId = string(json, u"guild_id");
    message.nonce = json.value(u"nonce").isString() ? string(json, u"nonce")
                                                     : QString::number(json.value(u"nonce").toInteger());
    message.author = User::fromJson(json.value(u"author").toObject());
    message.update(json);
    if (json.contains(u"member"))
        message.memberRoleIds = roleIdsOf(json.value(u"member").toObject());

    const QJsonObject referenced = json.value(u"referenced_message").toObject();
    if (!referenced.isEmpty()) {
        message.referencedMessageId = string(referenced, u"id");
        message.referencedAuthor = User::fromJson(referenced.value(u"author").toObject());
        if (referenced.contains(u"member"))
            message.referencedMemberRoleIds = roleIdsOf(referenced.value(u"member").toObject());
        message.referencedContent = string(referenced, u"content");
        if (message.referencedContent.isEmpty() && !referenced.value(u"attachments").toArray().isEmpty())
            message.referencedContent = QStringLiteral("📎");
    } else if (json.contains(u"message_reference") && message.type == Reply) {
        // The replied-to message was deleted.
        message.referencedMessageId = string(json.value(u"message_reference").toObject(), u"message_id");
        message.referencedDeleted = true;
    }
    return message;
}

void Message::update(const QJsonObject& json)
{
    if (json.contains(u"type"))
        type = json.value(u"type").toInt();
    if (json.contains(u"content"))
        content = string(json, u"content");
    if (json.contains(u"member"))
        memberRoleIds = roleIdsOf(json.value(u"member").toObject());
    if (json.contains(u"timestamp"))
        timestamp = ::timestamp(json.value(u"timestamp"));
    if (json.contains(u"edited_timestamp"))
        editedTimestamp = ::timestamp(json.value(u"edited_timestamp"));
    if (json.contains(u"attachments")) {
        attachments.clear();
        for (const QJsonValue& value : json.value(u"attachments").toArray())
            attachments.append(attachmentFromJson(value.toObject()));
    }
    if (json.contains(u"embeds")) {
        embeds.clear();
        for (const QJsonValue& value : json.value(u"embeds").toArray()) {
            const Embed embed = embedFromJson(value.toObject());
            if (!embed.isEmpty())
                embeds.append(embed);
        }
    }
    if (json.contains(u"reactions"))
        reactions = reactionsFromJson(json.value(u"reactions").toArray());
    if (json.contains(u"sticker_items")) {
        stickerNames.clear();
        for (const QJsonValue& value : json.value(u"sticker_items").toArray())
            stickerNames.append(string(value.toObject(), u"name"));
    }
    if (json.contains(u"mentions")) {
        mentionedUserIds.clear();
        for (const QJsonValue& value : json.value(u"mentions").toArray())
            mentionedUserIds.append(string(value.toObject(), u"id"));
    }
    if (json.contains(u"mention_roles")) {
        mentionedRoleIds.clear();
        for (const QJsonValue& value : json.value(u"mention_roles").toArray())
            mentionedRoleIds.append(value.toString());
    }
    if (json.contains(u"mention_everyone"))
        mentionsEveryone = json.value(u"mention_everyone").toBool();
}
