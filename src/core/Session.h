#pragma once

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

private:
    void onDispatch(const QString& event, const QJsonObject& data);
    void loadReady(const QJsonObject& data);
    void loadGuild(const QJsonObject& data);
    void applyVoiceState(const VoiceState& state);
    void applyCallVoiceState(const VoiceState& state);
    void loadCall(const QJsonObject& data);
    void storeUser(const QJsonObject& json);
    void storeMember(const QString& guildId, const QJsonObject& member);
    void requestMissingUsers();

    Gateway* m_gateway;
    RestClient* m_rest;
    QHash<QString, PrivateChannel> m_privateChannels;
    QHash<QString, Call> m_calls; // by private channel ID
    QString m_token;
    User m_self;
    QHash<QString, Guild> m_guilds;
    QStringList m_guildOrder;
    QHash<QString, User> m_users;
    QHash<QString, QSet<QString>> m_missingUsers; // guild ID -> user IDs to request
    QTimer m_missingUsersTimer;
};
