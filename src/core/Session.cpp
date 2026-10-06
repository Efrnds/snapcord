#include "core/Session.h"

#include "core/Gateway.h"
#include "core/Permissions.h"
#include "core/RestClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

Session::Session(QObject* parent)
    : QObject(parent)
    , m_gateway(new Gateway(this))
    , m_rest(new RestClient(this))
    , m_messages(new MessageStore(m_rest, this))
{
    connect(m_gateway, &Gateway::dispatch, this, &Session::onDispatch);
    connect(m_gateway, &Gateway::authenticationFailed, this, &Session::authenticationFailed);
    connect(m_gateway, &Gateway::connectionStateChanged, this, &Session::connectionStateChanged);

    // Unknown users in voice channels are requested in batches instead of one request per event.
    m_missingUsersTimer.setSingleShot(true);
    m_missingUsersTimer.setInterval(250);
    connect(&m_missingUsersTimer, &QTimer::timeout, this, &Session::requestMissingUsers);
}

void Session::start(const QString& token)
{
    m_token = token;
    m_rest->setToken(token);
    m_gateway->start(token);
}

void Session::stop()
{
    m_gateway->stop();
}

QString Session::sessionId() const
{
    return m_gateway->sessionId();
}

const Guild* Session::guild(const QString& id) const
{
    const auto it = m_guilds.constFind(id);
    return it == m_guilds.cend() ? nullptr : &it.value();
}

const Channel* Session::channel(const QString& guildId, const QString& channelId) const
{
    const Guild* g = guild(guildId);
    if (!g)
        return nullptr;
    const auto it = g->channels.constFind(channelId);
    return it == g->channels.cend() ? nullptr : &it.value();
}

QList<Channel> Session::visibleChannels(const QString& guildId) const
{
    const Guild* g = guild(guildId);
    if (!g)
        return {};

    auto byPosition = [](const Channel& a, const Channel& b) {
        // Text-like channels come before voice channels inside the same category, like in Discord.
        if (a.isVoice() != b.isVoice())
            return !a.isVoice();
        if (a.position != b.position)
            return a.position < b.position;
        return snowflakeLess(a.id, b.id);
    };

    QList<Channel> categories;
    QHash<QString, QList<Channel>> children; // by parent ID ("" = no category)
    for (const Channel& channel : g->channels) {
        if (channel.type == ChannelType::GuildCategory) {
            categories.append(channel);
            continue;
        }
        if (!(Permissions::compute(*g, channel, m_self.id) & Permissions::ViewChannel))
            continue;
        children[channel.parentId].append(channel);
    }
    std::sort(categories.begin(), categories.end(), byPosition);

    QList<Channel> result;
    QList<Channel> uncategorized = children.value(QString());
    std::sort(uncategorized.begin(), uncategorized.end(), byPosition);
    result.append(uncategorized);
    for (const Channel& category : categories) {
        QList<Channel> items = children.value(category.id);
        if (items.isEmpty())
            continue; // Discord hides categories with no visible channels
        std::sort(items.begin(), items.end(), byPosition);
        result.append(category);
        result.append(items);
    }
    return result;
}

QList<VoiceState> Session::voiceStatesInChannel(const QString& guildId, const QString& channelId) const
{
    QList<VoiceState> states;
    if (guildId.isEmpty()) {
        if (const Call* c = call(channelId))
            states = c->voiceStates.values();
    } else if (const Guild* g = guild(guildId)) {
        for (const VoiceState& state : g->voiceStates) {
            if (state.channelId == channelId)
                states.append(state);
        }
    }
    std::sort(states.begin(), states.end(), [this](const VoiceState& a, const VoiceState& b) {
        return user(a.userId).displayName().compare(user(b.userId).displayName(), Qt::CaseInsensitive) < 0;
    });
    return states;
}

bool Session::canConnect(const QString& guildId, const QString& channelId) const
{
    if (guildId.isEmpty())
        return m_privateChannels.contains(channelId);
    const Guild* g = guild(guildId);
    const Channel* c = channel(guildId, channelId);
    if (!g || !c)
        return false;
    const quint64 permissions = Permissions::compute(*g, *c, m_self.id);
    return (permissions & Permissions::ViewChannel) && (permissions & Permissions::Connect);
}

