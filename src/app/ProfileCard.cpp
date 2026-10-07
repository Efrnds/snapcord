#include "ProfileCard.h"

#include "Avatar.h"
#include "ImageCache.h"
#include "Theme.h"
#include "core/Markdown.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int BannerHeight = 106;
constexpr int AvatarSize = 80;
constexpr int AvatarBorder = 6;
constexpr int ActivityImageSize = 64;

QLabel* makeLabel(const QString& text, const char* objectName, bool wrap = true)
{
    auto* label = new QLabel(text);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(wrap);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel* sectionTitle(const QString& text)
{
    return makeLabel(text.toUpper(), "profileSection", false);
}

QString formatDuration(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms / 1000);
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds / 60) % 60;
    const qint64 s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// A rounded square picture (or a letter on a colored square when there is none).
QPixmap roundedPicture(const QImage& picture, const QString& fallback, int size, qreal dpr)
{
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), 8, 8);
    painter.setClipPath(clip);
    if (!picture.isNull()) {
        painter.drawImage(QRectF(0, 0, size, size), picture);
    } else {
        painter.fillRect(QRectF(0, 0, size, size), Theme::instance().palette().button);
        QFont font = painter.font();
        font.setPixelSize(size / 2);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, fallback.left(1).toUpper());
    }
    return pixmap;
}

} // namespace

// --- ProfileHeader ----------------------------------------------------------------------------------

ProfileHeader::ProfileHeader(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(BannerHeight + AvatarSize / 2 + 8);
}

void ProfileHeader::setContent(const QImage& banner, const QColor& bannerColor, const QImage& avatar,
                               const QString& name, UserStatus status)
{
    m_banner = banner;
    m_bannerColor = bannerColor;
    m_avatar = avatar;
    m_name = name;
    m_status = status;
    update();
}

void ProfileHeader::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QColor background = Theme::instance().palette().bg3;

    const QRectF bannerRect(0, 0, width(), BannerHeight);
    QPainterPath top;
    top.addRoundedRect(bannerRect.adjusted(0, 0, 0, 8), 8, 8);
    painter.setClipPath(top);
    if (!m_banner.isNull()) {
        // Cover the banner area, cropping the picture's excess.
        const QSizeF scaled = QSizeF(m_banner.size()).scaled(bannerRect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(bannerRect.center().x() - scaled.width() / 2, bannerRect.center().y() - scaled.height() / 2,
                            scaled.width(), scaled.height());
        painter.drawImage(target, m_banner);
    } else {
        QColor color = m_bannerColor;
        if (!color.isValid() && !m_avatar.isNull())
            color = m_avatar.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
        if (!color.isValid())
            color = Theme::instance().accent();
        painter.fillRect(bannerRect, color);
    }
    painter.setClipping(false);

    const QRectF ring(16 - AvatarBorder, BannerHeight - AvatarSize / 2 - AvatarBorder, AvatarSize + 2 * AvatarBorder,
                      AvatarSize + 2 * AvatarBorder);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(ring);
    const QPixmap avatar = makeAvatar(m_name, m_avatar, AvatarSize, devicePixelRatioF());
    painter.drawPixmap(QPointF(16, BannerHeight - AvatarSize / 2), avatar);

    if (m_status != UserStatus::Unknown) {
        const qreal dot = 28;
        drawStatusDot(painter, QRectF(16 + AvatarSize - dot + 4, BannerHeight + AvatarSize / 2 - dot + 4, dot, dot),
                      m_status, background);
    }
}

// --- ProfileCard ------------------------------------------------------------------------------------

ProfileCard::ProfileCard(ImageCache* images, QWidget* parent)
    : QWidget(parent)
    , m_images(images)
    , m_header(new ProfileHeader)
    , m_layout(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("profileCard"));
    setAttribute(Qt::WA_StyledBackground);
    m_layout->setContentsMargins(0, 0, 0, 16);
    m_layout->setSpacing(0);
    m_layout->addWidget(m_header);

    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &ProfileCard::updateTimes);
    connect(m_images, &ImageCache::imageLoaded, this, [this](const QUrl& url) {
        if (m_headerUrls.contains(url)) {
            updateHeader();
            emit headerPicturesChanged();
        }
        if (!m_urls.contains(url) || m_rebuildPending)
            return;
        // Several pictures tend to arrive together; rebuild once.
        m_rebuildPending = true;
        QTimer::singleShot(0, this, [this] {
            m_rebuildPending = false;
            rebuild();
        });
    });
    connect(&Theme::instance(), &Theme::changed, this, &ProfileCard::rebuild);
}

