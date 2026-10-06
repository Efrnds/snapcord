#include "VoiceController.h"

#include "core/Session.h"

#include <QSettings>

VoiceController::VoiceController(Session* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_connection(new VoiceConnection(this))
{
    // Discord remembers mute/deafen across restarts; so does Snapcord.
    QSettings settings;
    m_selfMuted = settings.value(QStringLiteral("voice/selfMuted"), false).toBool();
    m_selfDeafened = settings.value(QStringLiteral("voice/selfDeafened"), false).toBool();
    m_connection->setSelfMuted(m_selfMuted);
    m_connection->setSelfDeafened(m_selfDeafened);

    connect(m_session, &Session::ownVoiceStateChanged, this, &VoiceController::onOwnVoiceState);
    connect(m_session, &Session::voiceServerUpdated, this, &VoiceController::onVoiceServer);
    connect(m_session, &Session::connectionStateChanged, this, [this](bool connected) {
        // After a Gateway reconnect, tell Discord again where we are (the voice connection itself survives).
        if (connected && !m_channelId.isEmpty())
            sendVoiceState();
    });

    connect(m_connection, &VoiceConnection::stateChanged, this, &VoiceController::stateChanged);
    connect(m_connection, &VoiceConnection::pingChanged, this, &VoiceController::pingChanged);
    connect(m_connection, &VoiceConnection::speakingChanged, this, [this](const QString& userId, bool speaking) {
        if (speaking)
            m_speaking.insert(userId);
        else
            m_speaking.remove(userId);
        emit speakingChanged(userId, speaking);
    });
    connect(m_connection, &VoiceConnection::failed, this, [this](const QString& reason) {
        if (!m_channelId.isEmpty()) {
            leave();
            emit errorOccurred(reason);
        }
    });
}

VoiceController::~VoiceController()
{
    if (!m_channelId.isEmpty())
        m_session->updateVoiceState(QString(), QString(), m_selfMuted, m_selfDeafened);
}

void VoiceController::join(const QString& guildId, const QString& channelId)
{
    if (guildId == m_guildId && channelId == m_channelId)
        return;
    if (!m_guildId.isEmpty() && guildId != m_guildId) {
        // Moving to another guild: leave the current call first.
        m_connection->disconnect();
    }
    m_guildId = guildId;
    m_channelId = channelId;
    m_endpoint.clear();
    m_voiceToken.clear();
    m_haveVoiceState = false;
    sendVoiceState();
    emit channelChanged();
}

void VoiceController::leave()
{
    if (m_channelId.isEmpty())
        return;
    m_connection->disconnect();
    m_session->updateVoiceState(QString(), QString(), m_selfMuted, m_selfDeafened);
    m_guildId.clear();
    m_channelId.clear();
    m_endpoint.clear();
    m_voiceToken.clear();
    m_haveVoiceState = false;
    emit channelChanged();
}

void VoiceController::toggleMute()
{
    if (m_selfDeafened)
        setSelfState(false, false);
    else
        setSelfState(!m_selfMuted, false);
}

void VoiceController::toggleDeafen()
{
    if (m_selfDeafened) {
        setSelfState(m_mutedBeforeDeafen, false);
    } else {
        m_mutedBeforeDeafen = m_selfMuted;
        setSelfState(true, true);
    }
}

void VoiceController::setSelfState(bool muted, bool deafened)
{
    m_selfMuted = muted;
    m_selfDeafened = deafened;
    m_connection->setSelfMuted(muted);
    m_connection->setSelfDeafened(deafened);
    QSettings settings;
    settings.setValue(QStringLiteral("voice/selfMuted"), muted);
    settings.setValue(QStringLiteral("voice/selfDeafened"), deafened);
    if (!m_channelId.isEmpty())
        sendVoiceState();
    emit selfStateChanged();
}

float VoiceController::userVolume(const QString& userId) const
{
    return VoiceSettings::userVolume(userId);
}

void VoiceController::setUserVolume(const QString& userId, float volume)
{
    VoiceSettings::setUserVolume(userId, volume);
    m_connection->setUserVolume(userId, volume);
}

void VoiceController::applySettings(const VoiceSettings& settings)
{
    settings.save();
    m_connection->applySettings(settings);
}

void VoiceController::sendVoiceState()
{
    m_session->updateVoiceState(m_guildId, m_channelId, m_selfMuted, m_selfDeafened);
}

void VoiceController::onOwnVoiceState(const VoiceState& state)
{
    if (m_channelId.isEmpty())
        return;
    if (state.channelId.isEmpty()) {
        // Disconnected by someone else (kicked, or the channel was deleted).
        m_connection->disconnect();
        m_guildId.clear();
        m_channelId.clear();
        m_haveVoiceState = false;
        emit channelChanged();
        return;
    }
    if (state.channelId != m_channelId) {
        // Moved to another channel by a moderator; a new voice server update follows.
        m_channelId = state.channelId;
        emit channelChanged();
    }
    m_haveVoiceState = true;
    maybeConnect();
}

void VoiceController::onVoiceServer(const QString& guildId, const QString& endpoint, const QString& token)
{
    if (guildId != m_guildId || m_channelId.isEmpty())
        return;
    m_endpoint = endpoint;
    m_voiceToken = token;
    maybeConnect();
}

void VoiceController::maybeConnect()
{
    // Both the voice state (session ID) and the voice server (endpoint, token) are needed to connect.
    if (!m_haveVoiceState || m_endpoint.isEmpty() || m_voiceToken.isEmpty())
        return;

    VoiceGateway::Credentials credentials;
    credentials.endpoint = m_endpoint;
    credentials.serverId = m_guildId;
    credentials.userId = m_session->self().id;
    credentials.sessionId = m_session->sessionId();
    credentials.token = m_voiceToken;
    // Voice tokens are single-use: a reconnect needs a new voice server update.
    m_voiceToken.clear();
    m_connection->connectTo(credentials, m_channelId);
}
