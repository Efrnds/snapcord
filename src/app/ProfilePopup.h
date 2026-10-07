#pragma once

#include "core/Models.h"

#include <QFrame>

#include <optional>

class ImageCache;
class ProfileCard;
class QScrollArea;
class Session;

// Profile popout opened by clicking a user. Appears once the full profile (bio, banner, roles...) and its
// pictures have loaded, or after a short wait with what the Session knows. Stays current with presence updates.
class ProfilePopup : public QFrame
{
    Q_OBJECT

public:
    // `guildId` adds the member's roles and join date for that server (empty for direct messages).
    ProfilePopup(Session* session, ImageCache* images, const QString& userId, const QString& guildId,
                 QWidget* parent = nullptr);

    // Opens next to a point (global coordinates), kept inside the screen.
    void popupAt(const QPoint& position);
    // Opens to the left of a point, top-aligned with it (the member list).
    void popupLeftOf(const QPoint& position);
    // Opens above a widget, aligned to its left edge (the user panel).
    void popupAbove(QWidget* anchor);

signals:
    void editProfileRequested();
    void customStatusRequested();
    void openChannelRequested(const QString& channelId);
    void linkActivated(const QString& url);

private:
    void refresh();
    void fitToContent();
    void showWhenReady();
    QString directMessageId() const;

    Session* m_session;
    ProfileCard* m_card;
    QScrollArea* m_scroll;
    QString m_userId;
    QString m_guildId;
    std::optional<UserProfile> m_profile;
    User m_shownUser;
    QString m_error;
    QPoint m_anchor;
    bool m_above = false;
    bool m_leftOf = false;
    bool m_showRequested = false;
    bool m_waitedEnough = false;
};
