#include "core/MessageStore.h"

#include "core/RestClient.h"

#include <QDateTime>
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

void MessageStore::send(const QString& channelId, const QString& guildId, const QString& content,
                        const QString& replyToMessageId)
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
    m_rest->post(QStringLiteral("/channels/%1/messages").arg(channelId), QJsonDocument(body),
                 [this, channelId, nonce](const RestClient::Response& response) {
                     Channel* channel = find(channelId);
                     if (!channel)
                         return;
                     const int index = indexOf(*channel, nonce);
                     if (response.ok()) {
                         // Usually the Gateway echo already replaced the pending copy; if not, do it now.
                         if (index >= 0) {
                             channel->messages[index] = Message::fromJson(response.body.object());
                             emit changed(channelId, index);
                         }
                         return;
                     }
                     if (index >= 0) {
                         channel->messages[index].pending = false;
                         channel->messages[index].failed = true;
                         emit changed(channelId, index);
                     }
                     const QString reason = response.body.object().value(u"message").toString();
                     emit sendFailed(channelId, reason.isEmpty() ? QStringLiteral("HTTP %1").arg(response.status) : reason);
                 });
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
            channel->messages.removeAt(index);
            emit removed(channelId, index);
        }
    } else if (event == u"MESSAGE_DELETE_BULK") {
        for (const QJsonValue& id : data.value(u"ids").toArray()) {
            const int index = indexOf(*channel, id.toString());
            if (index >= 0) {
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
