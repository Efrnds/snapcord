#include "MainWindow.h"

#include "Avatar.h"
#include "ChannelSidebar.h"
#include "ImageCache.h"
#include "ServerRail.h"
#include "SettingsDialog.h"
#include "UserPanel.h"
#include "VoiceChannelView.h"
#include "VoiceController.h"
#include "VoicePanel.h"
#include "core/Session.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

constexpr auto LastGuildKey = "ui/lastGuild";

} // namespace

MainWindow::MainWindow(Session* session, VoiceController* voice, QWidget* parent)
    : QMainWindow(parent)
    , m_session(session)
    , m_voice(voice)
    , m_images(new ImageCache(this))
    , m_rail(new ServerRail)
    , m_sidebar(new ChannelSidebar)
    , m_pages(new QStackedWidget)
    , m_voiceView(new VoiceChannelView)
{
    setWindowTitle(QStringLiteral("Snapcord"));
    resize(1280, 720);
    setMinimumSize(940, 500);

    m_homePage = buildPlaceholderPage(tr("Direct Messages"), tr("Direct messages and calls arrive in Phase 2."));
    m_textPage = buildPlaceholderPage(tr("Text Channels"), tr("Text chat arrives in Phase 3. For now, Snapcord "
                                                              "focuses on voice channels."));
    m_pages->addWidget(m_homePage);
    m_pages->addWidget(m_textPage);
    m_pages->addWidget(m_voiceView);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    m_sidebar->setTitle(tr("Connecting…"));

    // Several Gateway events often arrive together; refresh the lists once per burst.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        refreshChannels();
        refreshVoiceView();
        refreshVoicePanel();
        refreshUserPanel();
    });
    auto scheduleRefresh = [this] { m_refreshTimer.start(); };

    connect(m_session, &Session::ready, this, [this] {
        rebuildServerRail();
        const QString lastGuild = QSettings().value(QLatin1String(LastGuildKey)).toString();
        selectGuild(m_session->guild(lastGuild) ? lastGuild : m_session->guildOrder().value(0));
        refreshUserPanel();
    });
    connect(m_session, &Session::guildListChanged, this, &MainWindow::rebuildServerRail);
    connect(m_session, &Session::guildChanged, this, [this, scheduleRefresh](const QString& guildId) {
        if (guildId == m_guildId || guildId == m_voice->guildId())
            scheduleRefresh();
    });
    connect(m_session, &Session::voiceStatesChanged, this, [this, scheduleRefresh](const QString& guildId) {
        if (guildId == m_guildId)
            scheduleRefresh();
    });
    connect(m_session, &Session::usersChanged, this, scheduleRefresh);
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

    connect(m_rail, &ServerRail::serverSelected, this, &MainWindow::selectGuild);
    connect(m_rail, &ServerRail::homeSelected, this, [this] { selectGuild(QString()); });

    connect(m_sidebar, &ChannelSidebar::channelClicked, this, [this](const QString& id, ChannelSidebar::ItemKind kind) {
        onChannelClicked(id, kind == ChannelSidebar::ItemKind::VoiceChannel);
    });
    connect(m_sidebar, &ChannelSidebar::memberContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::participantContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::joinRequested, this, [this] { m_voice->join(m_guildId, m_channelId); });

    UserPanel* userPanel = m_sidebar->userPanel();
    connect(userPanel, &UserPanel::muteClicked, m_voice, &VoiceController::toggleMute);
    connect(userPanel, &UserPanel::deafenClicked, m_voice, &VoiceController::toggleDeafen);
    connect(userPanel, &UserPanel::settingsRequested, this, &MainWindow::openSettings);
    connect(m_sidebar->voicePanel(), &VoicePanel::disconnectRequested, m_voice, &VoiceController::leave);

    connect(m_voice, &VoiceController::selfStateChanged, this, &MainWindow::refreshUserPanel);
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
    m_rail->clearServers();
    for (const QString& id : m_session->guildOrder()) {
        const Guild* guild = m_session->guild(id);
        if (!guild || guild->unavailable)
            continue;
        m_rail->addServer(id, guild->name, m_images->image(ImageCache::guildIconUrl(id, guild->icon)));
    }
    m_rail->select(m_guildId);
}

void MainWindow::selectGuild(const QString& guildId)
{
    m_guildId = guildId;
    m_channelId.clear();
    m_rail->select(guildId);
    QSettings().setValue(QLatin1String(LastGuildKey), guildId);

    if (guildId.isEmpty()) {
        m_sidebar->setTitle(tr("Direct Messages"));
        m_pages->setCurrentWidget(m_homePage);
    } else {
        const Guild* guild = m_session->guild(guildId);
        m_sidebar->setTitle(guild ? guild->name : QString());
        // Show the voice channel we're connected to, if it's in this guild; otherwise the first channel.
        if (m_voice->guildId() == guildId) {
            m_channelId = m_voice->channelId();
        } else {
            for (const Channel& channel : m_session->visibleChannels(guildId)) {
                if (channel.type != ChannelType::GuildCategory) {
                    m_channelId = channel.id;
                    break;
                }
            }
        }
    }
    m_sidebar->setSelectedChannel(m_channelId);
    refreshChannels();
    refreshVoiceView();
}

