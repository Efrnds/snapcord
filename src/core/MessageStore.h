#pragma once

#include "core/Message.h"

#include <QHash>
#include <QObject>

class RestClient;

// Messages of the channels the user has opened. Only a few channels are kept in memory (least recently
// opened ones are dropped) and only the most recent messages are fetched; older ones load on demand.
class MessageStore : public QObject
{
    Q_OBJECT

public:
    MessageStore(RestClient* rest, QObject* parent = nullptr);

    void setSelf(const User& self) { m_self = self; }

    // Starts loading a channel's recent messages if they are not in memory yet.
    void open(const QString& channelId);
    void loadOlder(const QString& channelId);
    // Puts a channel's complete history in memory without asking the server (demo mode).
    void preload(const QString& channelId, const QList<Message>& messages);
    bool isLoading(const QString& channelId) const;
    bool hasOlder(const QString& channelId) const;
    const QList<Message>& messages(const QString& channelId) const;
    const Message* message(const QString& channelId, const QString& messageId) const;

    void send(const QString& channelId, const QString& guildId, const QString& content, const QString& replyToMessageId);
    void edit(const QString& channelId, const QString& messageId, const QString& content);
    void remove(const QString& channelId, const QString& messageId);
    void setReaction(const QString& channelId, const QString& messageId, const Emoji& emoji, bool add);

    // Gateway events concerning messages.
    void handleDispatch(const QString& event, const QJsonObject& data);

signals:
    void reset(const QString& channelId);              // the whole list changed (first load)
    void olderLoaded(const QString& channelId, int count); // `count` messages were prepended
    void inserted(const QString& channelId, int index);
    void changed(const QString& channelId, int index);
    void removed(const QString& channelId, int index);
    void sendFailed(const QString& channelId, const QString& reason);

private:
    struct Channel
    {
        QList<Message> messages; // oldest first
        bool loading = false;
        bool hasOlder = true;
        quint64 lastUsed = 0;
    };

    Channel* find(const QString& channelId);
    int indexOf(const Channel& channel, const QString& messageId) const;
    void insertMessage(const QString& channelId, Message message);
    void evictOldChannels();
    void applyReaction(const QJsonObject& data, bool add);

    RestClient* m_rest;
    User m_self;
    QHash<QString, Channel> m_channels;
    quint64 m_useCounter = 0;
};
