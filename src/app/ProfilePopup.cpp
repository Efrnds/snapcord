#include "ProfilePopup.h"

#include "ProfileCard.h"
#include "core/Session.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int PopupWidth = 340;
constexpr int ScreenMargin = 12;
// Longest wait for the profile and its pictures before showing what is known.
constexpr int MaxWaitMs = 1500;

} // namespace

ProfilePopup::ProfilePopup(Session* session, ImageCache* images, const QString& userId, const QString& guildId,
                           QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , m_session(session)
    , m_card(new ProfileCard(images))
    , m_scroll(new QScrollArea)
    , m_userId(userId)
    , m_guildId(guildId)
{
    setObjectName(QStringLiteral("profilePopup"));
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(PopupWidth);

    m_scroll->setWidget(m_card);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scroll);

    // Resized in the same pass as the rebuild, so a half-updated card is never on screen.
    connect(m_card, &ProfileCard::contentResized, this, [this] {
        if (isVisible())
            fitToContent();
        else
            showWhenReady();
    });
    connect(m_card, &ProfileCard::headerPicturesChanged, this, &ProfilePopup::showWhenReady);
    connect(m_card, &ProfileCard::linkActivated, this, &ProfilePopup::linkActivated);
    connect(m_card, &ProfileCard::statusRequested, this, [this](UserStatus status) { m_session->setStatus(status); });
    connect(m_card, &ProfileCard::editProfileRequested, this, [this] {
        emit editProfileRequested();
        close();
    });
    connect(m_card, &ProfileCard::customStatusRequested, this, [this] {
        emit customStatusRequested();
        close();
    });
    connect(m_card, &ProfileCard::messageRequested, this, [this] {
        emit openChannelRequested(directMessageId());
        close();
    });

    connect(m_session, &Session::presenceChanged, this, [this](const QString& id) {
        if (id == m_userId)
            refresh();
    });
    // Fires for every batch of members; only a new name or picture for this user matters.
    connect(m_session, &Session::usersChanged, this, [this] {
        const User user = m_session->user(m_userId);
        if (user.globalName != m_shownUser.globalName || user.username != m_shownUser.username
            || user.avatar != m_shownUser.avatar)
            refresh();
    });

    m_session->requestPresence(guildId, userId);
    QPointer<ProfilePopup> guard(this);
    m_session->fetchProfile(userId, guildId, [guard](const UserProfile* profile, const QString& error) {
        if (!guard)
            return;
        if (profile)
            guard->m_profile = *profile;
        guard->m_error = error;
        guard->refresh();
    });
    if (!m_profile && m_error.isEmpty())
        refresh(); // still loading: the basics, in case the wait runs out
    QTimer::singleShot(MaxWaitMs, this, [this] {
        m_waitedEnough = true;
        showWhenReady();
    });
}

void ProfilePopup::showWhenReady()
{
    if (!m_showRequested || isVisible())
        return;
    const bool loaded = (m_profile || !m_error.isEmpty()) && m_card->headerPicturesReady();
    if (!loaded && !m_waitedEnough)
        return;
    fitToContent();
    show();
}

QString ProfilePopup::directMessageId() const
{
    for (const PrivateChannel& channel : m_session->privateChannels()) {
        if (!channel.isGroup() && channel.recipientIds == QStringList{m_userId})
            return channel.id;
    }
    return {};
}

void ProfilePopup::refresh()
{
    const bool self = m_userId == m_session->self().id;
    ProfileCardData card;
    card.user = self ? m_session->self() : (m_profile ? m_profile->user : m_session->user(m_userId));
    m_shownUser = self ? card.user : m_session->user(m_userId);
    card.displayName = card.user.displayName();
    card.presence = m_session->presence(m_userId);
    card.isSelf = self;
    card.canMessage = !self && !directMessageId().isEmpty();
    card.profileLoaded = m_profile.has_value();
    card.error = m_error;

    const Guild* guild = m_guildId.isEmpty() ? nullptr : m_session->guild(m_guildId);
    if (m_profile) {
        if (!m_profile->nick.isEmpty())
            card.displayName = m_profile->nick;
        card.pronouns = m_profile->pronouns;
        card.bio = m_profile->bio;
        card.banner = m_profile->banner;
        if (m_profile->accentColor >= 0)
            card.bannerColor = QColor::fromRgb(QRgb(m_profile->accentColor));
        card.badges = m_profile->badges;
        card.connections = m_profile->connections;
        for (const QString& id : m_profile->mutualGuildIds) {
            if (const Guild* mutual = m_session->guild(id))
                card.mutualServers.append(mutual->name);
        }
        if (guild) {
            card.serverName = guild->name;
            card.joinedServer = m_profile->joinedAt;
            QList<Role> roles;
            for (const QString& id : m_profile->roleIds) {
                if (guild->roles.contains(id) && id != guild->id)
                    roles.append(guild->roles.value(id));
            }
            // Highest role first, like Discord.
            std::sort(roles.begin(), roles.end(), [](const Role& a, const Role& b) { return a.position > b.position; });
            for (const Role& role : roles)
                card.roles.append({role.name, role.color ? QColor::fromRgb(QRgb(role.color)) : QColor()});
        }
    }
    if (card.displayName.isEmpty())
        card.displayName = tr("Unknown user");
    m_card->setData(card);
}

void ProfilePopup::fitToContent()
{
    const QScreen* screen = QGuiApplication::screenAt(m_anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry().adjusted(ScreenMargin, ScreenMargin, -ScreenMargin, -ScreenMargin);

    // Wrapped text makes the height depend on the width.
    const int innerWidth = width() - 2 * frameWidth();
    const int cardHeight = m_card->hasHeightForWidth() ? m_card->heightForWidth(innerWidth) : m_card->sizeHint().height();
    const int contentHeight = cardHeight + 2 * frameWidth();
    const int height = std::min({contentHeight, 640, available.height()});

    QPoint position = m_above ? QPoint(m_anchor.x(), m_anchor.y() - height - 8) : m_anchor;
    position.setX(std::clamp(position.x(), available.left(), available.right() - width()));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - height));
    // One geometry change (not a resize and then a move), followed by a full repaint: moving a window
    // in two steps can leave pieces of the previous frame on screen.
    setGeometry(QRect(position, QSize(width(), height)));
    m_scroll->widget()->update();
    update();
}

void ProfilePopup::popupAt(const QPoint& position)
{
    m_anchor = position + QPoint(12, -40);
    m_above = false;
    m_showRequested = true;
    showWhenReady();
}

void ProfilePopup::popupAbove(QWidget* anchor)
{
    m_anchor = anchor->mapToGlobal(QPoint(8, 0));
    m_above = true;
    m_showRequested = true;
    showWhenReady();
}