User Session::user(const QString& id) const
{
    if (id == m_self.id)
        return m_self;
    User result = m_users.value(id);
    if (result.id.isEmpty())
        result.id = id;
    return result;
}

void Session::updateVoiceState(const QString& guildId, const QString& channelId, bool selfMute, bool selfDeaf)
{
    m_gateway->updateVoiceState(guildId, channelId, selfMute, selfDeaf);
}

QList<PrivateChannel> Session::privateChannels() const
{
    QList<PrivateChannel> channels = m_privateChannels.values();
    std::sort(channels.begin(), channels.end(), [](const PrivateChannel& a, const PrivateChannel& b) {
        // Channels without messages sort by creation time (their ID).
        const QString& left = a.lastMessageId.isEmpty() ? a.id : a.lastMessageId;
        const QString& right = b.lastMessageId.isEmpty() ? b.id : b.lastMessageId;
        return snowflakeLess(right, left);
    });
    return channels;
}

const PrivateChannel* Session::privateChannel(const QString& id) const
{
    const auto it = m_privateChannels.constFind(id);
    return it == m_privateChannels.cend() ? nullptr : &it.value();
}

QString Session::privateChannelName(const PrivateChannel& channel) const
{
    if (!channel.name.isEmpty())
        return channel.name;
    QStringList names;
    for (const QString& id : channel.recipientIds)
        names.append(user(id).displayName());
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

const Call* Session::call(const QString& channelId) const
{
    const auto it = m_calls.constFind(channelId);
    return it == m_calls.cend() ? nullptr : &it.value();
}

bool Session::isRingingSelf(const QString& channelId) const
{
    const Call* c = call(channelId);
    return c && c->ringing.contains(m_self.id);
}

void Session::ringCall(const QString& channelId)
{
    // A null recipient list rings everyone in the channel.
    m_rest->post(QStringLiteral("/channels/%1/call/ring").arg(channelId),
                 QJsonDocument(QJsonObject{{QStringLiteral("recipients"), QJsonValue()}}), nullptr);
}

void Session::declineCall(const QString& channelId)
{
    // Without recipients, this stops ringing the current user only.
    m_rest->post(QStringLiteral("/channels/%1/call/stop-ringing").arg(channelId),
                 QJsonDocument(QJsonObject{{QStringLiteral("recipients"), QJsonValue()}}), nullptr);
}

void Session::onDispatch(const QString& event, const QJsonObject& data)
{
    const QString guildId = data.value(u"guild_id").toString();

    if (event == u"READY") {
        loadReady(data);
    } else if (event == u"READY_SUPPLEMENTAL") {
        for (const QJsonValue& value : data.value(u"guilds").toArray()) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(u"id").toString();
            for (const QJsonValue& state : object.value(u"voice_states").toArray())
                applyVoiceState(VoiceState::fromJson(state.toObject(), id));
            emit voiceStatesChanged(id);
        }
    } else if (event == u"GUILD_CREATE") {
        loadGuild(data);
        const QString id = data.value(u"id").toString();
        if (!m_guildOrder.contains(id)) {
            m_guildOrder.append(id);
            emit guildListChanged();
        }
        emit guildChanged(id);
    } else if (event == u"GUILD_UPDATE") {
        auto it = m_guilds.find(data.value(u"id").toString());
        if (it != m_guilds.end()) {
            it->name = data.value(u"name").toString(it->name);
            it->icon = data.value(u"icon").toString();
            it->ownerId = data.value(u"owner_id").toString(it->ownerId);
            emit guildChanged(it->id);
            emit guildListChanged();
        }
    } else if (event == u"GUILD_DELETE") {
        const QString id = data.value(u"id").toString();
        if (data.value(u"unavailable").toBool()) {
            if (m_guilds.contains(id))
                m_guilds[id].unavailable = true;
        } else {
            m_guilds.remove(id);
            m_guildOrder.removeAll(id);
        }
        emit guildListChanged();
    } else if (event == u"GUILD_ROLE_CREATE" || event == u"GUILD_ROLE_UPDATE") {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end()) {
            const Role role = Role::fromJson(data.value(u"role").toObject());
            it->roles.insert(role.id, role);
            emit guildChanged(guildId);
        }
    } else if (event == u"GUILD_ROLE_DELETE") {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end()) {
            it->roles.remove(data.value(u"role_id").toString());
            emit guildChanged(guildId);
        }
    } else if (event == u"GUILD_MEMBER_UPDATE") {
        storeMember(guildId, data);
        if (data.value(u"user").toObject().value(u"id").toString() == m_self.id)
            emit guildChanged(guildId);
    } else if (event == u"GUILD_MEMBERS_CHUNK") {
        for (const QJsonValue& member : data.value(u"members").toArray())
            storeMember(guildId, member.toObject());
        emit usersChanged();
    } else if ((event == u"CHANNEL_CREATE" || event == u"CHANNEL_UPDATE") && guildId.isEmpty()) {
        for (const QJsonValue& recipient : data.value(u"recipients").toArray())
            storeUser(recipient.toObject());
        const PrivateChannel channel = PrivateChannel::fromJson(data);
        if (channel.type == ChannelType::DirectMessage || channel.type == ChannelType::GroupDirectMessage) {
            m_privateChannels.insert(channel.id, channel);
            emit privateChannelsChanged();
        }
    } else if (event == u"CHANNEL_DELETE" && guildId.isEmpty()) {
        if (m_privateChannels.remove(data.value(u"id").toString()))
            emit privateChannelsChanged();
    } else if (event == u"CHANNEL_RECIPIENT_ADD" || event == u"CHANNEL_RECIPIENT_REMOVE") {
        auto it = m_privateChannels.find(data.value(u"channel_id").toString());
        if (it != m_privateChannels.end()) {
            const QJsonObject userJson = data.value(u"user").toObject();
            storeUser(userJson);
            const QString userId = userJson.value(u"id").toString();
            it->recipientIds.removeAll(userId);
            if (event == u"CHANNEL_RECIPIENT_ADD")
                it->recipientIds.append(userId);
            emit privateChannelsChanged();
        }
    } else if (event == u"MESSAGE_CREATE") {
        onMessageCreate(data);
        m_messages->handleDispatch(event, data);
    } else if (event.startsWith(u"MESSAGE_REACTION") || event == u"MESSAGE_UPDATE" || event == u"MESSAGE_DELETE"
               || event == u"MESSAGE_DELETE_BULK") {
        m_messages->handleDispatch(event, data);
    } else if (event == u"MESSAGE_ACK") {
        const QString channelId = data.value(u"channel_id").toString();
        ReadState& state = m_readStates[channelId];
        state.lastAckedId = data.value(u"message_id").toString();
        if (data.value(u"mention_count").isDouble())
            state.mentionCount = data.value(u"mention_count").toInt();
        else
            state.mentionCount = 0;
        emit readStateChanged(m_privateChannels.contains(channelId) ? QString() : guildId, channelId);
    } else if (event == u"TYPING_START") {
        const QString userId = data.value(u"user_id").toString();
        if (data.contains(u"member"))
            storeMember(guildId, data.value(u"member").toObject());
        if (userId != m_self.id)
            emit typingStarted(data.value(u"channel_id").toString(), userId);
    } else if (event == u"USER_GUILD_SETTINGS_UPDATE") {
        loadGuildSettings(data);
        emit readStateChanged(data.value(u"guild_id").toString(), QString());
    } else if (event == u"GUILD_EMOJIS_UPDATE") {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end()) {
            it->emojis.clear();
            for (const QJsonValue& value : data.value(u"emojis").toArray()) {
                const QJsonObject emoji = value.toObject();
                it->emojis.append({emoji.value(u"id").toString(), emoji.value(u"name").toString(),
                                   emoji.value(u"animated").toBool()});
            }
        }
    } else if (event == u"CALL_CREATE") {
        loadCall(data);
    } else if (event == u"CALL_UPDATE") {
        const QString channelId = data.value(u"channel_id").toString();
        Call& call = m_calls[channelId];
        call.channelId = channelId;
        call.ringing.clear();
        for (const QJsonValue& id : data.value(u"ringing").toArray())
            call.ringing.append(id.toString());
        emit callChanged(channelId);
    } else if (event == u"CALL_DELETE") {
        const QString channelId = data.value(u"channel_id").toString();
        m_calls.remove(channelId);
        emit callChanged(channelId);
    } else if (event == u"CHANNEL_CREATE" || event == u"CHANNEL_UPDATE") {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end()) {
            const Channel channel = Channel::fromJson(data, guildId);
            it->channels.insert(channel.id, channel);
            emit guildChanged(guildId);
        }
    } else if (event == u"CHANNEL_DELETE") {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end()) {
            it->channels.remove(data.value(u"id").toString());
            emit guildChanged(guildId);
        }
    } else if (event == u"VOICE_STATE_UPDATE") {
        if (data.contains(u"member"))
            storeMember(guildId, data.value(u"member").toObject());
        const VoiceState state = VoiceState::fromJson(data, guildId);
        if (guildId.isEmpty()) {
            applyCallVoiceState(state);
        } else {
            applyVoiceState(state);
            emit voiceStatesChanged(guildId);
        }
        if (state.userId == m_self.id && state.sessionId == sessionId())
            emit ownVoiceStateChanged(state);
    } else if (event == u"VOICE_SERVER_UPDATE") {
        // A null endpoint means the voice server is being reallocated; a new update follows.
        const QString endpoint = data.value(u"endpoint").toString();
        if (!endpoint.isEmpty())
            emit voiceServerUpdated(guildId, data.value(u"channel_id").toString(), endpoint,
                                    data.value(u"token").toString());
    } else if (event == u"USER_UPDATE") {
        if (data.value(u"id").toString() == m_self.id) {
            m_self = User::fromJson(data);
            emit usersChanged();
        }
    }
}

