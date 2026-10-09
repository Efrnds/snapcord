#include "MainWindow.h"

#include "Avatar.h"
#include "ChannelSidebar.h"
#include "ChatView.h"
#include "FriendsView.h"
#include "ConnectionInfoPopup.h"
#include "ImageCache.h"
#include "Motion.h"
#include "IncomingCallWindow.h"
#include "Notifier.h"
#include "ProfileCard.h"
#include "ProfileEditor.h"
#include "ProfilePopup.h"
#include "ServerDialogs.h"
#include "ServerRail.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "UserPanel.h"
#include "VoiceChannelView.h"
#include "VoiceController.h"
#include "VoicePanel.h"
#include "core/Permissions.h"
#include "core/Session.h"

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QScopeGuard>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <limits>

namespace {

constexpr auto LastGuildKey = "ui/lastGuild";
constexpr auto LastChannelKey = "ui/lastChannel/";

} // namespace

// Solid theme background behind the rail / sidebar / chat columns.
class MainWindow::AppShell : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::instance().palette().bg2);
    }
};

MainWindow::MainWindow(Session* session, VoiceController* voice, QWidget* parent)
    : QMainWindow(parent)
    , m_session(session)
    , m_voice(voice)
    , m_images(new ImageCache(this))
    , m_rail(new ServerRail)
    , m_sidebar(new ChannelSidebar)
    , m_pages(new QStackedWidget)
    , m_chatView(new ChatView(session, m_images, voice))
    , m_voiceView(new VoiceChannelView)
    , m_incomingCall(new IncomingCallWindow(this))
    , m_notifier(new Notifier(session, voice->sounds(), this, this))
{
    setWindowTitle(QStringLiteral("Snapcord"));
    const QColor background = Theme::instance().palette().bg2;
    QPalette colors = palette();
    colors.setColor(QPalette::Window, background);
    colors.setColor(QPalette::WindowText, Theme::instance().palette().text);
    setPalette(colors);
    setAutoFillBackground(true);
    resize(1280, 720);
    setMinimumSize(940, 500);

    m_homePage = buildPlaceholderPage(tr("Direct Messages"), tr("Pick a conversation on the left."));
    m_friendsPage = new FriendsView(session, m_images);
    m_pages->addWidget(m_homePage);
    m_pages->addWidget(m_friendsPage);
    connect(m_friendsPage, &FriendsView::conversationRequested, this, [this](const QString& userId) {
        m_session->openDirectMessage(userId, [this](const QString& channelId, const QString& error) {
            if (!error.isEmpty())
                reportModeration(error);
            else if (!channelId.isEmpty())
                showChannel(QString(), channelId);
        });
    });
    m_pages->addWidget(m_chatView);
    m_pages->addWidget(m_voiceView);

    m_shell = new AppShell;
    m_shell->setAttribute(Qt::WA_StyledBackground, false);
    auto* layout = new QHBoxLayout(m_shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(m_shell);
    connect(&Theme::instance(), &Theme::changed, m_shell, QOverload<>::of(&QWidget::update));

    m_sidebar->setTitle(tr("Connecting…"));

    // Several Gateway events often arrive together; refresh once per burst.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        refreshChannels();
        refreshCenter();
        refreshVoicePanel();
        refreshUserPanel();
    });
    auto scheduleRefresh = [this] { m_refreshTimer.start(); };
    // Unread badges of all servers are recomputed less often: busy servers produce many messages.
    m_badgeTimer.setSingleShot(true);
    m_badgeTimer.setInterval(250);
    connect(&m_badgeTimer, &QTimer::timeout, this, &MainWindow::refreshUnreadBadges);

    m_notifier->setCurrentChannelProvider([this] { return m_pages->currentWidget() == m_chatView ? m_chatView->channelId() : QString(); });
    connect(m_notifier, &Notifier::openChannelRequested, this, [this](const QString& guildId, const QString& channelId) {
        selectGuild(guildId);
        openChannel(channelId);
    });

    connect(m_session, &Session::ready, this, [this] {
        rebuildServerRail();
        const QString lastGuild = QSettings().value(QLatin1String(LastGuildKey)).toString();
        selectGuild(lastGuild.isEmpty() || m_session->guild(lastGuild) ? lastGuild : m_session->guildOrder().value(0));
        refreshUserPanel();
        const User& self = m_session->self();
        Theme::instance().setDiscordProfileColors(
            self.hasAccentColor ? QColor::fromRgb(self.accentColorRgb) : QColor(),
            QColor(self.bannerColorHex));
    });
    connect(m_session, &Session::usersChanged, this, [this] {
        const User& self = m_session->self();
        Theme::instance().setDiscordProfileColors(
            self.hasAccentColor ? QColor::fromRgb(self.accentColorRgb) : QColor(),
            QColor(self.bannerColorHex));
    });
    connect(m_session, &Session::guildListChanged, this, &MainWindow::rebuildServerRail);
    connect(m_session, &Session::guildChanged, this, [this, scheduleRefresh](const QString& guildId) {
        // The open channel was deleted: move to another one, like Discord does.
        const Guild* guild = guildId.isEmpty() || guildId != m_guildId ? nullptr : m_session->guild(guildId);
        if (guild && !guild->unavailable && !guild->channels.isEmpty() && !m_channelId.isEmpty()
            && !guild->channels.contains(m_channelId)) {
            selectGuild(guildId);
            return;
        }
        if (guildId == m_guildId || guildId == m_voice->guildId())
            scheduleRefresh();
    });
    connect(m_session, &Session::voiceStatesChanged, this, [this, scheduleRefresh](const QString& guildId) {
        if (guildId == m_guildId)
            scheduleRefresh();
    });
    connect(m_session, &Session::usersChanged, this, scheduleRefresh);
    connect(m_session, &Session::presenceChanged, this, [this, scheduleRefresh](const QString& userId, bool statusChanged) {
        if (userId == m_session->self().id)
            refreshUserPanel();
        else if (statusChanged && m_guildId.isEmpty())
            scheduleRefresh(); // status dots in the direct message list
    });
    connect(m_session, &Session::privateChannelsChanged, this, &MainWindow::openPendingDestination);
    connect(m_session, &Session::privateChannelsChanged, this, [this, scheduleRefresh] {
        if (m_guildId.isEmpty())
            scheduleRefresh();
    });
    connect(m_session, &Session::readStateChanged, this, [this, scheduleRefresh](const QString& guildId, const QString& channelId) {
        m_badgeTimer.start();
        if (guildId == m_guildId || (m_guildId.isEmpty() && m_session->privateChannel(channelId)))
            scheduleRefresh();
    });
    connect(m_session, &Session::callChanged, this, [this, scheduleRefresh](const QString& channelId) {
        updateIncomingCall(channelId);
        if (m_guildId.isEmpty() || channelId == m_voice->channelId())
            scheduleRefresh();
    });
    connect(m_images, &ImageCache::imageLoaded, this, [this, scheduleRefresh](const QUrl& url) {
        for (const QString& guildId : m_session->guildOrder()) {
            const Guild* guild = m_session->guild(guildId);
            if (guild && ImageCache::guildIconUrl(guild->id, guild->icon) == url) {
                m_rail->setServerIcon(guildId, m_images->image(url));
                return;
            }
        }
        scheduleRefresh();
    });

    connect(m_incomingCall, &IncomingCallWindow::accepted, this, [this](const QString& channelId) {
        m_incomingCall->hide();
        m_voice->sounds()->stopRinging();
        m_voice->join(QString(), channelId);
        selectGuild(QString());
        openChannel(channelId);
    });
    connect(m_incomingCall, &IncomingCallWindow::declined, this, [this](const QString& channelId) {
        m_incomingCall->hide();
        m_voice->sounds()->stopRinging();
        m_session->declineCall(channelId);
    });

    connect(m_rail, &ServerRail::serverSelected, this, &MainWindow::selectGuild);
    connect(m_rail, &ServerRail::homeSelected, this, [this] { selectGuild(QString()); });
    connect(m_sidebar, &ChannelSidebar::friendsSelected, this, [this] {
        if (!m_guildId.isEmpty())
            return;
        m_channelId.clear();
        m_sidebar->setSelectedChannel(QString());
        m_sidebar->setFriendsSelected(true);
        refreshCenter();
    });
    connect(m_rail, &ServerRail::foldersChanged, m_session, &Session::setGuildFolders);
    connect(m_rail, &ServerRail::addServerRequested, this, [this] { openJoinDialog({}); });
    connect(m_rail, &ServerRail::serverContextMenuRequested, this, &MainWindow::showServerMenu);
    connect(m_rail, &ServerRail::folderContextMenuRequested, this, &MainWindow::showFolderMenu);
    connect(m_chatView, &ChatView::inviteLinkActivated, this, &MainWindow::openJoinDialog);
    connect(m_chatView, &ChatView::openChannelRequested, this, &MainWindow::goTo);

    connect(m_sidebar, &ChannelSidebar::channelClicked, this, [this](const QString& id, ChannelSidebar::ItemKind kind) {
        // Discord joins a guild voice channel on a single click.
        if (kind == ChannelSidebar::ItemKind::VoiceChannel && m_session->canConnect(m_guildId, id))
            m_voice->join(m_guildId, id);
        openChannel(id);
    });
    connect(m_sidebar, &ChannelSidebar::memberContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_sidebar, &ChannelSidebar::channelContextMenuRequested, this,
            [this](const QString& id, ChannelSidebar::ItemKind, const QPoint& position) { showChannelMenu(id, position); });
    connect(m_sidebar, &ChannelSidebar::createChannelRequested, this, &MainWindow::openCreateChannel);
    connect(m_sidebar, &ChannelSidebar::memberClicked, this, [this](const QString& userId, const QPoint& position) {
        showProfile(userId, m_guildId, position);
    });
    connect(m_voiceView, &VoiceChannelView::participantClicked, this, [this](const QString& userId, const QPoint& position) {
        showProfile(userId, m_guildId, position);
    });
    connect(m_chatView, &ChatView::profileRequested, this, &MainWindow::showProfile);
    connect(m_chatView, &ChatView::memberProfileRequested, this,
            [this](const QString& userId, const QString& guildId, const QPoint& position) {
                auto* popup = new ProfilePopup(m_session, m_images, userId, guildId, this);
                connectProfilePopup(popup);
                popup->popupLeftOf(position);
            });
    connect(m_chatView, &ChatView::memberContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::participantContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::joinRequested, this, [this] { m_voice->join(m_guildId, m_channelId); });
    connect(m_sidebar->voicePanel(), &VoicePanel::detailsRequested, this, [this] {
        auto* popup = new ConnectionInfoPopup(m_voice, this);
        popup->showAbove(m_sidebar->voicePanel());
    });

    UserPanel* userPanel = m_sidebar->userPanel();
    connect(userPanel, &UserPanel::muteClicked, m_voice, &VoiceController::toggleMute);
    connect(userPanel, &UserPanel::deafenClicked, m_voice, &VoiceController::toggleDeafen);
    connect(userPanel, &UserPanel::settingsRequested, this, &MainWindow::openSettings);
    connect(userPanel, &UserPanel::profileRequested, this, &MainWindow::showOwnProfile);
    connect(m_sidebar->voicePanel(), &VoicePanel::disconnectRequested, m_voice, &VoiceController::leave);

    connect(m_voice, &VoiceController::selfStateChanged, this, &MainWindow::refreshUserPanel);
    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::refreshUserPanel);
    connect(m_voice, &VoiceController::channelChanged, this, scheduleRefresh);
    connect(m_voice, &VoiceController::stateChanged, this, scheduleRefresh);
    connect(m_voice, &VoiceController::pingChanged, m_sidebar->voicePanel(), &VoicePanel::setPing);
    connect(m_voice, &VoiceController::speakingChanged, this, &MainWindow::onSpeakingChanged);
    connect(m_voice, &VoiceController::errorOccurred, this, [this](const QString& message) {
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Voice"), message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();
    });

    refreshUserPanel();
}

