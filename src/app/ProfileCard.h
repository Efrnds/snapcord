#pragma once

#include "core/Models.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWidget>

class ImageCache;
class QLabel;
class QProgressBar;
class QVBoxLayout;

// Everything a profile card shows. The basics come from the Session right away; the rest arrives
// with the profile API a moment later (`profileLoaded`).
struct ProfileCardData
{
    struct RoleChip
    {
        QString name;
        QColor color; // invalid = no color
    };

    User user;
    QString displayName;
    QString pronouns;
    QString bio;
    QString banner;            // image hash of the banner, empty = a plain color
    QColor bannerColor;        // invalid = taken from the avatar
    QImage avatarOverride;     // the editor's preview of a new picture
    bool avatarRemoved = false; // the editor's preview of "Remove Avatar"
    Presence presence;
    QList<ProfileBadge> badges;
    QList<ConnectedAccount> connections;
    QList<RoleChip> roles;
    QString serverName;
    QDateTime joinedServer;
    QStringList mutualServers;
    bool profileLoaded = false;
    QString error;
    bool isSelf = false;
    bool canMessage = false; // a direct message with this user exists
};

// Banner, round avatar and status dot at the top of a profile card.
class ProfileHeader : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileHeader(QWidget* parent = nullptr);

    void setContent(const QImage& banner, const QColor& bannerColor, const QImage& avatar, const QString& name,
                    UserStatus status);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_banner;
    QColor m_bannerColor;
    QImage m_avatar;
    QString m_name;
    UserStatus m_status = UserStatus::Unknown;
};

// A user profile laid out like Discord's profile popout. Used by the popup and as the editor's preview.
class ProfileCard : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileCard(ImageCache* images, QWidget* parent = nullptr);

    void setData(const ProfileCardData& data);
    const ProfileCardData& data() const { return m_data; }
    // The editor preview has no buttons.
    // False while the avatar or banner is still downloading.
    bool headerPicturesReady() const { return !m_headerPicturesMissing; }
    void setActionsVisible(bool visible);

    static QString statusName(UserStatus status);

signals:
    void editProfileRequested();
    void statusRequested(UserStatus status);
    void customStatusRequested();
    void messageRequested();
    void linkActivated(const QString& url);
    void contentResized();
    // The avatar or banner finished downloading.
    void headerPicturesChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct TimedActivity
    {
        Activity activity;
        QLabel* label = nullptr;
        QProgressBar* progress = nullptr;
        QLabel* position = nullptr;
        QLabel* duration = nullptr;
    };

    void rebuild();
    void updateHeader();
    QWidget* buildBody();
    QWidget* buildActivity(const Activity& activity);
    QWidget* buildActions();
    void updateTimes();
    QImage image(const QUrl& url, const QSize& bounds = {});
    QUrl activityImage(const Activity& activity, const QString& asset) const;

    ImageCache* m_images;
    ProfileCardData m_data;
    ProfileHeader* m_header;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_layout;
    QSet<QUrl> m_urls;       // pictures in the body, to rebuild when one of them arrives
    QSet<QUrl> m_headerUrls; // the avatar and banner, which only need a repaint
    bool m_headerPicturesMissing = false;
    QList<TimedActivity> m_timed;
    QTimer m_clock;
    bool m_actionsVisible = true;
    bool m_rebuildPending = false;
};
