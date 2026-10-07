#include "core/MessageStore.h"

#include "core/RestClient.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QUrl>

namespace {

constexpr int PageSize = 50;
// Channels kept in memory at once.
constexpr int MaxChannels = 8;
constexpr quint64 DiscordEpochMs = 1420070400000ull;

// A snowflake-shaped nonce lets the client recognize its own message when the Gateway echoes it back.
QString makeNonce()
{
    const quint64 ms = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) - DiscordEpochMs;
    return QString::number(ms << 22);
}

QString emojiPath(const Emoji& emoji)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(emoji.apiName()));
}

const QList<Message> emptyList;

} // namespace

MessageStore::MessageStore(RestClient* rest, QObject* parent)
    : QObject(parent)
    , m_rest(rest)
{
}

MessageStore::Channel* MessageStore::find(const QString& channelId)
{
    const auto it = m_channels.find(channelId);
    return it == m_channels.end() ? nullptr : &it.value();
}

int MessageStore::indexOf(const Channel& channel, const QString& messageId) const
{
    // Recent messages are the most likely targets, so search from the end.
    for (qsizetype i = channel.messages.size() - 1; i >= 0; --i) {
        if (channel.messages[i].id == messageId)
            return static_cast<int>(i);
    }
    return -1;
}

void MessageStore::open(const QString& channelId)
{
    Channel& channel = m_channels[channelId];
    channel.lastUsed = ++m_useCounter;
    evictOldChannels();
    if (!channel.messages.isEmpty() || channel.loading)
        return;

    channel.loading = true;
    m_rest->get(QStringLiteral("/channels/%1/messages?limit=%2").arg(channelId).arg(PageSize),
                [this, channelId](const RestClient::Response& response) {
                    Channel* channel = find(channelId);
                    if (!channel)
                        return;
                    channel->loading = false;
                    const QJsonArray array = response.body.array();
                    // The API returns newest first.
                    QList<Message> loaded;
                    for (qsizetype i = array.size() - 1; i >= 0; --i)
                        loaded.append(Message::fromJson(array.at(i).toObject()));
                    channel->hasOlder = response.ok() && array.size() == PageSize;
                    // Keep messages that arrived over the Gateway while the request was in flight.
                    for (const Message& message : std::as_const(channel->messages)) {
                        if (loaded.isEmpty() || snowflakeLess(loaded.last().id, message.id))
                            loaded.append(message);
                    }
                    channel->messages = loaded;
                    emit reset(channelId);
                });
}

void MessageStore::loadOlder(const QString& channelId)
{
    Channel* channel = find(channelId);
    if (!channel || channel->loading || !channel->hasOlder || channel->messages.isEmpty())
        return;
    channel->loading = true;
    const QString before = channel->messages.first().id;
    m_rest->get(QStringLiteral("/channels/%1/messages?limit=%2&before=%3").arg(channelId).arg(PageSize).arg(before),
                [this, channelId](const RestClient::Response& response) {
                    Channel* channel = find(channelId);
                    if (!channel)
                        return;
                    channel->loading = false;
                    const QJsonArray array = response.body.array();
                    channel->hasOlder = response.ok() && array.size() == PageSize;
                    QList<Message> older;
                    for (qsizetype i = array.size() - 1; i >= 0; --i)
                        older.append(Message::fromJson(array.at(i).toObject()));
                    channel->messages = older + channel->messages;
                    emit olderLoaded(channelId, static_cast<int>(older.size()));
                });
}

void MessageStore::preload(const QString& channelId, const QList<Message>& messages)
{
    Channel& channel = m_channels[channelId];
    channel.messages = messages;
    channel.loading = false;
    channel.hasOlder = false;
    channel.lastUsed = ++m_useCounter;
    emit reset(channelId);
}

bool MessageStore::isLoading(const QString& channelId) const
{
    const auto it = m_channels.constFind(channelId);
    return it != m_channels.cend() && it->loading;
}

bool MessageStore::hasOlder(const QString& channelId) const
{
    const auto it = m_channels.constFind(channelId);
    return it != m_channels.cend() && it->hasOlder;
}

const QList<Message>& MessageStore::messages(const QString& channelId) const
{
    const auto it = m_channels.constFind(channelId);
    return it == m_channels.cend() ? emptyList : it->messages;
}

const Message* MessageStore::message(const QString& channelId, const QString& messageId) const
{
    const auto it = m_channels.constFind(channelId);
    if (it == m_channels.cend())
        return nullptr;
    const int index = indexOf(*it, messageId);
    return index < 0 ? nullptr : &it->messages[index];
}