void MainWindow::showChannel(const QString& guildId, const QString& channelId)
{
    selectGuild(guildId);
    openChannel(channelId);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // Coming back to the window reads the conversation on screen.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_pages->currentWidget() == m_chatView)
        m_chatView->markReadIfVisible();
}

QWidget* MainWindow::buildPlaceholderPage(const QString& title, const QString& subtitle)
{
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("chatArea"));
    page->setAttribute(Qt::WA_StyledBackground);
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("welcomeTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    auto* subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setObjectName(QStringLiteral("welcomeSubtitle"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 0, 48, 0);
    layout->addStretch();
    layout->addWidget(titleLabel);
    layout->addSpacing(8);
    layout->addWidget(subtitleLabel);
    layout->addStretch();
    return page;
}

void MainWindow::rebuildServerRail()
{
    QHash<QString, ServerRail::ServerInfo> servers;
    for (const QString& id : m_session->guildOrder()) {
        const Guild* guild = m_session->guild(id);
        if (!guild || guild->unavailable)
            continue;
        servers.insert(id, {guild->name, m_images->image(ImageCache::guildIconUrl(id, guild->icon))});
    }
    m_rail->setServers(m_session->guildFolders(), servers);
    refreshUnreadBadges();
    // The server on screen was left (or the user was removed from it).
    if (!m_guildId.isEmpty() && !m_session->guild(m_guildId))
        selectGuild(QString());
    else
        m_rail->select(m_guildId);
    openPendingDestination();
}