void Session::loadReady(const QJsonObject& data)
{
    m_self = User::fromJson(data.value(u"user").toObject());
    m_messages->setSelf(m_self);
    m_guilds.clear();
    m_guildOrder.clear();
    m_privateChannels.clear();
    m_calls.clear();
    m_readStates.clear();
    m_guildSettings.clear();
    loadReadStates(data.value(u"read_state"));
    // Without the versioned capability this is a plain array; with it, an object with "entries".
    const QJsonValue guildSettings = data.value(u"user_guild_settings");
    const QJsonArray settingsEntries = guildSettings.isArray() ? guildSettings.toArray()
                                                               : guildSettings.toObject().value(u"entries").toArray();
    for (const QJsonValue& entry : settingsEntries)
        loadGuildSettings(entry.toObject());

    for (const QJsonValue& value : data.value(u"users").toArray())
        storeUser(value.toObject());

    for (const QJsonValue& value : data.value(u"private_channels").toArray()) {
        const QJsonObject json = value.toObject();
        for (const QJsonValue& recipient : json.value(u"recipients").toArray())
            storeUser(recipient.toObject());
        const PrivateChannel channel = PrivateChannel::fromJson(json);
        if (channel.type == ChannelType::DirectMessage || channel.type == ChannelType::GroupDirectMessage)
            m_privateChannels.insert(channel.id, channel);
    }

    const QJsonArray guilds = data.value(u"guilds").toArray();
    const QJsonArray mergedMembers = data.value(u"merged_members").toArray();
    for (qsizetype i = 0; i < guilds.size(); ++i) {
        const QJsonObject guildJson = guilds.at(i).toObject();
        loadGuild(guildJson);
        const QString id = guildJson.value(u"id").toString();
        for (const QJsonValue& member : mergedMembers.at(i).toArray())
            storeMember(id, member.toObject());
    }

    // Order guilds like the user arranged them in the official client (folders flattened).
    const QJsonArray folders = data.value(u"user_settings").toObject().value(u"guild_folders").toArray();
    for (const QJsonValue& folder : folders) {
        for (const QJsonValue& id : folder.toObject().value(u"guild_ids").toArray()) {
            const QString guildId = id.isString() ? id.toString() : QString::number(id.toInteger());
            if (m_guilds.contains(guildId) && !m_guildOrder.contains(guildId))
                m_guildOrder.append(guildId);
        }
    }
    for (const QJsonValue& value : guilds) {
        const QString id = value.toObject().value(u"id").toString();
        if (!m_guildOrder.contains(id))
            m_guildOrder.append(id);
    }

    emit ready();
    emit guildListChanged();
    emit privateChannelsChanged();
}