void ProfileCard::setData(const ProfileCardData& data)
{
    m_data = data;
    rebuild();
}

void ProfileCard::setActionsVisible(bool visible)
{
    m_actionsVisible = visible;
    rebuild();
}

QString ProfileCard::statusName(UserStatus status)
{
    switch (status) {
    case UserStatus::Online:
        return tr("Online");
    case UserStatus::Idle:
        return tr("Idle");
    case UserStatus::DoNotDisturb:
        return tr("Do Not Disturb");
    case UserStatus::Invisible:
        return tr("Invisible");
    case UserStatus::Offline:
        return tr("Offline");
    case UserStatus::Unknown:
        break;
    }
    return {};
}

void ProfileCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_timed.isEmpty())
        m_clock.start();
}

void ProfileCard::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_clock.stop();
}

QImage ProfileCard::image(const QUrl& url, const QSize& bounds)
{
    if (url.isEmpty())
        return {};
    m_urls.insert(url);
    return bounds.isEmpty() ? m_images->image(url) : m_images->image(url, bounds);
}

QUrl ProfileCard::activityImage(const Activity& activity, const QString& asset) const
{
    if (asset.isEmpty())
        return {};
    if (asset.startsWith(u"mp:"))
        return QUrl(QStringLiteral("https://media.discordapp.net/") + asset.mid(3));
    if (asset.startsWith(u"spotify:"))
        return QUrl(QStringLiteral("https://i.scdn.co/image/") + asset.mid(8));
    if (asset.startsWith(u"http://") || asset.startsWith(u"https://"))
        return QUrl(asset);
    if (activity.applicationId.isEmpty() || asset.contains(u':'))
        return {}; // Twitch and YouTube previews are not worth the extra requests
    return QUrl(QStringLiteral("https://cdn.discordapp.com/app-assets/%1/%2.png").arg(activity.applicationId, asset));
}

void ProfileCard::updateHeader()
{
    // Collected apart from the body's pictures: the header has a fixed size, so these never change the layout.
    const QSet<QUrl> bodyUrls = std::exchange(m_urls, {});
    QImage avatar;
    if (!m_data.avatarOverride.isNull())
        avatar = m_data.avatarOverride;
    else if (!m_data.avatarRemoved)
        avatar = image(ImageCache::avatarUrl(m_data.user));
    else {
        User plain = m_data.user;
        plain.avatar.clear();
        avatar = image(ImageCache::avatarUrl(plain));
    }
    const QImage banner = m_data.banner.isEmpty()
        ? QImage()
        : image(QUrl(QStringLiteral("https://cdn.discordapp.com/banners/%1/%2.png?size=600").arg(m_data.user.id, m_data.banner)),
                QSize(600, 240));
    m_headerUrls = std::exchange(m_urls, bodyUrls);
    m_headerPicturesMissing = (!m_headerUrls.isEmpty() && avatar.isNull() && m_data.avatarOverride.isNull())
        || (!m_data.banner.isEmpty() && banner.isNull());
    m_header->setContent(banner, m_data.bannerColor, avatar, m_data.displayName, m_data.presence.status);
}

void ProfileCard::rebuild()
{
    m_urls.clear();
    m_timed.clear();
    updateHeader();

    // Swap the body without ever painting the old and new ones together: the old one is hidden at once
    // (deleted later, as this may run from one of its own buttons), and the new one gets its geometry
    // before the next paint.
    setUpdatesEnabled(false);
    if (m_body) {
        m_layout->removeWidget(m_body);
        m_body->hide();
        m_body->deleteLater();
    }
    m_body = buildBody();
    // Never let a long word or row widen the card past the popup: it wraps or clips inside instead.
    m_body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_layout->addWidget(m_body);
    // Shown now rather than on Qt's deferred show: until then a hidden body counts as empty, and the
    // popup would fit itself to the header alone.
    m_body->show();
    m_layout->activate();
    setUpdatesEnabled(true);

    if (isVisible() && !m_timed.isEmpty())
        m_clock.start();
    else if (m_timed.isEmpty())
        m_clock.stop();
    updateTimes();
    emit contentResized();
}