void MainWindow::goTo(const QString& guildId, const QString& channelId)
{
    m_pendingGuildId = guildId;
    m_pendingChannelId = guildId.isEmpty() ? channelId : QString();
    openPendingDestination();
}

void MainWindow::openPendingDestination()
{
    if (!m_pendingGuildId.isEmpty() && m_session->guild(m_pendingGuildId)) {
        const QString guildId = std::exchange(m_pendingGuildId, QString());
        selectGuild(guildId);
    } else if (m_pendingGuildId.isEmpty() && !m_pendingChannelId.isEmpty() && m_session->privateChannel(m_pendingChannelId)) {
        const QString channelId = std::exchange(m_pendingChannelId, QString());
        selectGuild(QString());
        openChannel(channelId);
    }
}

void MainWindow::openJoinDialog(const QString& code)
{
    auto* dialog = new JoinServerDialog(m_session, m_images, code, this);
    connect(dialog, &JoinServerDialog::joined, this, &MainWindow::goTo);
    dialog->open();
}

void MainWindow::showServerMenu(const QString& guildId, const QPoint& globalPosition)
{
    const Guild* guild = m_session->guild(guildId);
    if (!guild)
        return;
    QMenu menu(this);
    if (m_session->canManageChannels(guildId)) {
        menu.addAction(tr("Create Channel"), this, [this, guildId] {
            if (guildId != m_guildId)
                selectGuild(guildId);
            openCreateChannel({});
        });
    }
    const QString inviteChannel = m_session->inviteChannel(guildId, guildId == m_guildId ? m_channelId : QString());
    if (!inviteChannel.isEmpty()) {
        menu.addAction(tr("Invite People"), this, [this, guildId, inviteChannel] {
            auto* dialog = new InviteDialog(m_session, guildId, inviteChannel, this);
            dialog->open();
        });
    }
    // The owner cannot leave without handing the server over, which the official app does.
    if (guild->ownerId != m_session->self().id) {
        if (!menu.isEmpty())
            menu.addSeparator();
        menu.addAction(tr("Leave Server"), this, [this, guildId, name = guild->name] {
            QMessageBox box(QMessageBox::Warning, tr("Leave '%1'").arg(name),
                            tr("Are you sure you want to leave %1? You won't be able to rejoin this server unless you are re-invited.")
                                .arg(name),
                            QMessageBox::Cancel, this);
            QPushButton* confirm = box.addButton(tr("Leave Server"), QMessageBox::DestructiveRole);
            box.exec();
            if (box.clickedButton() != confirm)
                return;
            m_session->leaveGuild(guildId, [this](const QString& error) {
                if (error.isEmpty())
                    return;
                auto* failure = new QMessageBox(QMessageBox::Warning, tr("Leave Server"), error, QMessageBox::Ok, this);
                failure->setAttribute(Qt::WA_DeleteOnClose);
                failure->show();
            });
        });
    }
    if (!menu.isEmpty())
        menu.exec(globalPosition);
}