void Session::loadReadStates(const QJsonValue& value)
{
    // A plain array, or {"entries": [...]} with the versioned read states capability.
    const QJsonArray entries = value.isArray() ? value.toArray() : value.toObject().value(u"entries").toArray();
    for (const QJsonValue& entry : entries) {
        const QJsonObject json = entry.toObject();
        if (json.value(u"read_state_type").toInt(0) != 0)
            continue; // only channel read states matter here
        ReadState state;
        state.lastAckedId = json.value(u"last_message_id").isString()
            ? json.value(u"last_message_id").toString()
            : QString::number(json.value(u"last_message_id").toInteger());
        state.mentionCount = json.value(u"mention_count").toInt();
        m_readStates.insert(json.value(u"id").toString(), state);
    }
}

void Session::loadGuildSettings(const QJsonObject& json)
{
    GuildSettings settings;
    // A temporary mute ("mute_config" with an end time) still counts while it lasts; Snapcord treats it as muted.
    settings.muted = json.value(u"muted").toBool();
    settings.suppressEveryone = json.value(u"suppress_everyone").toBool();
    settings.suppressRoles = json.value(u"suppress_roles").toBool();
    for (const QJsonValue& value : json.value(u"channel_overrides").toArray()) {
        const QJsonObject override = value.toObject();
        if (override.value(u"muted").toBool())
            settings.mutedChannels.insert(override.value(u"channel_id").toString());
    }
    m_guildSettings.insert(json.value(u"guild_id").toString(), settings);
}