QWidget* ProfileCard::buildBody()
{
    auto* body = new QWidget;
    auto* outer = new QVBoxLayout(body);
    outer->setContentsMargins(16, 0, 16, 0);
    outer->setSpacing(12);

    // Names, badges and the custom status.
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    names->addWidget(makeLabel(m_data.displayName, "profileName"));
    QString username = m_data.user.username;
    if (!m_data.pronouns.isEmpty())
        username += QStringLiteral(" • ") + m_data.pronouns;
    names->addWidget(makeLabel(username, "profileUsername"));

    if (!m_data.badges.isEmpty()) {
        auto* badges = new QHBoxLayout;
        badges->setSpacing(4);
        for (const ProfileBadge& badge : m_data.badges) {
            auto* icon = new QLabel;
            icon->setFixedSize(20, 20);
            icon->setToolTip(badge.description);
            const QImage picture = image(QUrl(QStringLiteral("https://cdn.discordapp.com/badge-icons/%1.png").arg(badge.icon)));
            if (!picture.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(picture).scaled(QSize(20, 20) * devicePixelRatioF(), Qt::KeepAspectRatio,
                                                                   Qt::SmoothTransformation);
                pixmap.setDevicePixelRatio(devicePixelRatioF());
                icon->setPixmap(pixmap);
            }
            badges->addWidget(icon);
        }
        badges->addStretch();
        names->addSpacing(4);
        names->addLayout(badges);
    }

    if (const Activity* custom = m_data.presence.customStatus()) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        if (!custom->emojiId.isEmpty()) {
            auto* emoji = new QLabel;
            emoji->setFixedSize(18, 18);
            const QImage picture = image(QUrl(Markdown::customEmojiUrl(custom->emojiId, custom->emojiAnimated)));
            if (!picture.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(picture).scaled(QSize(18, 18) * devicePixelRatioF(),
                                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
                pixmap.setDevicePixelRatio(devicePixelRatioF());
                emoji->setPixmap(pixmap);
            }
            row->addWidget(emoji, 0, Qt::AlignTop);
        }
        QString text = custom->state;
        if (custom->emojiId.isEmpty() && !custom->emojiName.isEmpty())
            text = custom->emojiName + (text.isEmpty() ? QString() : QStringLiteral(" ") + text);
        row->addWidget(makeLabel(text, "profileText"), 1);
        names->addSpacing(6);
        names->addLayout(row);
    }
    outer->addLayout(names);

    // The rest sits in a darker box, like Discord's popout.
    auto* box = new QWidget;
    box->setObjectName(QStringLiteral("profileBox"));
    box->setAttribute(Qt::WA_StyledBackground);
    auto* content = new QVBoxLayout(box);
    content->setContentsMargins(12, 12, 12, 12);
    content->setSpacing(6);
    auto addSection = [content](const QString& title) {
        if (content->count() > 0)
            content->addSpacing(8);
        content->addWidget(sectionTitle(title));
    };

    if (!m_data.bio.isEmpty()) {
        addSection(tr("About Me"));
        Markdown::Context context;
        context.selfUserId = m_data.isSelf ? m_data.user.id : QString();
        QLabel* bio = makeLabel(Markdown::toHtml(m_data.bio, context).html, "profileText");
        bio->setTextFormat(Qt::RichText);
        bio->setTextInteractionFlags(Qt::TextBrowserInteraction);
        connect(bio, &QLabel::linkActivated, this, &ProfileCard::linkActivated);
        content->addWidget(bio);
    }

    for (const Activity& activity : m_data.presence.activities) {
        if (activity.type == Activity::Custom)
            continue;
        if (content->count() > 0)
            content->addSpacing(8);
        content->addWidget(buildActivity(activity));
    }

    const QDateTime created = snowflakeTime(m_data.user.id);
    if (created.isValid() && !m_data.user.id.isEmpty()) {
        addSection(tr("Member Since"));
        const QLocale locale;
        QString text = tr("Discord: %1").arg(locale.toString(created.toLocalTime().date(), QLocale::LongFormat));
        if (m_data.joinedServer.isValid())
            text += u'\n' + tr("%1: %2").arg(m_data.serverName, locale.toString(m_data.joinedServer.toLocalTime().date(),
                                                                                QLocale::LongFormat));
        content->addWidget(makeLabel(text, "profileText"));
    }

    if (!m_data.roles.isEmpty()) {
        addSection(tr("Roles"));
        QStringList chips;
        for (const ProfileCardData::RoleChip& role : m_data.roles) {
            const QString color = role.color.isValid() ? role.color.name() : Theme::instance().palette().textMuted.name();
            chips.append(QStringLiteral("<span style=\"color:%1;\">●</span>&nbsp;%2").arg(color, role.name.toHtmlEscaped()));
        }
        QLabel* roles = makeLabel(chips.join(QStringLiteral("&nbsp;&nbsp;&nbsp; ")), "profileText");
        roles->setTextFormat(Qt::RichText);
        content->addWidget(roles);
    }

    if (!m_data.connections.isEmpty()) {
        addSection(tr("Connections"));
        for (const ConnectedAccount& account : m_data.connections) {
            QString type = account.type;
            if (!type.isEmpty())
                type[0] = type[0].toUpper();
            content->addWidget(makeLabel(QStringLiteral("%1: %2%3").arg(type, account.name,
                                                                         account.verified ? QStringLiteral(" ✓") : QString()),
                                         "profileText"));
        }
    }

    if (!m_data.mutualServers.isEmpty()) {
        content->addSpacing(8);
        QLabel* mutual = makeLabel(tr("Mutual servers: %1").arg(m_data.mutualServers.size()), "profileMuted");
        mutual->setToolTip(m_data.mutualServers.join(u'\n'));
        content->addWidget(mutual);
    }

    if (!m_data.profileLoaded && m_data.error.isEmpty())
        content->addWidget(makeLabel(tr("Loading profile…"), "profileMuted"));
    else if (!m_data.error.isEmpty())
        content->addWidget(makeLabel(tr("Could not load the full profile: %1").arg(m_data.error), "profileMuted"));

    if (content->count() > 0)
        outer->addWidget(box);
    else
        delete box;

    if (m_actionsVisible) {
        if (QWidget* actions = buildActions())
            outer->addWidget(actions);
    }
    return body;
}