void MainWindow::showFolderMenu(const QString& folderId, const QPoint& globalPosition)
{
    QMenu menu(this);
    menu.addAction(tr("Folder Settings"), this, [this, folderId] {
        QList<GuildFolder> folders = m_session->guildFolders();
        const qsizetype index = GuildFolders::folderIndex(folders, folderId);
        if (index < 0)
            return;
        QStringList names;
        for (const QString& id : std::as_const(folders[index].guildIds)) {
            if (const Guild* guild = m_session->guild(id))
                names.append(guild->name);
        }
        FolderSettingsDialog dialog(folders[index], names.join(QStringLiteral(", ")), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        // The list may have changed while the dialog was open.
        folders = m_session->guildFolders();
        const qsizetype current = GuildFolders::folderIndex(folders, folderId);
        if (current < 0)
            return;
        folders[current].name = dialog.folder().name;
        folders[current].color = dialog.folder().color;
        m_session->setGuildFolders(folders);
    });
    menu.exec(globalPosition);
}

void MainWindow::showChannelMenu(const QString& channelId, const QPoint& globalPosition)
{
    if (m_guildId.isEmpty())
        return;
    const Channel* channel = m_session->channel(m_guildId, channelId);
    const bool category = channel && channel->type == ChannelType::GuildCategory;
    QMenu menu(this);
    // New channels go inside the category that was clicked (or the clicked channel's category).
    const QString parentId = !channel ? QString() : category ? channel->id : channel->parentId;
    if (parentId.isEmpty() ? m_session->canManageChannels(m_guildId) : m_session->canManageChannels(m_guildId, parentId))
        menu.addAction(tr("Create Channel"), this, [this, parentId] { openCreateChannel(parentId); });
    if (channel && m_session->canManageChannels(m_guildId, channelId)) {
        if (!menu.isEmpty())
            menu.addSeparator();
        menu.addAction(category ? tr("Edit Category") : tr("Edit Channel"), this, [this, channelId] {
            auto* dialog = new ChannelSettingsDialog(m_session, m_guildId, channelId, this);
            dialog->open();
        });
        menu.addAction(category ? tr("Delete Category") : tr("Delete Channel"), this,
                       [this, channelId] { confirmDeleteChannel(m_session, m_guildId, channelId, this); });
    }
    if (!menu.isEmpty())
        menu.exec(globalPosition);
}

void MainWindow::openCreateChannel(const QString& categoryId)
{
    if (m_guildId.isEmpty())
        return;
    auto* dialog = new CreateChannelDialog(m_session, m_guildId, categoryId, this);
    connect(dialog, &CreateChannelDialog::created, this, [this, guildId = m_guildId](const QString& channelId) {
        // Like Discord, a new text channel opens right away.
        const Channel* channel = m_session->channel(guildId, channelId);
        if (guildId == m_guildId && channel && !channel->isVoice())
            openChannel(channelId);
    });
    dialog->open();
}

void MainWindow::refreshUnreadBadges()
{
    for (const QString& id : m_session->guildOrder())
        m_rail->setServerUnread(id, m_session->guildHasUnread(id), m_session->guildMentionCount(id));
    m_rail->setHomeMentions(m_session->privateMentionCount());
}

void MainWindow::selectGuild(const QString& guildId)
{
    m_guildId = guildId;
    m_rail->select(guildId);
    QSettings settings;
    settings.setValue(QLatin1String(LastGuildKey), guildId);

    // Reopen the channel last viewed in this guild, or the call we are in, or a sensible default.
    m_channelId = settings.value(QLatin1String(LastChannelKey) + (guildId.isEmpty() ? QStringLiteral("@me") : guildId)).toString();
    if (m_voice->guildId() == guildId && !m_voice->channelId().isEmpty() && m_channelId.isEmpty())
        m_channelId = m_voice->channelId();

    if (guildId.isEmpty()) {
        // Home opens the friends list. A conversation opens only when one is clicked.
        m_sidebar->setTitle(tr("Direct Messages"));
        m_channelId.clear();
    } else {
        const Guild* guild = m_session->guild(guildId);
        m_sidebar->setTitle(guild ? guild->name : QString());
        const Channel* remembered = m_session->channel(guildId, m_channelId);
        if (!remembered) {
            m_channelId.clear();
            for (const Channel& channel : m_session->visibleChannels(guildId)) {
                if (channel.type != ChannelType::GuildCategory && !channel.isVoice()) {
                    m_channelId = channel.id;
                    break;
                }
            }
        }
    }
    m_sidebar->setSelectedChannel(m_channelId);
    m_sidebar->setFriendsVisible(guildId.isEmpty());
    m_sidebar->setFriendsSelected(guildId.isEmpty());
    refreshChannels();
    refreshCenter();
}

void MainWindow::openChannel(const QString& channelId)
{
    m_channelId = channelId;
    m_sidebar->setSelectedChannel(channelId);
    QSettings().setValue(QLatin1String(LastChannelKey) + (m_guildId.isEmpty() ? QStringLiteral("@me") : m_guildId), channelId);
    refreshCenter();
}

void MainWindow::refreshChannels()
{
    auto addCallMembers = [this](const QString& guildId, const QString& channelId) {
        for (const VoiceState& state : m_session->voiceStatesInChannel(guildId, channelId)) {
            ChannelSidebar::Member member;
            member.userId = state.userId;
            member.name = m_session->user(state.userId).displayName();
            if (member.name.isEmpty())
                member.name = tr("Unknown user");
            member.speaking = m_voice->isSpeaking(state.userId);
            member.muted = state.selfMute || state.mute;
            member.deafened = state.selfDeaf || state.deaf;
            member.avatar = memberAvatar(state.userId);
            member.nameColor = roleColor(state.userId);
            m_sidebar->addVoiceMember(member);
        }
    };

    m_sidebar->beginRebuild();
    if (m_guildId.isEmpty()) {
        m_sidebar->addCategory(QStringLiteral("direct-messages"), tr("Direct Messages"));
        // Only the most recent conversations download their pictures; older ones show initials.
        constexpr int PicturesToLoad = 60;
        int index = 0;
        for (const PrivateChannel& channel : m_session->privateChannels()) {
            const Call* call = m_session->call(channel.id);
            const bool inCall = call && !call->voiceStates.isEmpty();
            m_sidebar->addDirectMessage(channel.id, m_session->privateChannelName(channel),
                                        privateChannelAvatar(channel, 32, index++ < PicturesToLoad || inCall, true), inCall,
                                        m_session->mentionCount(channel.id));
            if (inCall)
                addCallMembers(QString(), channel.id);
        }
    } else {
        for (const Channel& channel : m_session->visibleChannels(m_guildId)) {
            if (channel.type == ChannelType::GuildCategory) {
                m_sidebar->addCategory(channel.id, channel.name, m_session->canManageChannels(m_guildId, channel.id));
                continue;
            }
            if (!channel.isVoice()) {
                m_sidebar->addChannel(channel.id, channel.name, ChannelSidebar::ItemKind::TextChannel,
                                      m_session->isUnread(m_guildId, channel.id), m_session->mentionCount(channel.id),
                                      m_session->isMuted(m_guildId, channel.id));
                continue;
            }
            m_sidebar->addChannel(channel.id, channel.name, ChannelSidebar::ItemKind::VoiceChannel);
            addCallMembers(m_guildId, channel.id);
        }
    }
    m_sidebar->endRebuild();
}

void MainWindow::refreshCenter()
{
    // Switching between the home page, a chat and a voice channel fades; refreshes of the same page do not.
    QWidget* previousPage = m_pages->currentWidget();
    auto fadeGuard = qScopeGuard([this, previousPage] {
        if (m_pages->currentWidget() != previousPage)
            Motion::fadeInWidget(m_pages->currentWidget(), Motion::Fast);
    });
    if (m_guildId.isEmpty()) {
        if (!m_session->privateChannel(m_channelId)) {
            m_sidebar->setFriendsSelected(true);
            m_pages->setCurrentWidget(m_friendsPage);
            return;
        }
        m_chatView->showChannel(QString(), m_channelId);
        m_pages->setCurrentWidget(m_chatView);
        return;
    }

    const Channel* channel = m_session->channel(m_guildId, m_channelId);
    if (!channel) {
        m_pages->setCurrentWidget(m_homePage);
        return;
    }
    if (!channel->isVoice()) {
        m_chatView->showChannel(m_guildId, m_channelId);
        m_pages->setCurrentWidget(m_chatView);
        return;
    }

    QList<ParticipantTile::Participant> participants;
    for (const VoiceState& state : m_session->voiceStatesInChannel(m_guildId, m_channelId)) {
        ParticipantTile::Participant participant;
        participant.userId = state.userId;
        participant.name = m_session->user(state.userId).displayName();
        if (participant.name.isEmpty())
            participant.name = tr("Unknown user");
        participant.picture = userPicture(state.userId);
        participant.speaking = m_voice->isSpeaking(state.userId);
        participant.muted = state.selfMute || state.mute;
        participant.deafened = state.selfDeaf || state.deaf;
        participant.nameColor = roleColor(state.userId);
        participants.append(participant);
    }
    m_voiceView->setChannelName(channel->name);
    m_voiceView->setJoinText(tr("Join Voice"));
    m_voiceView->setParticipants(participants);
    m_voiceView->setJoinState(m_voice->channelId() == m_channelId, m_session->canConnect(m_guildId, m_channelId));
    m_pages->setCurrentWidget(m_voiceView);
}

void MainWindow::refreshVoicePanel()
{
    VoicePanel* panel = m_sidebar->voicePanel();
    if (m_voice->channelId().isEmpty()) {
        panel->hide();
        return;
    }
    panel->setStatus(m_voice->state() == VoiceConnection::State::Connected ? VoicePanel::Status::Connected
                                                                           : VoicePanel::Status::Connecting);
    if (m_voice->guildId().isEmpty()) {
        panel->setLocation(locationName(QString(), m_voice->channelId()), tr("Direct Messages"));
    } else {
        const Guild* guild = m_session->guild(m_voice->guildId());
        panel->setLocation(locationName(m_voice->guildId(), m_voice->channelId()), guild ? guild->name : QString());
    }
    panel->show();
}

QString MainWindow::locationName(const QString& guildId, const QString& channelId) const
{
    if (guildId.isEmpty()) {
        const PrivateChannel* privateChannel = m_session->privateChannel(channelId);
        return privateChannel ? m_session->privateChannelName(*privateChannel) : QString();
    }
    const Channel* channel = m_session->channel(guildId, channelId);
    return channel ? channel->name : QString();
}

void MainWindow::updateIncomingCall(const QString& channelId)
{
    // Ring while someone calls us, unless we are already in that call.
    const bool ringing = m_session->isRingingSelf(channelId) && m_voice->channelId() != channelId;
    if (ringing && (!m_incomingCall->isVisible() || m_incomingCall->channelId() != channelId)) {
        const PrivateChannel* privateChannel = m_session->privateChannel(channelId);
        if (!privateChannel)
            return;
        m_incomingCall->setCaller(channelId, m_session->privateChannelName(*privateChannel),
                                  privateChannelAvatar(*privateChannel, 80));
        m_incomingCall->showAtCorner();
        m_voice->sounds()->startRinging();
    } else if (!ringing && m_incomingCall->isVisible() && m_incomingCall->channelId() == channelId) {
        m_incomingCall->hide();
        m_voice->sounds()->stopRinging();
    }
}

QPixmap MainWindow::privateChannelAvatar(const PrivateChannel& channel, int size, bool loadPicture, bool showStatus)
{
    const QString name = m_session->privateChannelName(channel);
    QImage picture;
    if (loadPicture) {
        if (channel.isGroup()) {
            if (!channel.icon.isEmpty())
                picture = m_images->image(QUrl(QStringLiteral("https://cdn.discordapp.com/channel-icons/%1/%2.png?size=128")
                                                   .arg(channel.id, channel.icon)));
        } else {
            picture = userPicture(channel.recipientIds.value(0));
        }
    }
    if (showStatus && !channel.isGroup()) {
        // Users who are not friends have no known presence: no dot, rather than a misleading "offline".
        const UserStatus status = m_session->presence(channel.recipientIds.value(0)).status;
        if (status != UserStatus::Unknown)
            return makeAvatar(name, picture, size, devicePixelRatioF(), false, Theme::instance().palette().bg1, status);
    }
    return makeAvatar(name, picture, size, devicePixelRatioF());
}

void MainWindow::refreshUserPanel()
{
    const User& self = m_session->self();
    const QString name = self.displayName().isEmpty() ? tr("Connecting…") : self.displayName();
    // The custom status replaces the status name, like in Discord.
    const UserStatus status = m_session->selfStatus();
    const CustomStatus custom = m_session->selfCustomStatus();
    QString statusText = ProfileCard::statusName(status);
    if (custom.isActive())
        statusText = custom.text.isEmpty() ? custom.emojiName : (custom.emojiName.isEmpty() ? custom.text : custom.emojiName + u' ' + custom.text);
    m_sidebar->userPanel()->setUser(name, statusText,
                                    makeAvatar(name, userPicture(self.id), 32, devicePixelRatioF(), false,
                                               Theme::instance().profilePrimary(), status));
    m_sidebar->userPanel()->setVoiceState(m_voice->isSelfMuted(), m_voice->isSelfDeafened());
}

void MainWindow::onSpeakingChanged(const QString& userId, bool speaking)
{
    m_sidebar->setMemberSpeaking(userId, speaking);
    m_voiceView->setSpeaking(userId, speaking);
}

QColor MainWindow::roleColor(const QString& userId) const
{
    if (m_guildId.isEmpty() || userId.isEmpty())
        return {};
    const Guild* guild = m_session->guild(m_guildId);
    if (!guild)
        return {};
    QStringList roleIds;
    if (const std::optional<QStringList> known = m_session->memberRoleIds(m_guildId, userId))
        roleIds = *known;
    const Role* best = nullptr;
    for (const QString& id : roleIds) {
        const auto it = guild->roles.constFind(id);
        if (it != guild->roles.cend() && it->color != 0 && (!best || it->position > best->position))
            best = &*it;
    }
    return best ? QColor::fromRgb(QRgb(best->color)) : QColor();
}

void MainWindow::reportModeration(const QString& error)
{
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Couldn't do that"), error);
}

void MainWindow::showUserMenu(const QString& userId, const QPoint& globalPosition)
{
    QMenu menu(this);
    QAction* profile = menu.addAction(tr("Profile"));
    connect(profile, &QAction::triggered, this, [this, userId, globalPosition] {
        showProfile(userId, m_guildId, globalPosition);
    });
    if (userId == m_session->self().id) {
        menu.exec(globalPosition);
        return;
    }
    menu.addSeparator();

    auto* widget = new QWidget;
    widget->setObjectName(QStringLiteral("volumeMenuWidget"));
    auto* label = new QLabel(tr("User Volume"));
    label->setObjectName(QStringLiteral("settingsSection"));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 200);
    slider->setValue(qRound(m_voice->userVolume(userId) * 100));
    auto* value = new QLabel(QStringLiteral("%1%").arg(slider->value()));
    auto* row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(value);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(label);
    layout->addLayout(row);
    widget->setMinimumWidth(220);
    connect(slider, &QSlider::valueChanged, this, [this, userId, value](int volume) {
        value->setText(QStringLiteral("%1%").arg(volume));
        m_voice->setUserVolume(userId, volume / 100.0f);
    });

    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(widget);
    menu.addAction(action);
    menu.addSeparator();
    QAction* mute = menu.addAction(tr("Mute for me"));
    mute->setCheckable(true);
    mute->setChecked(m_voice->userVolume(userId) <= 0.0f);
    connect(mute, &QAction::toggled, this, [this, userId, slider](bool muted) {
        slider->setValue(muted ? 0 : 100);
        m_voice->setUserVolume(userId, muted ? 0.0f : 1.0f);
    });

    const QString displayName = m_session->user(userId).displayName();
    QAction* block = menu.addAction(tr("Block"));
    connect(block, &QAction::triggered, this, [this, userId, displayName] {
        if (QMessageBox::question(this, tr("Block"), tr("Block %1?").arg(displayName)) != QMessageBox::Yes)
            return;
        m_session->blockUser(userId, [this](const QString& error) { reportModeration(error); });
    });

    const Guild* guild = m_guildId.isEmpty() ? nullptr : m_session->guild(m_guildId);
    if (!guild) {
        menu.exec(globalPosition);
        return;
    }

    const QStringList selfRoles = guild->selfRoleIds;
    const std::optional<QStringList> knownRoles = m_session->memberRoleIds(m_guildId, userId);
    const QStringList targetRoles = knownRoles.value_or(QStringList());
    const bool owner = guild->ownerId == m_session->self().id;
    auto topPosition = [guild](const QStringList& roleIds) {
        int best = guild->roles.value(guild->id).position;
        for (const QString& id : roleIds) {
            const auto it = guild->roles.constFind(id);
            if (it != guild->roles.cend())
                best = std::max(best, it->position);
        }
        return best;
    };
    const bool outranks = userId != m_session->self().id && guild->ownerId != userId
        && (owner || topPosition(selfRoles) > topPosition(targetRoles));
    const quint64 guildPerms = Permissions::guildPermissions(*guild, m_session->self().id, selfRoles);
    const QString viewed = m_channelId;

    const VoiceState* voice = nullptr;
    if (const auto it = guild->voiceStates.constFind(userId); it != guild->voiceStates.cend())
        voice = &*it;

    if (outranks && (guildPerms & Permissions::MuteMembers)) {
        const bool serverMuted = voice && voice->mute;
        QAction* serverMute = menu.addAction(serverMuted ? tr("Server Unmute") : tr("Server Mute"));
        connect(serverMute, &QAction::triggered, this, [this, userId, viewed, serverMuted] {
            m_session->setServerMute(m_guildId, userId, viewed, !serverMuted, [this](const QString& error) { reportModeration(error); });
        });
    }
    if (outranks && (guildPerms & Permissions::ModerateMembers)) {
        QMenu* timeout = menu.addMenu(tr("Timeout"));
        const struct { const char* label; int seconds; } choices[] = {
            {QT_TR_NOOP("60 seconds"), 60},
            {QT_TR_NOOP("5 minutes"), 5 * 60},
            {QT_TR_NOOP("1 hour"), 60 * 60},
            {QT_TR_NOOP("1 day"), 24 * 60 * 60},
            {QT_TR_NOOP("1 week"), 7 * 24 * 60 * 60},
        };
        for (const auto& choice : choices) {
            QAction* action = timeout->addAction(tr(choice.label));
            connect(action, &QAction::triggered, this, [this, userId, viewed, seconds = choice.seconds] {
                m_session->setTimeout(m_guildId, userId, viewed, seconds, [this](const QString& error) { reportModeration(error); });
            });
        }
        timeout->addSeparator();
        QAction* clear = timeout->addAction(tr("Remove timeout"));
        connect(clear, &QAction::triggered, this, [this, userId, viewed] {
            m_session->setTimeout(m_guildId, userId, viewed, 0, [this](const QString& error) { reportModeration(error); });
        });
    }
    if (outranks && (guildPerms & Permissions::MoveMembers)) {
        QMenu* move = menu.addMenu(tr("Move to"));
        if (voice && !voice->channelId.isEmpty()) {
            QAction* disconnect = move->addAction(tr("Disconnect"));
            connect(disconnect, &QAction::triggered, this, [this, userId, viewed] {
                m_session->moveMember(m_guildId, userId, viewed, QString(), [this](const QString& error) { reportModeration(error); });
            });
            move->addSeparator();
        }
        for (const Channel& candidate : m_session->visibleChannels(m_guildId)) {
            if (!candidate.isVoice() || (voice && candidate.id == voice->channelId))
                continue;
            if (!(Permissions::compute(*guild, candidate, m_session->self().id) & Permissions::Connect))
                continue;
            QAction* action = move->addAction(candidate.name);
            connect(action, &QAction::triggered, this, [this, userId, viewed, channelId = candidate.id] {
                m_session->moveMember(m_guildId, userId, viewed, channelId, [this](const QString& error) { reportModeration(error); });
            });
        }
    }
    if (outranks && (guildPerms & Permissions::ManageRoles)) {
        QList<Role> assignable;
        const int ceiling = owner ? std::numeric_limits<int>::max() : topPosition(selfRoles);
        for (const Role& role : guild->roles) {
            if (role.id == guild->id || role.managed || role.position >= ceiling)
                continue;
            assignable.append(role);
        }
        if (!assignable.isEmpty()) {
            std::sort(assignable.begin(), assignable.end(), [](const Role& a, const Role& b) { return a.position > b.position; });
            QMenu* roles = menu.addMenu(tr("Roles"));
            for (const Role& role : assignable) {
                QAction* action = roles->addAction(role.name);
                action->setCheckable(true);
                action->setChecked(targetRoles.contains(role.id));
                connect(action, &QAction::toggled, this, [this, userId, viewed, roleId = role.id](bool on) {
                    m_session->setMemberRole(m_guildId, userId, viewed, roleId, on, [this](const QString& error) { reportModeration(error); });
                });
            }
        }
    }
    if (outranks && (guildPerms & Permissions::KickMembers)) {
        QAction* kick = menu.addAction(tr("Kick"));
        connect(kick, &QAction::triggered, this, [this, userId, viewed, displayName] {
            if (QMessageBox::question(this, tr("Kick"), tr("Kick %1 from the server?").arg(displayName)) != QMessageBox::Yes)
                return;
            m_session->kickMember(m_guildId, userId, viewed, [this](const QString& error) { reportModeration(error); });
        });
    }
    if (outranks && (guildPerms & Permissions::BanMembers)) {
        QAction* ban = menu.addAction(tr("Ban"));
        connect(ban, &QAction::triggered, this, [this, userId, viewed, displayName] {
            if (QMessageBox::question(this, tr("Ban"), tr("Ban %1 from the server?").arg(displayName)) != QMessageBox::Yes)
                return;
            m_session->banMember(m_guildId, userId, viewed, [this](const QString& error) { reportModeration(error); });
        });
    }
    menu.exec(globalPosition);
}