QString Session::lastMessageId(const QString& guildId, const QString& channelId) const
{
    if (guildId.isEmpty()) {
        const PrivateChannel* channel = privateChannel(channelId);
        return channel ? channel->lastMessageId : QString();
    }
    const Channel* c = channel(guildId, channelId);
    return c ? c->lastMessageId : QString();
}

bool Session::isUnread(const QString& guildId, const QString& channelId) const
{
    const QString last = lastMessageId(guildId, channelId);
    if (last.isEmpty())
        return false;
    const auto it = m_readStates.constFind(channelId);
    // Channels never opened have no read state; Discord only shows them as unread once a read state exists.
    if (it == m_readStates.cend())
        return false;
    return snowflakeLess(it->lastAckedId, last);
}

int Session::mentionCount(const QString& channelId) const
{
    return m_readStates.value(channelId).mentionCount;
}

bool Session::isMuted(const QString& guildId, const QString& channelId) const
{
    const auto it = m_guildSettings.constFind(guildId);
    if (it == m_guildSettings.cend())
        return false;
    if (it->muted && !guildId.isEmpty())
        return true;
    if (channelId.isEmpty())
        return false;
    if (it->mutedChannels.contains(channelId))
        return true;
    // A muted category mutes the channels inside it.
    const Channel* c = guildId.isEmpty() ? nullptr : channel(guildId, channelId);
    return c && !c->parentId.isEmpty() && it->mutedChannels.contains(c->parentId);
}

bool Session::guildHasUnread(const QString& guildId) const
{
    const Guild* g = guild(guildId);
    if (!g || isMuted(guildId, QString()))
        return false;
    for (const Channel& c : g->channels) {
        if (c.isVoice() || c.type == ChannelType::GuildCategory || isMuted(guildId, c.id))
            continue;
        if (isUnread(guildId, c.id) && (Permissions::compute(*g, c, m_self.id) & Permissions::ViewChannel))
            return true;
    }
    return false;
}