QWidget* ProfileCard::buildActivity(const Activity& activity)
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QString title;
    switch (activity.type) {
    case Activity::Streaming:
        title = tr("Live on %1").arg(activity.name);
        break;
    case Activity::Listening:
        title = tr("Listening to %1").arg(activity.name);
        break;
    case Activity::Watching:
        title = tr("Watching %1").arg(activity.name);
        break;
    case Activity::Competing:
        title = tr("Competing in %1").arg(activity.name);
        break;
    default:
        title = tr("Playing");
        break;
    }
    layout->addWidget(sectionTitle(title));

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    const QImage large = image(activityImage(activity, activity.largeImage));
    auto* picture = new QLabel;
    picture->setFixedSize(ActivityImageSize, ActivityImageSize);
    picture->setPixmap(roundedPicture(large, activity.name, ActivityImageSize, devicePixelRatioF()));
    picture->setToolTip(activity.largeText);
    row->addWidget(picture, 0, Qt::AlignTop);

    auto* lines = new QVBoxLayout;
    lines->setSpacing(1);
    const bool spotify = activity.isSpotify();
    // Music shows the song first; everything else its name, then details and state.
    if (activity.type == Activity::Listening || activity.type == Activity::Watching) {
        lines->addWidget(makeLabel(activity.details.isEmpty() ? activity.name : activity.details, "profileActivityName"));
        if (!activity.state.isEmpty())
            lines->addWidget(makeLabel(spotify ? tr("by %1").arg(activity.state) : activity.state, "profileText"));
        if (!activity.largeText.isEmpty() && spotify)
            lines->addWidget(makeLabel(tr("on %1").arg(activity.largeText), "profileText"));
    } else {
        lines->addWidget(makeLabel(activity.name, "profileActivityName"));
        if (!activity.details.isEmpty())
            lines->addWidget(makeLabel(activity.details, "profileText"));
        QString state = activity.state;
        if (activity.partyMax > 0)
            state += (state.isEmpty() ? QString() : QStringLiteral(" ")) + tr("(%1 of %2)").arg(activity.partySize).arg(activity.partyMax);
        if (!state.isEmpty())
            lines->addWidget(makeLabel(state, "profileText"));
    }

    TimedActivity timed;
    timed.activity = activity;
    if (activity.start > 0 && activity.end > activity.start) {
        // A known length: a progress bar with the position and duration (songs, videos).
        timed.progress = new QProgressBar;
        timed.progress->setObjectName(QStringLiteral("profileProgress"));
        timed.progress->setTextVisible(false);
        timed.progress->setFixedHeight(4);
        timed.progress->setRange(0, 1000);
        timed.position = makeLabel(QString(), "profileMuted", false);
        timed.duration = makeLabel(formatDuration(activity.end - activity.start), "profileMuted", false);
        auto* times = new QHBoxLayout;
        times->addWidget(timed.position);
        times->addStretch();
        times->addWidget(timed.duration);
        lines->addSpacing(4);
        lines->addWidget(timed.progress);
        lines->addLayout(times);
    } else if (activity.start > 0 || activity.end > 0) {
        timed.label = makeLabel(QString(), "profileMuted", false);
        lines->addWidget(timed.label);
    }
    if (timed.label || timed.progress)
        m_timed.append(timed);

    lines->addStretch();
    row->addLayout(lines, 1);
    layout->addLayout(row);
    return widget;
}

