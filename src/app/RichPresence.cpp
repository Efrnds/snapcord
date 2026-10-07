#include "RichPresence.h"

#include "core/GameDetector.h"
#include "core/Session.h"
#include "core/SpotifyPresence.h"

#include <QJsonObject>
#include <QSettings>

namespace {

constexpr auto GamesKey = "activity/shareGames";
constexpr auto SpotifyKey = "activity/shareSpotify";

// The settings dialog changes these while the app runs.
RichPresence* s_instance = nullptr;

} // namespace

RichPresence::RichPresence(Session* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_games(new GameDetector(this))
    , m_spotify(new SpotifyPresence(session->rest(), this))
{
    s_instance = this;
    connect(m_games, &GameDetector::activityChanged, this,
            [this](const QJsonObject& activity) { m_session->setLocalActivity(QStringLiteral("game"), activity); });
    connect(m_spotify, &SpotifyPresence::activityChanged, this,
            [this](const QJsonObject& activity) { m_session->setLocalActivity(QStringLiteral("spotify"), activity); });
    connect(m_session, &Session::ready, this, [this] {
        // A new READY can mean a new account state; look at the connected Spotify account again.
        m_ready = true;
        m_spotify->stop();
        applySettings();
    });
    connect(m_session, &Session::connectionsChanged, m_spotify, &SpotifyPresence::refreshConnection);
    applySettings();
}

RichPresence::~RichPresence()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void RichPresence::applySettings()
{
    m_games->setEnabled(shareGames());
    if (shareSpotify() && m_ready)
        m_spotify->start(m_session->self().id);
    else
        m_spotify->stop();
}

bool RichPresence::shareGames()
{
    return QSettings().value(QLatin1String(GamesKey), true).toBool();
}

bool RichPresence::shareSpotify()
{
    return QSettings().value(QLatin1String(SpotifyKey), true).toBool();
}

void RichPresence::setShareGames(bool enabled)
{
    QSettings().setValue(QLatin1String(GamesKey), enabled);
    if (s_instance)
        s_instance->applySettings();
}

void RichPresence::setShareSpotify(bool enabled)
{
    QSettings().setValue(QLatin1String(SpotifyKey), enabled);
    if (s_instance)
        s_instance->applySettings();
}
