#pragma once

#include <QImage>
#include <QList>
#include <QWidget>

class QLabel;
class QPushButton;

// One participant tile in the voice channel view: avatar, name, mute/deafen icons and the speaking border.
class ParticipantTile : public QWidget
{
    Q_OBJECT

public:
    struct Participant
    {
        QString userId;
        QString name;
        QImage picture;
        bool speaking = false;
        bool muted = false;
        bool deafened = false;
    };

    explicit ParticipantTile(QWidget* parent = nullptr);

    void setParticipant(const Participant& participant);
    const Participant& participant() const { return m_participant; }
    void setSpeaking(bool speaking);

signals:
    void contextMenuRequested(const QString& userId, const QPoint& globalPosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    Participant m_participant;
};

// Center area shown when a voice channel is selected: the participants and a "Join Voice" button.
class VoiceChannelView : public QWidget
{
    Q_OBJECT

public:
    explicit VoiceChannelView(QWidget* parent = nullptr);

    void setChannelName(const QString& name);
    void setParticipants(const QList<ParticipantTile::Participant>& participants);
    void setSpeaking(const QString& userId, bool speaking);
    // `joined`: this client is in this channel. `canJoin`: the user has permission to connect.
    void setJoinState(bool joined, bool canJoin);

signals:
    void joinRequested();
    void participantContextMenuRequested(const QString& userId, const QPoint& globalPosition);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void layoutTiles();

    QLabel* m_title;
    QWidget* m_grid;
    QLabel* m_emptyLabel;
    QPushButton* m_joinButton;
    QList<ParticipantTile*> m_tiles;
};