// A message with files on its way: the files go up first, then the message that refers to them.
struct MessageStore::Upload
{
    QString channelId;
    QString nonce;
    QJsonObject body;
    QList<OutgoingFile> files;
    QList<qint64> sent; // bytes sent per file
    QJsonArray attachments;
    int remaining = 0;
    bool failed = false;

    qint64 totalSize() const
    {
        qint64 total = 0;
        for (const OutgoingFile& file : files)
            total += file.size;
        return std::max<qint64>(total, 1);
    }
};

namespace {

// Opens the bytes of a file to send; null (with `error` set) if the file can no longer be read.
QIODevice* openFile(const OutgoingFile& file, QObject* parent, QString* error)
{
    if (file.path.isEmpty()) {
        auto* buffer = new QBuffer(parent);
        buffer->setData(file.data);
        buffer->open(QIODevice::ReadOnly);
        return buffer;
    }
    auto* device = new QFile(file.path, parent);
    if (!device->open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: %2").arg(file.filename, device->errorString());
        delete device;
        return nullptr;
    }
    return device;
}

// Discord's error text, or the network error, or the HTTP status.
QString errorReason(const RestClient::Response& response)
{
    const QString message = response.body.object().value(u"message").toString();
    if (!message.isEmpty())
        return message;
    return !response.networkError.isEmpty() ? response.networkError : QStringLiteral("HTTP %1").arg(response.status);
}

} // namespace

void MessageStore::send(const QString& channelId, const QString& guildId, const QString& content,
                        const QString& replyToMessageId, const QList<OutgoingFile>& files)
{
    // Show the message right away, greyed out until Discord confirms it.
    Message pending;
    pending.id = makeNonce();
    pending.nonce = pending.id;
    pending.channelId = channelId;
    pending.guildId = guildId;
    pending.author = m_self;
    pending.content = content;
    pending.timestamp = QDateTime::currentDateTime();
    pending.pending = true;
    for (const OutgoingFile& file : files) {
        Attachment attachment;
        attachment.filename = file.filename;
        attachment.size = file.size;
        attachment.contentType = file.contentType;
        pending.attachments.append(attachment);
    }
    if (!files.isEmpty())
        pending.uploadProgress = 0;
    if (!replyToMessageId.isEmpty()) {
        pending.type = Message::Reply;
        pending.referencedMessageId = replyToMessageId;
        if (const Message* original = message(channelId, replyToMessageId)) {
            pending.referencedAuthor = original->author;
            pending.referencedContent = original->content;
        }
    }
    const QString nonce = pending.nonce;
    insertMessage(channelId, pending);

    QJsonObject body{
        {QStringLiteral("content"), content},
        {QStringLiteral("nonce"), nonce},
        {QStringLiteral("tts"), false},
        {QStringLiteral("flags"), 0},
    };
    if (!replyToMessageId.isEmpty()) {
        QJsonObject reference{{QStringLiteral("message_id"), replyToMessageId},
                              {QStringLiteral("channel_id"), channelId}};
        if (!guildId.isEmpty())
            reference.insert(QStringLiteral("guild_id"), guildId);
        body.insert(QStringLiteral("message_reference"), reference);
    }
    if (files.isEmpty()) {
        postMessage(channelId, nonce, body);
        return;
    }

    auto upload = std::make_shared<Upload>();
    upload->channelId = channelId;
    upload->nonce = nonce;
    upload->body = body;
    upload->files = files;
    uploadToCloud(upload);
}

