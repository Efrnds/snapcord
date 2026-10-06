#pragma once

#include "core/Message.h"
#include "core/MessageStore.h"
#include "core/Models.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

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

signals:
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
};