void MainWindow::refreshChannels()
{
    m_sidebar->beginRebuild();
    if (!m_guildId.isEmpty()) {
        for (const Channel& channel : m_session->visibleChannels(m_guildId)) {
            if (channel.type == ChannelType::GuildCategory) {
                m_sidebar->addCategory(channel.id, channel.name);
                continue;
            }
            if (!channel.isVoice()) {
                m_sidebar->addChannel(channel.id, channel.name, ChannelSidebar::ItemKind::TextChannel);
                continue;
            }
            m_sidebar->addChannel(channel.id, channel.name, ChannelSidebar::ItemKind::VoiceChannel);
            for (const VoiceState& state : m_session->voiceStatesInChannel(m_guildId, channel.id)) {
                ChannelSidebar::Member member;
                member.userId = state.userId;
                member.name = m_session->user(state.userId).displayName();
                if (member.name.isEmpty())
                    member.name = tr("Unknown user");
                member.speaking = m_voice->isSpeaking(state.userId);
                member.muted = state.selfMute || state.mute;
                member.deafened = state.selfDeaf || state.deaf;
                member.avatar = memberAvatar(state.userId, member.speaking);
                m_sidebar->addVoiceMember(member);
            }
        }
    }
    m_sidebar->endRebuild();
}

void MainWindow::refreshVoiceView()
{
    const Channel* channel = m_guildId.isEmpty() ? nullptr : m_session->channel(m_guildId, m_channelId);
    if (!channel) {
        if (!m_guildId.isEmpty() && m_pages->currentWidget() != m_textPage)
            m_pages->setCurrentWidget(m_textPage);
        return;
    }
    if (!channel->isVoice()) {
        m_pages->setCurrentWidget(m_textPage);
        return;
    }

    QList<ParticipantTile::Participant> participants;
    for (const VoiceState& state : m_session->voiceStatesInChannel(m_guildId, channel->id)) {
        ParticipantTile::Participant participant;
        participant.userId = state.userId;
        participant.name = m_session->user(state.userId).displayName();
        if (participant.name.isEmpty())
            participant.name = tr("Unknown user");
        participant.picture = userPicture(state.userId);
        participant.speaking = m_voice->isSpeaking(state.userId);
        participant.muted = state.selfMute || state.mute;
        participant.deafened = state.selfDeaf || state.deaf;
        participants.append(participant);
    }
    m_voiceView->setChannelName(channel->name);
    m_voiceView->setParticipants(participants);
    const bool joined = m_voice->channelId() == channel->id;
    m_voiceView->setJoinState(joined, m_session->canConnect(m_guildId, channel->id));
    m_pages->setCurrentWidget(m_voiceView);
}

void MainWindow::refreshVoicePanel()
{
    VoicePanel* panel = m_sidebar->voicePanel();
    if (m_voice->channelId().isEmpty()) {
        panel->hide();
        return;
    }
    const Guild* guild = m_session->guild(m_voice->guildId());
    const Channel* channel = m_session->channel(m_voice->guildId(), m_voice->channelId());
    panel->setStatus(m_voice->state() == VoiceConnection::State::Connected ? VoicePanel::Status::Connected
                                                                           : VoicePanel::Status::Connecting);
    panel->setLocation(channel ? channel->name : QString(), guild ? guild->name : QString());
    panel->show();
}

void MainWindow::refreshUserPanel()
{
    const User& self = m_session->self();
    const QString name = self.displayName().isEmpty() ? tr("Connecting…") : self.displayName();
    m_sidebar->userPanel()->setUser(name, tr("Online"),
                                    makeAvatar(name, userPicture(self.id), 32, devicePixelRatioF(), false,
                                               QColor(0x23, 0x24, 0x28)));
    m_sidebar->userPanel()->setVoiceState(m_voice->isSelfMuted(), m_voice->isSelfDeafened());
}

void MainWindow::onChannelClicked(const QString& channelId, bool isVoice)
{
    m_channelId = channelId;
    if (isVoice && m_session->canConnect(m_guildId, channelId))
        m_voice->join(m_guildId, channelId); // Discord joins a voice channel on a single click
    refreshVoiceView();
}

void MainWindow::onSpeakingChanged(const QString& userId, bool speaking)
{
    m_sidebar->setMemberSpeaking(userId, speaking, memberAvatar(userId, speaking));
    m_voiceView->setSpeaking(userId, speaking);
}

void MainWindow::showUserMenu(const QString& userId, const QPoint& globalPosition)
{
    if (userId == m_session->self().id)
        return;

    QMenu menu(this);
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
    QAction* mute = menu.addAction(tr("Mute"));
    mute->setCheckable(true);
    mute->setChecked(m_voice->userVolume(userId) <= 0.0f);
    connect(mute, &QAction::toggled, this, [this, userId, slider](bool muted) {
        slider->setValue(muted ? 0 : 100);
        m_voice->setUserVolume(userId, muted ? 0.0f : 1.0f);
    });
    menu.exec(globalPosition);
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

QPixmap MainWindow::memberAvatar(const QString& userId, bool speaking)
{
    const User user = m_session->user(userId);
    return makeAvatar(user.displayName(), userPicture(userId), 24, devicePixelRatioF(), speaking);
}