void MessageStore::uploadToCloud(const std::shared_ptr<Upload>& upload)
{
    // Exactly like the official client: ask for one storage URL per file, upload each there, then send the
    // message naming the uploaded files. There is deliberately no other way (no multipart fallback): a
    // request the official client never makes would make this client stand out.
    QJsonArray requested;
    for (qsizetype i = 0; i < upload->files.size(); ++i) {
        requested.append(QJsonObject{
            {QStringLiteral("filename"), upload->files[i].filename},
            {QStringLiteral("file_size"), upload->files[i].size},
            {QStringLiteral("id"), QString::number(i)},
            {QStringLiteral("is_clip"), false},
        });
    }
    m_rest->post(QStringLiteral("/channels/%1/attachments").arg(upload->channelId),
                 QJsonDocument(QJsonObject{{QStringLiteral("files"), requested}}),
                 [this, upload](const RestClient::Response& response) {
        const QJsonArray targets = response.body.object().value(u"attachments").toArray();
        if (!response.ok() || targets.size() != upload->files.size()) {
            markFailed(upload->channelId, upload->nonce, errorReason(response));
            return;
        }
        QList<int> indexes;
        for (const QJsonValue& value : targets) {
            const QJsonValue id = value.toObject().value(u"id");
            const int index = id.isString() ? id.toString().toInt() : id.toInt();
            if (index < 0 || index >= upload->files.size() || indexes.contains(index)) {
                markFailed(upload->channelId, upload->nonce, QStringLiteral("Unexpected upload response"));
                return;
            }
            indexes.append(index);
        }

        upload->sent = QList<qint64>(upload->files.size(), 0);
        upload->remaining = static_cast<int>(targets.size());
        const qint64 total = upload->totalSize();
        for (qsizetype i = 0; i < targets.size(); ++i) {
            const QJsonObject target = targets.at(i).toObject();
            const int index = indexes[i];
            upload->attachments.append(QJsonObject{
                {QStringLiteral("id"), QString::number(index)},
                {QStringLiteral("filename"), upload->files[index].filename},
                {QStringLiteral("uploaded_filename"), target.value(u"upload_filename").toString()},
            });
            QString error;
            QIODevice* device = openFile(upload->files[index], this, &error);
            if (!device) {
                upload->failed = true;
                markFailed(upload->channelId, upload->nonce, error);
                return;
            }
            m_rest->putToStorage(
                QUrl(target.value(u"upload_url").toString()), device, upload->files[index].size,
                [this, upload, index, total](qint64 sent, qint64) {
                    upload->sent[index] = sent;
                    qint64 all = 0;
                    for (qint64 bytes : std::as_const(upload->sent))
                        all += bytes;
                    setUploadProgress(upload->channelId, upload->nonce, static_cast<int>(all * 100 / total));
                },
                [this, upload, device](const RestClient::Response& response) {
                    device->deleteLater();
                    if (upload->failed)
                        return;
                    if (!response.ok()) {
                        upload->failed = true;
                        markFailed(upload->channelId, upload->nonce, errorReason(response));
                        return;
                    }
                    if (--upload->remaining > 0)
                        return;
                    QJsonObject body = upload->body;
                    body.insert(QStringLiteral("attachments"), upload->attachments);
                    body.insert(QStringLiteral("channel_id"), upload->channelId);
                    body.insert(QStringLiteral("type"), 0);
                    body.insert(QStringLiteral("sticker_ids"), QJsonArray());
                    postMessage(upload->channelId, upload->nonce, body);
                });
        }
    });
}

void MessageStore::postMessage(const QString& channelId, const QString& nonce, const QJsonObject& body)
{
    m_rest->post(QStringLiteral("/channels/%1/messages").arg(channelId), QJsonDocument(body),
                 [this, channelId, nonce](const RestClient::Response& response) {
                     onMessagePosted(channelId, nonce, response);
                 });
}

void MessageStore::onMessagePosted(const QString& channelId, const QString& nonce, const RestClient::Response& response)
{
    if (response.ok()) {
        // Usually the Gateway echo already replaced the pending copy; if not, do it now.
        Channel* channel = find(channelId);
        const int index = channel ? indexOf(*channel, nonce) : -1;
        if (index >= 0) {
            channel->messages[index] = Message::fromJson(response.body.object());
            emit changed(channelId, index);
        }
        return;
    }
    markFailed(channelId, nonce, errorReason(response));
}

void MessageStore::setUploadProgress(const QString& channelId, const QString& nonce, int percent)
{
    percent = std::clamp(percent, 0, 100);
    Channel* channel = find(channelId);
    const int index = channel ? indexOf(*channel, nonce) : -1;
    if (index < 0 || !channel->messages[index].pending || channel->messages[index].uploadProgress == percent)
        return;
    channel->messages[index].uploadProgress = percent;
    emit changed(channelId, index);
}

void MessageStore::markFailed(const QString& channelId, const QString& nonce, const QString& reason)
{
    if (Channel* channel = find(channelId)) {
        const int index = indexOf(*channel, nonce);
        if (index >= 0) {
            channel->messages[index].pending = false;
            channel->messages[index].failed = true;
            channel->messages[index].uploadProgress = -1;
            emit changed(channelId, index);
        }
    }
    emit sendFailed(channelId, reason);
}

void MessageStore::edit(const QString& channelId, const QString& messageId, const QString& content)
{
    m_rest->patch(QStringLiteral("/channels/%1/messages/%2").arg(channelId, messageId),
                  QJsonDocument(QJsonObject{{QStringLiteral("content"), content}}), nullptr);
}

void MessageStore::remove(const QString& channelId, const QString& messageId)
{
    m_rest->deleteResource(QStringLiteral("/channels/%1/messages/%2").arg(channelId, messageId), nullptr);
}