QWidget* ProfileCard::buildActions()
{
    if (!m_data.isSelf && !m_data.canMessage)
        return nullptr;
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    if (!m_data.isSelf) {
        auto* message = new QPushButton(tr("Message"));
        message->setObjectName(QStringLiteral("brandButton"));
        message->setCursor(Qt::PointingHandCursor);
        connect(message, &QPushButton::clicked, this, &ProfileCard::messageRequested);
        layout->addWidget(message);
        return widget;
    }

    auto* edit = new QPushButton(tr("Edit Profile"));
    edit->setObjectName(QStringLiteral("brandButton"));
    edit->setCursor(Qt::PointingHandCursor);
    connect(edit, &QPushButton::clicked, this, &ProfileCard::editProfileRequested);
    layout->addWidget(edit);

    auto statusIcon = [this](UserStatus option, const QColor& background) {
        QPixmap dot(QSize(16, 16) * devicePixelRatioF());
        dot.setDevicePixelRatio(devicePixelRatioF());
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        drawStatusDot(painter, QRectF(1, 1, 14, 14), option, background);
        return QIcon(dot);
    };
    auto* status = new QPushButton(statusIcon(m_data.presence.status, Theme::instance().palette().button),
                                   statusName(m_data.presence.status));
    status->setObjectName(QStringLiteral("secondaryButton"));
    status->setCursor(Qt::PointingHandCursor);
    auto* menu = new QMenu(status);
    for (UserStatus option : {UserStatus::Online, UserStatus::Idle, UserStatus::DoNotDisturb, UserStatus::Invisible}) {
        QAction* action = menu->addAction(statusIcon(option, Theme::instance().palette().bg4), statusName(option));
        connect(action, &QAction::triggered, this, [this, option] { emit statusRequested(option); });
    }
    status->setMenu(menu);
    layout->addWidget(status);

    auto* custom = new QPushButton(m_data.presence.customStatus() ? tr("Edit Custom Status") : tr("Set Custom Status"));
    custom->setObjectName(QStringLiteral("secondaryButton"));
    custom->setCursor(Qt::PointingHandCursor);
    connect(custom, &QPushButton::clicked, this, &ProfileCard::customStatusRequested);
    layout->addWidget(custom);
    return widget;
}

void ProfileCard::updateTimes()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const TimedActivity& timed : std::as_const(m_timed)) {
        const Activity& activity = timed.activity;
        if (timed.progress) {
            const qint64 length = activity.end - activity.start;
            const qint64 position = qBound<qint64>(0, now - activity.start, length);
            timed.progress->setValue(int(position * 1000 / length));
            timed.position->setText(formatDuration(position));
        } else if (activity.end > 0) {
            timed.label->setText(tr("%1 left").arg(formatDuration(activity.end - now)));
        } else {
            timed.label->setText(tr("%1 elapsed").arg(formatDuration(now - activity.start)));
        }
    }
}
