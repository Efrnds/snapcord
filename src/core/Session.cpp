#include "core/Session.h"

#include "core/Gateway.h"
#include "core/Permissions.h"

#include <QJsonArray>

#include <algorithm>

Session::Session(QObject* parent)
    : QObject(parent)
    , m_gateway(new Gateway(this))
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
    if (const Guild* g = guild(guildId)) {
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
        applyVoiceState(state);
        if (!guildId.isEmpty())
            emit voiceStatesChanged(guildId);
        if (state.userId == m_self.id && state.sessionId == sessionId())
            emit ownVoiceStateChanged(state);
    } else if (event == u"VOICE_SERVER_UPDATE") {
        // A null endpoint means the voice server is being reallocated; a new update follows.
        const QString endpoint = data.value(u"endpoint").toString();
        if (!endpoint.isEmpty())
            emit voiceServerUpdated(guildId, endpoint, data.value(u"token").toString());
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
    m_guilds.clear();
    m_guildOrder.clear();

    for (const QJsonValue& value : data.value(u"users").toArray())
        storeUser(value.toObject());

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
