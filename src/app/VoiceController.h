#pragma once

#include "voice/VoiceConnection.h"

#include <QObject>
#include <QSet>

class Session;
struct VoiceState;

// Joins and leaves voice channels: asks the Gateway for a voice server, then hands the credentials to the
// VoiceConnection. Also keeps the self mute/deafen state in sync between Discord and the audio pipeline.
class VoiceController : public QObject
{
    Q_OBJECT

public:
    explicit VoiceController(Session* session, QObject* parent = nullptr);
    ~VoiceController() override;

    void join(const QString& guildId, const QString& channelId);
    void leave();

    QString guildId() const { return m_guildId; }
    QString channelId() const { return m_channelId; }
    VoiceConnection::State state() const { return m_connection->state(); }
    VoiceConnection* connection() const { return m_connection; }

    bool isSelfMuted() const { return m_selfMuted; }
    bool isSelfDeafened() const { return m_selfDeafened; }
    // Same behavior as Discord: deafening also mutes, undeafening restores the previous mute state,
    // and unmuting while deafened undeafens too.
    void toggleMute();
    void toggleDeafen();

    bool isSpeaking(const QString& userId) const { return m_speaking.contains(userId); }
    float userVolume(const QString& userId) const;
    void setUserVolume(const QString& userId, float volume);

    void applySettings(const VoiceSettings& settings);

signals:
    void channelChanged();
    void stateChanged(VoiceConnection::State state);
    void selfStateChanged();
    void speakingChanged(const QString& userId, bool speaking);
    void pingChanged(int milliseconds);
    void errorOccurred(const QString& message);

private:
    void onOwnVoiceState(const VoiceState& state);
    void onVoiceServer(const QString& guildId, const QString& endpoint, const QString& token);
    void maybeConnect();
    void sendVoiceState();
    void setSelfState(bool muted, bool deafened);

    Session* m_session;
    VoiceConnection* m_connection;
    QString m_guildId;
    QString m_channelId;
    QString m_endpoint;
    QString m_voiceToken;
    bool m_haveVoiceState = false;
    bool m_selfMuted = false;
    bool m_selfDeafened = false;
    bool m_mutedBeforeDeafen = false;
    QSet<QString> m_speaking;
};