int Session::guildMentionCount(const QString& guildId) const
{
    const Guild* g = guild(guildId);
    if (!g)
        return 0;
    int total = 0;
    for (const Channel& c : g->channels)
        total += m_readStates.value(c.id).mentionCount;
    return total;
}

int Session::privateMentionCount() const
{
    int total = 0;
    for (const PrivateChannel& channel : m_privateChannels)
        total += m_readStates.value(channel.id).mentionCount;
    return total;
}

void Session::markRead(const QString& guildId, const QString& channelId)
{
    const QString last = lastMessageId(guildId, channelId);
    if (last.isEmpty())
        return;
    ReadState& state = m_readStates[channelId];
    if (!snowflakeLess(state.lastAckedId, last) && state.mentionCount == 0)
        return;
    state.lastAckedId = last;
    state.mentionCount = 0;
    m_rest->post(QStringLiteral("/channels/%1/messages/%2/ack").arg(channelId, last),
                 QJsonDocument(QJsonObject{{QStringLiteral("token"), QJsonValue()}}), nullptr);
    emit readStateChanged(guildId, channelId);
}

void Session::sendTyping(const QString& channelId)
{
    // Discord shows the indicator for about 10 seconds, so one request every 8 seconds is enough.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (channelId == m_lastTypingChannel && now - m_lastTypingSent < 8000)
        return;
    m_lastTypingChannel = channelId;
    m_lastTypingSent = now;
    m_rest->post(QStringLiteral("/channels/%1/typing").arg(channelId), QJsonDocument(QJsonObject()), nullptr);
}

QList<CustomEmoji> Session::customEmojis(const QString& guildId) const
{
    const Guild* g = guild(guildId);
    return g ? g->emojis : QList<CustomEmoji>();
}

bool Session::mentionsSelf(const Message& message) const
{
    if (message.mentionedUserIds.contains(m_self.id))
        return true;
    const GuildSettings settings = m_guildSettings.value(message.guildId);
    if (message.mentionsEveryone && !settings.suppressEveryone)
        return true;
    if (!settings.suppressRoles && !message.guildId.isEmpty()) {
        if (const Guild* g = guild(message.guildId)) {
            for (const QString& role : message.mentionedRoleIds) {
                if (g->selfRoleIds.contains(role))
                    return true;
            }
        }
    }
    return false;
}

void Session::onMessageCreate(const QJsonObject& data)
{
    const Message message = Message::fromJson(data);
    const QString channelId = message.channelId;
    const bool isPrivate = message.guildId.isEmpty();

    if (isPrivate) {
        auto it = m_privateChannels.find(channelId);
        if (it == m_privateChannels.end())
            return;
        it->lastMessageId = message.id;
        emit privateChannelsChanged();
    } else {
        auto guildIt = m_guilds.find(message.guildId);
        if (guildIt == m_guilds.end())
            return;
        auto channelIt = guildIt->channels.find(channelId);
        if (channelIt != guildIt->channels.end())
            channelIt->lastMessageId = message.id;
    }

    ReadState& state = m_readStates[channelId];
    if (message.author.id == m_self.id) {
        // Sending a message reads the channel up to it.
        state.lastAckedId = message.id;
        state.mentionCount = 0;
    } else if (mentionsSelf(message) || (isPrivate && !isMuted(QString(), channelId))) {
        ++state.mentionCount;
        emit notificationMessage(message);
    }
    emit readStateChanged(message.guildId, channelId);
}

void Session::loadCall(const QJsonObject& data)
{
    Call call;
    call.channelId = data.value(u"channel_id").toString();
    for (const QJsonValue& id : data.value(u"ringing").toArray())
        call.ringing.append(id.toString());
    for (const QJsonValue& value : data.value(u"voice_states").toArray()) {
        const QJsonObject json = value.toObject();
        if (json.contains(u"member"))
            storeUser(json.value(u"member").toObject().value(u"user").toObject());
        const VoiceState state = VoiceState::fromJson(json, QString());
        if (!state.userId.isEmpty())
            call.voiceStates.insert(state.userId, state);
    }
    m_calls.insert(call.channelId, call);
    emit callChanged(call.channelId);
}

