#include "FriendsView.h"

#include "Avatar.h"
#include "ImageCache.h"
#include "Theme.h"
#include "core/Models.h"
#include "core/Session.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

bool isOnline(UserStatus status)
{
    return status == UserStatus::Online || status == UserStatus::Idle || status == UserStatus::DoNotDisturb;
}

} // namespace

QString FriendsView::statusLine(const Presence& presence) const
{
    if (const Activity* custom = presence.customStatus()) {
        QString text = custom->emojiName;
        if (!custom->state.isEmpty())
            text = text.isEmpty() ? custom->state : text + QLatin1Char(' ') + custom->state;
        if (!text.isEmpty())
            return text;
    }
    for (const Activity& activity : presence.activities) {
        if (activity.type == Activity::Custom || activity.name.isEmpty())
            continue;
        switch (activity.type) {
        case Activity::Playing:
            return tr("Playing %1").arg(activity.name);
        case Activity::Streaming:
            return tr("Streaming %1").arg(activity.name);
        case Activity::Listening:
            return tr("Listening to %1").arg(activity.name);
        case Activity::Watching:
            return tr("Watching %1").arg(activity.name);
        default:
            return activity.name;
        }
    }
    switch (presence.status) {
    case UserStatus::Idle:
        return tr("Idle");
    case UserStatus::DoNotDisturb:
        return tr("Do Not Disturb");
    case UserStatus::Offline:
    case UserStatus::Unknown:
    case UserStatus::Invisible:
        return tr("Offline");
    case UserStatus::Online:
        break;
    }
    return tr("Online");
}

