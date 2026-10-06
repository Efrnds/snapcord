#include "MainWindow.h"

#include "Avatar.h"
#include "ChannelSidebar.h"
#include "ConnectionInfoPopup.h"
#include "ImageCache.h"
#include "IncomingCallWindow.h"
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
    , m_incomingCall(new IncomingCallWindow(this))
{
    setWindowTitle(QStringLiteral("Snapcord"));
    resize(1280, 720);
    setMinimumSize(940, 500);

    m_homePage = buildPlaceholderPage(tr("Direct Messages"),
                                      tr("Pick a conversation on the left to start a voice call. "
                                         "Text chat arrives in Phase 3."));
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
    connect(m_session, &Session::privateChannelsChanged, this, [this, scheduleRefresh] {
        if (m_guildId.isEmpty())
            scheduleRefresh();
    });
    connect(m_session, &Session::callChanged, this, [this, scheduleRefresh](const QString& channelId) {
        updateIncomingCall(channelId);
        if (m_guildId.isEmpty() || channelId == m_voice->channelId())
            scheduleRefresh();
    });

    connect(m_incomingCall, &IncomingCallWindow::accepted, this, [this](const QString& channelId) {
        m_incomingCall->hide();
        m_voice->sounds()->stopRinging();
        m_voice->join(QString(), channelId);
        selectGuild(QString());
        onChannelClicked(channelId, false);
    });
    connect(m_incomingCall, &IncomingCallWindow::declined, this, [this](const QString& channelId) {
        m_incomingCall->hide();
        m_voice->sounds()->stopRinging();
        m_session->declineCall(channelId);
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

    connect(m_rail, &ServerRail::serverSelected, this, &MainWindow::selectGuild);
    connect(m_rail, &ServerRail::homeSelected, this, [this] { selectGuild(QString()); });

    connect(m_sidebar, &ChannelSidebar::channelClicked, this, [this](const QString& id, ChannelSidebar::ItemKind kind) {
        onChannelClicked(id, kind == ChannelSidebar::ItemKind::VoiceChannel);
    });
    connect(m_sidebar, &ChannelSidebar::memberContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::participantContextMenuRequested, this, &MainWindow::showUserMenu);
    connect(m_voiceView, &VoiceChannelView::joinRequested, this, [this] {
        if (m_guildId.isEmpty())
            m_voice->startCall(m_channelId);
        else
            m_voice->join(m_guildId, m_channelId);
    });
    connect(m_sidebar->voicePanel(), &VoicePanel::detailsRequested, this, [this] {
        auto* popup = new ConnectionInfoPopup(m_voice, this);
        popup->showAbove(m_sidebar->voicePanel());
    });

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
        // Show the private call we're in, if any.
        if (!m_voice->channelId().isEmpty() && m_voice->guildId().isEmpty())
            m_channelId = m_voice->channelId();
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
            member.avatar = memberAvatar(state.userId, member.speaking);
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
                                        privateChannelAvatar(channel, 32, index++ < PicturesToLoad || inCall),
                                        inCall);
            if (inCall)
                addCallMembers(QString(), channel.id);
        }
    } else {
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
            addCallMembers(m_guildId, channel.id);
        }
    }
    m_sidebar->endRebuild();
}

void MainWindow::refreshVoiceView()
{
    QString title;
    QString joinText;
    if (m_guildId.isEmpty()) {
        const PrivateChannel* privateChannel = m_session->privateChannel(m_channelId);
        if (!privateChannel) {
            m_pages->setCurrentWidget(m_homePage);
            return;
        }
        const Call* call = m_session->call(m_channelId);
        title = m_session->privateChannelName(*privateChannel);
        joinText = call && !call->voiceStates.isEmpty() ? tr("Join Call") : tr("Start Call");
    } else {
        const Channel* channel = m_session->channel(m_guildId, m_channelId);
        if (!channel || !channel->isVoice()) {
            m_pages->setCurrentWidget(m_textPage);
            return;
        }
        title = channel->name;
        joinText = tr("Join Voice");
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
        participants.append(participant);
    }
    m_voiceView->setChannelName(title);
    m_voiceView->setJoinText(joinText);
    m_voiceView->setParticipants(participants);
    const bool joined = m_voice->channelId() == m_channelId;
    m_voiceView->setJoinState(joined, m_session->canConnect(m_guildId, m_channelId));
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

QPixmap MainWindow::privateChannelAvatar(const PrivateChannel& channel, int size, bool loadPicture)
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
    return makeAvatar(name, picture, size, devicePixelRatioF());
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
    m_sidebar->setSelectedChannel(channelId);
    // Discord joins a guild voice channel on a single click; private calls start with the "Start Call" button.
    if (isVoice && m_session->canConnect(m_guildId, channelId))
        m_voice->join(m_guildId, channelId);
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
