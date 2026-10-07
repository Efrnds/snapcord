#pragma once

#include <QObject>

class GameDetector;
class Session;
class SpotifyPresence;

// Shares what the user is doing in their status: the game being played and the song playing on Spotify.
// Each can be turned off in the settings (Activity Privacy).
class RichPresence : public QObject
{
    Q_OBJECT

public:
    // Switched off for now by the project owner: nothing is detected or shared, and the settings page is hidden.
    static constexpr bool Enabled = false;

    explicit RichPresence(Session* session, QObject* parent = nullptr);
    ~RichPresence() override;

    static bool shareGames();
    static bool shareSpotify();
    static void setShareGames(bool enabled);
    static void setShareSpotify(bool enabled);

private:
    void applySettings();

    Session* m_session;
    GameDetector* m_games;
    SpotifyPresence* m_spotify;
    bool m_ready = false;
};