FriendsView::FriendsView(Session* session, ImageCache* images, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_images(images)
    , m_tabs(new QButtonGroup(this))
    , m_stack(new QStackedWidget)
    , m_scroll(new QScrollArea)
    , m_addPage(new QWidget)
    , m_username(new QLineEdit)
    , m_addStatus(new QLabel)
{
    setObjectName(QStringLiteral("friendsView"));
    setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(tr("Friends"));
    title->setObjectName(QStringLiteral("friendsTitle"));

    auto addTab = [this](const QString& text, Tab tab) {
        auto* button = new QPushButton(text);
        button->setObjectName(tab == Tab::Add ? QStringLiteral("friendsAddTab") : QStringLiteral("friendsTab"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        m_tabs->addButton(button, static_cast<int>(tab));
        return button;
    };
    QPushButton* online = addTab(tr("Online"), Tab::Online);
    online->setChecked(true);
    QPushButton* all = addTab(tr("All"), Tab::All);
    QPushButton* pending = addTab(tr("Pending"), Tab::Pending);
    QPushButton* add = addTab(tr("Add Friend"), Tab::Add);
    m_tabs->setExclusive(true);
    connect(m_tabs, &QButtonGroup::idClicked, this, [this](int id) { setTab(static_cast<Tab>(id)); });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(16, 0, 16, 0);
    header->setSpacing(8);
    header->addWidget(title);
    header->addSpacing(8);
    header->addWidget(online);
    header->addWidget(all);
    header->addWidget(pending);
    header->addStretch();
    header->addWidget(add);

    auto* headerBar = new QWidget;
    headerBar->setObjectName(QStringLiteral("friendsHeader"));
    headerBar->setAttribute(Qt::WA_StyledBackground);
    headerBar->setFixedHeight(48);
    headerBar->setLayout(header);

    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* addLayout = new QVBoxLayout(m_addPage);
    addLayout->setContentsMargins(24, 24, 24, 24);
    addLayout->setSpacing(8);
    auto* addTitle = new QLabel(tr("Add Friend"));
    addTitle->setObjectName(QStringLiteral("friendsTitle"));
    auto* hint = new QLabel(tr("You can add friends with their Discord username."));
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    m_username->setPlaceholderText(tr("Username"));
    m_username->setClearButtonEnabled(true);
    auto* send = new QPushButton(tr("Send Friend Request"));
    send->setObjectName(QStringLiteral("accentButton"));
    send->setCursor(Qt::PointingHandCursor);
    m_addStatus->setObjectName(QStringLiteral("settingsHint"));
    m_addStatus->setWordWrap(true);
    addLayout->addWidget(addTitle);
    addLayout->addWidget(hint);
    addLayout->addSpacing(8);
    addLayout->addWidget(m_username);
    addLayout->addWidget(send, 0, Qt::AlignLeft);
    addLayout->addWidget(m_addStatus);
    addLayout->addStretch();
    connect(send, &QPushButton::clicked, this, &FriendsView::sendRequest);
    connect(m_username, &QLineEdit::returnPressed, this, &FriendsView::sendRequest);

    m_stack->addWidget(m_scroll);
    m_stack->addWidget(m_addPage);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(headerBar);
    layout->addWidget(m_stack, 1);

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(50);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &FriendsView::rebuild);
    connect(m_session, &Session::relationshipsChanged, this, &FriendsView::rebuild);
    connect(m_session, &Session::presenceChanged, this, &FriendsView::scheduleRebuild);
    connect(m_session, &Session::usersChanged, this, &FriendsView::scheduleRebuild);
    connect(m_session, &Session::ready, this, &FriendsView::rebuild);
    connect(m_images, &ImageCache::imageLoaded, this, &FriendsView::scheduleRebuild);
    rebuild();
}

void FriendsView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    rebuild();
}

void FriendsView::scheduleRebuild()
{
    if (isVisible())
        m_rebuildTimer.start();
}

void FriendsView::setTab(Tab tab)
{
    m_tab = tab;
    m_stack->setCurrentWidget(tab == Tab::Add ? static_cast<QWidget*>(m_addPage) : m_scroll);
    if (tab != Tab::Add)
        rebuild();
}

void FriendsView::sendRequest()
{
    const QString username = m_username->text().trimmed();
    m_addStatus->setText(username.isEmpty() ? tr("Enter a username.") : tr("Sending…"));
    if (username.isEmpty())
        return;
    m_session->addFriend(username, [this, username](const QString& error) {
        if (!error.isEmpty())
            m_addStatus->setText(error);
        else {
            m_username->clear();
            m_addStatus->setText(tr("Friend request sent to %1.").arg(username));
        }
    });
}

void FriendsView::rebuild()
{
    if (m_tab == Tab::Add)
        return;

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(2);

    const QList<Relationship> all = m_session->relationships();
    QList<Relationship> shown;
    for (const Relationship& relationship : all) {
        const UserStatus status = m_session->presence(relationship.userId).status;
        if (m_tab == Tab::Online && relationship.type == Relationship::Friend && isOnline(status))
            shown.append(relationship);
        else if (m_tab == Tab::All && relationship.type == Relationship::Friend)
            shown.append(relationship);
        else if (m_tab == Tab::Pending
                 && (relationship.type == Relationship::Incoming || relationship.type == Relationship::Outgoing))
            shown.append(relationship);
    }
    std::sort(shown.begin(), shown.end(), [this](const Relationship& a, const Relationship& b) {
        const bool aOnline = isOnline(m_session->presence(a.userId).status);
        const bool bOnline = isOnline(m_session->presence(b.userId).status);
        if (aOnline != bOnline)
            return aOnline;
        if (a.type != b.type)
            return a.type < b.type;
        return m_session->user(a.userId).displayName().localeAwareCompare(m_session->user(b.userId).displayName()) < 0;
    });

    auto addHeader = [layout](const QString& text) {
        auto* header = new QLabel(text);
        header->setObjectName(QStringLiteral("friendsSection"));
        layout->addSpacing(8);
        layout->addWidget(header);
    };
    auto addRow = [this, layout](const Relationship& relationship) {
        const User user = m_session->user(relationship.userId);
        const Presence presence = m_session->presence(relationship.userId);
        const QString name = user.displayName().isEmpty() ? tr("Unknown user") : user.displayName();

        auto* row = new QWidget;
        row->setObjectName(QStringLiteral("friendRow"));
        row->setAttribute(Qt::WA_StyledBackground);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        rowLayout->setSpacing(12);

        auto* avatar = new QLabel;
        avatar->setPixmap(makeAvatar(name, m_images->image(ImageCache::avatarUrl(user)), 32, devicePixelRatioF(), false,
                                     Theme::instance().palette().bg2, presence.status));
        avatar->setFixedSize(32, 32);
        rowLayout->addWidget(avatar);

        auto* names = new QVBoxLayout;
        names->setSpacing(0);
        auto* nameLabel = new QLabel(name);
        nameLabel->setObjectName(QStringLiteral("friendName"));
        names->addWidget(nameLabel);
        QString detail = statusLine(presence);
        if (relationship.type == Relationship::Incoming)
            detail = tr("Incoming Friend Request");
        else if (relationship.type == Relationship::Outgoing)
            detail = tr("Outgoing Friend Request");
        else if (!user.username.isEmpty() && user.username != name)
            detail = user.username + QStringLiteral(" · ") + detail;
        auto* detailLabel = new QLabel(detail);
        detailLabel->setObjectName(QStringLiteral("settingsHint"));
        names->addWidget(detailLabel);
        rowLayout->addLayout(names, 1);

        if (relationship.type == Relationship::Friend) {
            auto* message = new QPushButton(tr("Message"));
            message->setObjectName(QStringLiteral("secondaryButton"));
            message->setCursor(Qt::PointingHandCursor);
            connect(message, &QPushButton::clicked, this, [this, userId = relationship.userId] {
                emit conversationRequested(userId);
            });
            rowLayout->addWidget(message);
        } else if (relationship.type == Relationship::Incoming) {
            auto* accept = new QPushButton(tr("Accept"));
            accept->setObjectName(QStringLiteral("accentButton"));
            accept->setCursor(Qt::PointingHandCursor);
            connect(accept, &QPushButton::clicked, this, [this, userId = relationship.userId] {
                m_session->acceptFriend(userId, [this](const QString& error) {
                    if (!error.isEmpty())
                        QMessageBox::warning(this, tr("Couldn't do that"), error);
                });
            });
            rowLayout->addWidget(accept);
        }

        if (relationship.type == Relationship::Friend || relationship.type == Relationship::Incoming
            || relationship.type == Relationship::Outgoing) {
            const bool removing = relationship.type == Relationship::Friend;
            auto* remove = new QPushButton(removing ? tr("Remove") : tr("Cancel"));
            remove->setObjectName(QStringLiteral("secondaryButton"));
            remove->setCursor(Qt::PointingHandCursor);
            connect(remove, &QPushButton::clicked, this, [this, userId = relationship.userId, name, removing] {
                if (removing
                    && QMessageBox::question(this, tr("Remove Friend"), tr("Remove %1 from your friends?").arg(name))
                        != QMessageBox::Yes)
                    return;
                m_session->removeRelationship(userId, [this](const QString& error) {
                    if (!error.isEmpty())
                        QMessageBox::warning(this, tr("Couldn't do that"), error);
                });
            });
            rowLayout->addWidget(remove);
        }
        layout->addWidget(row);
    };

    if (shown.isEmpty()) {
        auto* empty = new QLabel(m_tab == Tab::Pending ? tr("No pending requests.") : tr("No friends to show."));
        empty->setObjectName(QStringLiteral("settingsHint"));
        empty->setAlignment(Qt::AlignCenter);
        layout->addSpacing(48);
        layout->addWidget(empty);
    } else if (m_tab == Tab::Pending) {
        QList<Relationship> incoming;
        QList<Relationship> outgoing;
        for (const Relationship& relationship : shown) {
            if (relationship.type == Relationship::Incoming)
                incoming.append(relationship);
            else
                outgoing.append(relationship);
        }
        if (!incoming.isEmpty()) {
            addHeader(tr("Incoming — %1").arg(incoming.size()));
            for (const Relationship& relationship : incoming)
                addRow(relationship);
        }
        if (!outgoing.isEmpty()) {
            addHeader(tr("Outgoing — %1").arg(outgoing.size()));
            for (const Relationship& relationship : outgoing)
                addRow(relationship);
        }
    } else if (m_tab == Tab::All) {
        QList<Relationship> online;
        QList<Relationship> offline;
        for (const Relationship& relationship : shown) {
            if (isOnline(m_session->presence(relationship.userId).status))
                online.append(relationship);
            else
                offline.append(relationship);
        }
        if (!online.isEmpty()) {
            addHeader(tr("Online — %1").arg(online.size()));
            for (const Relationship& relationship : online)
                addRow(relationship);
        }
        if (!offline.isEmpty()) {
            addHeader(tr("Offline — %1").arg(offline.size()));
            for (const Relationship& relationship : offline)
                addRow(relationship);
        }
    } else {
        addHeader(tr("Online — %1").arg(shown.size()));
        for (const Relationship& relationship : shown)
            addRow(relationship);
    }
    layout->addStretch();
    m_scroll->setWidget(content);
}