void MainWindow::showProfile(const QString& userId, const QString& guildId, const QPoint& globalPosition)
{
    if (userId.isEmpty())
        return;
    auto* popup = new ProfilePopup(m_session, m_images, userId, guildId, this);
    connectProfilePopup(popup);
    popup->popupAt(globalPosition);
}

void MainWindow::showOwnProfile()
{
    // In a server, the own profile shows that server's roles too.
    auto* popup = new ProfilePopup(m_session, m_images, m_session->self().id, m_guildId, this);
    connectProfilePopup(popup);
    popup->popupAbove(m_sidebar->userPanel());
}

void MainWindow::connectProfilePopup(ProfilePopup* popup)
{
    // A popup still waiting for its profile is dropped when another user is clicked meanwhile.
    if (m_pendingProfile && !m_pendingProfile->isVisible())
        m_pendingProfile->deleteLater();
    m_pendingProfile = popup;
    // Dialogs open after the popup has closed, outside of its event handling.
    connect(popup, &ProfilePopup::editProfileRequested, this, &MainWindow::openProfileEditor, Qt::QueuedConnection);
    connect(popup, &ProfilePopup::customStatusRequested, this, &MainWindow::openCustomStatus, Qt::QueuedConnection);
    connect(popup, &ProfilePopup::openChannelRequested, this, [this](const QString& channelId) {
        if (!channelId.isEmpty())
            showChannel(QString(), channelId);
    });
    connect(popup, &ProfilePopup::linkActivated, this, [](const QString& url) {
        if (url.startsWith(u"http://") || url.startsWith(u"https://"))
            QDesktopServices::openUrl(QUrl(url));
    });
}

void MainWindow::openProfileEditor()
{
    ProfileEditDialog dialog(m_session, m_images, this);
    dialog.exec();
}

void MainWindow::openCustomStatus()
{
    CustomStatusDialog dialog(m_session, m_images, this);
    dialog.exec();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_voice, this);
    // Queued: logging out destroys this window, which must not happen inside the dialog's event loop.
    connect(&dialog, &SettingsDialog::logoutRequested, this, &MainWindow::logoutRequested, Qt::QueuedConnection);
    dialog.exec();
}

QImage MainWindow::userPicture(const QString& userId)
{
    return m_images->image(ImageCache::avatarUrl(m_session->user(userId)));
}

QPixmap MainWindow::memberAvatar(const QString& userId)
{
    // The speaking ring is drawn (and animated) by the channel list itself.
    const User user = m_session->user(userId);
    return makeAvatar(user.displayName(), userPicture(userId), 24, devicePixelRatioF());
}
