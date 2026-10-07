#pragma once

#include "core/Models.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct Emoji
{
    QString id;   // empty for Unicode emoji
    QString name; // the Unicode character(s), or the custom emoji name
    bool animated = false;

    bool isCustom() const { return !id.isEmpty(); }
    // The form used in reaction URLs: the Unicode emoji itself, or "name:id".
    QString apiName() const { return isCustom() ? name + u':' + id : name; }
    bool operator==(const Emoji& other) const { return isCustom() ? id == other.id : name == other.name; }

    static Emoji fromJson(const QJsonObject& json);
};

struct Attachment
{
    QString id;
    QString filename;
    QString url;
    QString proxyUrl;
    QString contentType;
    qint64 size = 0;
    int width = 0;
    int height = 0;

    bool isImage() const { return width > 0 && height > 0 && !contentType.startsWith(u"video/"); }
    bool isVideo() const { return width > 0 && height > 0 && contentType.startsWith(u"video/"); }
    // Image or video shown as an inline preview in chat.
    bool isMedia() const { return isImage() || isVideo(); }
};

struct Embed
{
    QString type;
    QString title;
    QString url;
    QString description;
    QString authorName;
    QString providerName;
    QString footer;
    QString imageUrl;         // proxied image or thumbnail (Discord CDN when available)
    QString imageOriginalUrl; // non-proxied source (e.g. i.ytimg.com) — more reliable for previews
    QString videoUrl;         // mp4/webm from gifv / video embeds
    int imageWidth = 0;
    int imageHeight = 0;
    bool imageIsThumbnail = false;
    int color = -1; // -1 = no color bar color

    bool isEmpty() const
    {
        return title.isEmpty() && description.isEmpty() && authorName.isEmpty() && imageUrl.isEmpty()
            && videoUrl.isEmpty();
    }
};

struct Reaction
{
    Emoji emoji;
    int count = 0;
    bool me = false;
};

struct Message
{
    enum Type { Default = 0, RecipientAdd = 1, RecipientRemove = 2, Call = 3, ChannelNameChange = 4,
                ChannelPinnedMessage = 6, UserJoin = 7, Reply = 19, ChatInputCommand = 20 };

    QString id;
    QString channelId;
    QString guildId;
    int type = Default;
    User author;
    QString content;
    QDateTime timestamp;
    QDateTime editedTimestamp;
    QList<Attachment> attachments;
    QList<Embed> embeds;
    QList<Reaction> reactions;
    QStringList stickerNames;
    QStringList mentionedUserIds;
    QStringList mentionedRoleIds;
    bool mentionsEveryone = false;

    // Replies keep a copy of the message they answer.
    QString referencedMessageId;
    User referencedAuthor;
    QString referencedContent;
    bool referencedDeleted = false;

    // Set while a message sent by this client waits for the server to confirm it.
    QString nonce;
    bool pending = false;
    bool failed = false;

    bool isSystemMessage() const { return type != Default && type != Reply && type != ChatInputCommand; }

    static Message fromJson(const QJsonObject& json);
    // Applies a partial MESSAGE_UPDATE payload (only the fields present change).
    void update(const QJsonObject& json);
};