void Session::applyCallVoiceState(const VoiceState& state)
{
    // A user is in at most one call: drop them from wherever they were, then add them to the new one.
    for (auto it = m_calls.begin(); it != m_calls.end(); ++it) {
        if (it.key() != state.channelId && it->voiceStates.remove(state.userId))
            emit callChanged(it.key());
    }
    if (state.channelId.isEmpty())
        return;
    Call& call = m_calls[state.channelId];
    call.channelId = state.channelId;
    call.voiceStates.insert(state.userId, state);
    emit callChanged(state.channelId);
}

void Session::loadGuild(const QJsonObject& data)
{
    // With the CLIENT_STATE_V2 capability the guild fields live in "properties"; without it they are merged in.
    const QJsonObject properties = data.contains(u"properties") ? data.value(u"properties").toObject() : data;

    Guild guild;
    guild.id = data.value(u"id").toString();
    guild.name = properties.value(u"name").toString();
    guild.icon = properties.value(u"icon").toString();
    guild.ownerId = properties.value(u"owner_id").toString();
    guild.unavailable = data.value(u"unavailable").toBool();

    for (const QJsonValue& value : data.value(u"roles").toArray()) {
        const Role role = Role::fromJson(value.toObject());
        guild.roles.insert(role.id, role);
    }
    for (const QJsonValue& value : data.value(u"channels").toArray()) {
        const Channel channel = Channel::fromJson(value.toObject(), guild.id);
        guild.channels.insert(channel.id, channel);
    }
    const QJsonArray emojis = data.contains(u"emojis") ? data.value(u"emojis").toArray()
                                                       : properties.value(u"emojis").toArray();
    for (const QJsonValue& value : emojis) {
        const QJsonObject emoji = value.toObject();
        guild.emojis.append({emoji.value(u"id").toString(), emoji.value(u"name").toString(),
                             emoji.value(u"animated").toBool()});
    }
    m_guilds.insert(guild.id, guild);

    for (const QJsonValue& member : data.value(u"members").toArray())
        storeMember(guild.id, member.toObject());
    for (const QJsonValue& state : data.value(u"voice_states").toArray())
        applyVoiceState(VoiceState::fromJson(state.toObject(), guild.id));
}

void Session::applyVoiceState(const VoiceState& state)
{
    auto it = m_guilds.find(state.guildId);
    if (it == m_guilds.end() || state.userId.isEmpty())
        return;
    if (state.channelId.isEmpty()) {
        it->voiceStates.remove(state.userId);
        return;
    }
    it->voiceStates.insert(state.userId, state);
    if (state.userId != m_self.id && !m_users.contains(state.userId)) {
        m_missingUsers[state.guildId].insert(state.userId);
        m_missingUsersTimer.start();
    }
}

void Session::storeUser(const QJsonObject& json)
{
    const User user = User::fromJson(json);
    if (!user.id.isEmpty())
        m_users.insert(user.id, user);
}

void Session::storeMember(const QString& guildId, const QJsonObject& member)
{
    const QJsonObject userJson = member.value(u"user").toObject();
    if (!userJson.isEmpty())
        storeUser(userJson);
    const QString userId = userJson.isEmpty() ? member.value(u"user_id").toString() : userJson.value(u"id").toString();
    if (userId == m_self.id && !m_self.id.isEmpty()) {
        auto it = m_guilds.find(guildId);
        if (it != m_guilds.end() && member.contains(u"roles")) {
            it->selfRoleIds.clear();
            for (const QJsonValue& role : member.value(u"roles").toArray())
                it->selfRoleIds.append(role.toString());
        }
    }
}

void Session::requestMissingUsers()
{
    for (auto it = m_missingUsers.cbegin(); it != m_missingUsers.cend(); ++it) {
        QStringList ids;
        for (const QString& id : it.value()) {
            if (!m_users.contains(id))
                ids.append(id);
        }
        // The Gateway accepts up to 100 user IDs per request.
        for (qsizetype i = 0; i < ids.size(); i += 100)
            m_gateway->requestGuildMembers(it.key(), ids.mid(i, 100));
    }
    m_missingUsers.clear();
}