void MessageStore::setReaction(const QString& channelId, const QString& messageId, const Emoji& emoji, bool add)
{
    const QString path = QStringLiteral("/channels/%1/messages/%2/reactions/%3/@me").arg(channelId, messageId, emojiPath(emoji));
    if (add)
        m_rest->put(path, nullptr);
    else
        m_rest->deleteResource(path, nullptr);
}

void MessageStore::insertMessage(const QString& channelId, Message message)
{
    Channel* channel = find(channelId);
    if (!channel)
        return;
    // Messages normally arrive in order; keep the list sorted if they don't.
    qsizetype index = channel->messages.size();
    while (index > 0 && snowflakeLess(message.id, channel->messages[index - 1].id))
        --index;
    channel->messages.insert(index, std::move(message));
    emit inserted(channelId, static_cast<int>(index));
}

void MessageStore::evictOldChannels()
{
    while (m_channels.size() > MaxChannels) {
        auto oldest = m_channels.begin();
        for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed)
                oldest = it;
        }
        m_channels.erase(oldest);
    }
}

void MessageStore::handleDispatch(const QString& event, const QJsonObject& data)
{
    const QString channelId = data.value(u"channel_id").toString();
    Channel* channel = find(channelId);
    if (!channel)
        return; // not loaded: nothing to keep in sync

    if (event == u"MESSAGE_CREATE") {
        Message message = Message::fromJson(data);
        // Our own message coming back: replace the pending copy instead of adding a second one.
        if (!message.nonce.isEmpty() && message.author.id == m_self.id) {
            const int pendingIndex = indexOf(*channel, message.nonce);
            if (pendingIndex >= 0) {
                emit aboutToRemove(channelId, pendingIndex);
                channel->messages.removeAt(pendingIndex);
                emit removed(channelId, pendingIndex);
            }
        }
        if (indexOf(*channel, message.id) < 0)
            insertMessage(channelId, std::move(message));
    } else if (event == u"MESSAGE_UPDATE") {
        const int index = indexOf(*channel, data.value(u"id").toString());
        if (index >= 0) {
            channel->messages[index].update(data);
            emit changed(channelId, index);
        }
    } else if (event == u"MESSAGE_DELETE") {
        const int index = indexOf(*channel, data.value(u"id").toString());
        if (index >= 0) {
            emit aboutToRemove(channelId, index);
            channel->messages.removeAt(index);
            emit removed(channelId, index);
        }
    } else if (event == u"MESSAGE_DELETE_BULK") {
        for (const QJsonValue& id : data.value(u"ids").toArray()) {
            const int index = indexOf(*channel, id.toString());
            if (index >= 0) {
                emit aboutToRemove(channelId, index);
                channel->messages.removeAt(index);
                emit removed(channelId, index);
            }
        }
    } else if (event == u"MESSAGE_REACTION_ADD") {
        applyReaction(data, true);
    } else if (event == u"MESSAGE_REACTION_REMOVE") {
        applyReaction(data, false);
    } else if (event == u"MESSAGE_REACTION_REMOVE_ALL" || event == u"MESSAGE_REACTION_REMOVE_EMOJI") {
        const int index = indexOf(*channel, data.value(u"message_id").toString());
        if (index < 0)
            return;
        auto& reactions = channel->messages[index].reactions;
        if (event == u"MESSAGE_REACTION_REMOVE_ALL") {
            reactions.clear();
        } else {
            const Emoji emoji = Emoji::fromJson(data.value(u"emoji").toObject());
            reactions.removeIf([&emoji](const Reaction& reaction) { return reaction.emoji == emoji; });
        }
        emit changed(channelId, index);
    }
}

void MessageStore::applyReaction(const QJsonObject& data, bool add)
{
    const QString channelId = data.value(u"channel_id").toString();
    Channel* channel = find(channelId);
    if (!channel)
        return;
    const int index = indexOf(*channel, data.value(u"message_id").toString());
    if (index < 0)
        return;
    const Emoji emoji = Emoji::fromJson(data.value(u"emoji").toObject());
    const bool byMe = data.value(u"user_id").toString() == m_self.id;
    auto& reactions = channel->messages[index].reactions;
    auto it = std::find_if(reactions.begin(), reactions.end(), [&emoji](const Reaction& r) { return r.emoji == emoji; });
    if (add) {
        if (it == reactions.end()) {
            reactions.append({emoji, 1, byMe});
        } else {
            ++it->count;
            it->me = it->me || byMe;
        }
    } else if (it != reactions.end()) {
        --it->count;
        if (byMe)
            it->me = false;
        if (it->count <= 0)
            reactions.erase(it);
    }
    emit changed(channelId, index);
}
