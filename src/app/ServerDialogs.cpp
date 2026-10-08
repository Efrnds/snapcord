#include "ServerDialogs.h"

#include "Avatar.h"
#include "ImageCache.h"
#include "ServerRail.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "core/Session.h"

#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("settingsSection"));
    return label;
}

QLabel* titleLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("dialogTitle"));
    label->setWordWrap(true);
    return label;
}

QLabel* hintLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("settingsHint"));
    label->setWordWrap(true);
    return label;
}

QPushButton* button(const QString& text, const char* objectName)
{
    auto* result = new QPushButton(text);
    result->setObjectName(QLatin1String(objectName));
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

void setupDialog(QDialog* dialog, const QString& title)
{
    dialog->setObjectName(QStringLiteral("profileEditor"));
    dialog->setAttribute(Qt::WA_StyledBackground);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
}

// Discord's folder color choices.
constexpr int FolderColors[] = {0x5865F2, 0x1ABC9C, 0x2ECC71, 0x3498DB, 0x9B59B6, 0xE91E63, 0xF1C40F, 0xE67E22,
                                0xE74C3C, 0x95A5A6, 0x607D8B, 0x11806A, 0x1F8B4C, 0x206694, 0x71368A, 0xAD1457,
                                0xC27C0E, 0xA84300, 0x992D22, 0x979C9F, 0x546E7A};

// Text channel names are lowercase with dashes for spaces; the official client changes them while typing.
void keepTextChannelName(QLineEdit* field, const std::function<bool()>& isText)
{
    QObject::connect(field, &QLineEdit::textEdited, field, [field, isText](const QString& text) {
        if (!isText())
            return;
        static const QRegularExpression spaces(QStringLiteral("\\s+"));
        const QString fixed = text.toLower().replace(spaces, QStringLiteral("-"));
        if (fixed != text) {
            const int cursor = field->cursorPosition();
            field->setText(fixed);
            field->setCursorPosition(std::min<int>(cursor, fixed.size()));
        }
    });
}

QLabel* errorLabel()
{
    auto* label = new QLabel;
    label->setObjectName(QStringLiteral("profileError"));
    label->setWordWrap(true);
    label->hide();
    return label;
}

QString channelLabel(const Channel& channel)
{
    if (channel.type == ChannelType::GuildCategory || channel.isVoice())
        return channel.name;
    return u'#' + channel.name;
}

} // namespace

// --- JoinServerDialog ------------------------------------------------------------------------------

JoinServerDialog::JoinServerDialog(Session* session, ImageCache* images, const QString& code, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_images(images)
    , m_pages(new QStackedWidget)
    , m_link(new QLineEdit)
    , m_error(new QLabel)
    , m_join(button(tr("Join Server"), "brandButton"))
    , m_previewIcon(new QLabel)
    , m_previewInviter(hintLabel({}))
    , m_previewName(titleLabel({}))
    , m_previewCounts(hintLabel({}))
    , m_acceptButton(button(tr("Accept Invite"), "brandButton"))
{
    setupDialog(this, tr("Join a Server"));
    m_error->setObjectName(QStringLiteral("profileError"));
    m_error->setWordWrap(true);
    m_error->hide();

    // Page 1: the invite link.
    auto* input = new QWidget;
    auto* subtitle = hintLabel(tr("Enter an invite below to join an existing server"));
    auto* title = titleLabel(tr("Join a Server"));
    title->setAlignment(Qt::AlignCenter);
    subtitle->setAlignment(Qt::AlignCenter);
    m_link->setPlaceholderText(QStringLiteral("https://discord.gg/hTKzmak"));
    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* inputButtons = new QHBoxLayout;
    inputButtons->addWidget(cancel);
    inputButtons->addStretch();
    inputButtons->addWidget(m_join);
    auto* inputLayout = new QVBoxLayout(input);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(6);
    inputLayout->addWidget(title);
    inputLayout->addWidget(subtitle);
    inputLayout->addSpacing(16);
    inputLayout->addWidget(sectionLabel(tr("Invite link")));
    inputLayout->addWidget(m_link);
    inputLayout->addSpacing(8);
    inputLayout->addWidget(sectionLabel(tr("Invites should look like")));
    inputLayout->addWidget(hintLabel(QStringLiteral("hTKzmak\nhttps://discord.gg/hTKzmak\nhttps://discord.gg/cool-people")));
    inputLayout->addStretch();
    inputLayout->addLayout(inputButtons);

    // Page 2: the invite's server, before accepting.
    auto* preview = new QWidget;
    m_previewIcon->setFixedSize(64, 64);
    m_previewIcon->setAlignment(Qt::AlignCenter);
    m_previewInviter->setAlignment(Qt::AlignCenter);
    m_previewName->setAlignment(Qt::AlignCenter);
    m_previewCounts->setAlignment(Qt::AlignCenter);
    auto* previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);
    previewLayout->addStretch();
    previewLayout->addWidget(m_previewIcon, 0, Qt::AlignHCenter);
    previewLayout->addWidget(m_previewInviter);
    previewLayout->addWidget(m_previewName);
    previewLayout->addWidget(m_previewCounts);
    previewLayout->addStretch();
    previewLayout->addWidget(m_acceptButton);

    // Page 3: looking the invite up.
    auto* loading = hintLabel(tr("Loading invite…"));
    loading->setAlignment(Qt::AlignCenter);

    m_pages->addWidget(input);
    m_pages->addWidget(preview);
    m_pages->addWidget(loading);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->addWidget(m_pages, 1);
    layout->addWidget(m_error);
    resize(440, 400);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_join, &QPushButton::clicked, this, &JoinServerDialog::joinTyped);
    connect(m_link, &QLineEdit::returnPressed, this, &JoinServerDialog::joinTyped);
    connect(m_acceptButton, &QPushButton::clicked, this, [this] { acceptInvite(m_invite); });
    connect(m_images, &ImageCache::imageLoaded, this, [this](const QUrl& url) {
        if (!m_invite.guildIcon.isEmpty() && url == ImageCache::guildIconUrl(m_invite.guildId, m_invite.guildIcon))
            showPreview(m_invite);
    });

    if (code.isEmpty())
        return;
    m_pages->setCurrentIndex(2);
    QPointer<JoinServerDialog> guard(this);
    m_session->fetchInvite(code, false, [guard](const InviteInfo* invite, const QString& error) {
        if (!guard)
            return;
        if (invite) {
            guard->showPreview(*invite);
        } else {
            guard->m_pages->setCurrentIndex(0);
            guard->showError(error);
        }
    });
}

void JoinServerDialog::joinTyped()
{
    const QString code = Invites::codeFromText(m_link->text());
    if (code.isEmpty()) {
        showError(tr("Please enter a valid invite link or invite code."));
        return;
    }
    m_error->hide();
    m_join->setEnabled(false);
    // Like the official dialog: the invite is looked up, then accepted right away.
    QPointer<JoinServerDialog> guard(this);
    m_session->fetchInvite(code, true, [guard](const InviteInfo* invite, const QString& error) {
        if (!guard)
            return;
        if (!invite) {
            guard->m_join->setEnabled(true);
            guard->showError(error);
            return;
        }
        guard->acceptInvite(*invite);
    });
}

void JoinServerDialog::showPreview(const InviteInfo& invite)
{
    m_invite = invite;
    const QString name = invite.guildName.isEmpty() ? invite.channelName : invite.guildName;
    const QImage icon = invite.guildIcon.isEmpty() ? QImage()
                                                   : m_images->image(ImageCache::guildIconUrl(invite.guildId, invite.guildIcon));
    m_previewIcon->setPixmap(makeAvatar(name, icon, 64, devicePixelRatioF()));
    m_previewInviter->setText(invite.inviterName.isEmpty() ? tr("You've been invited to join")
                                                           : tr("%1 invited you to join").arg(invite.inviterName));
    m_previewName->setText(name);
    QStringList counts;
    if (invite.onlineCount >= 0)
        counts.append(tr("%n Online", nullptr, invite.onlineCount));
    if (invite.memberCount >= 0)
        counts.append(tr("%n Members", nullptr, invite.memberCount));
    m_previewCounts->setText(counts.join(QStringLiteral("  ·  ")));
    const bool member = !invite.guildId.isEmpty() && m_session->guild(invite.guildId);
    m_acceptButton->setText(member ? tr("Go to Server") : tr("Accept Invite"));
    m_pages->setCurrentIndex(1);
}

void JoinServerDialog::acceptInvite(const InviteInfo& invite)
{
    // Already a member: just go there.
    if (!invite.guildId.isEmpty() && m_session->guild(invite.guildId)) {
        emit joined(invite.guildId, {});
        done(QDialog::Accepted);
        return;
    }
    m_acceptButton->setEnabled(false);
    m_join->setEnabled(false);
    QPointer<JoinServerDialog> guard(this);
    m_session->acceptInvite(invite, {}, [guard, invite](const QString& error) {
        if (!guard)
            return;
        guard->m_acceptButton->setEnabled(true);
        guard->m_join->setEnabled(true);
        if (!error.isEmpty()) {
            guard->showError(error);
            return;
        }
        emit guard->joined(invite.guildId, invite.channelId);
        guard->done(QDialog::Accepted);
    });
}

void JoinServerDialog::showError(const QString& error)
{
    m_error->setText(error);
    m_error->show();
}

// --- InviteDialog ----------------------------------------------------------------------------------

InviteDialog::InviteDialog(Session* session, const QString& guildId, const QString& channelId, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_search(new QLineEdit)
    , m_conversations(new QListWidget)
    , m_link(new QLineEdit)
    , m_copy(button(tr("Copy"), "brandButton"))
    , m_hint(hintLabel(tr("Creating invite…")))
{
    const Guild* guild = session->guild(guildId);
    const QString guildName = guild ? guild->name : QString();
    setupDialog(this, tr("Invite People"));

    m_search->setPlaceholderText(tr("Search for friends"));
    m_search->setClearButtonEnabled(true);
    m_conversations->setObjectName(QStringLiteral("inviteList"));
    m_conversations->setSelectionMode(QAbstractItemView::NoSelection);
    m_conversations->setFocusPolicy(Qt::NoFocus);
    m_link->setReadOnly(true);
    m_copy->setEnabled(false);

    auto* linkRow = new QHBoxLayout;
    linkRow->addWidget(m_link, 1);
    linkRow->addWidget(m_copy);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(8);
    layout->addWidget(titleLabel(tr("Invite friends to %1").arg(guildName)));
    layout->addWidget(m_search);
    layout->addWidget(m_conversations, 1);
    layout->addSpacing(8);
    layout->addWidget(sectionLabel(tr("Or, send a server invite link to a friend")));
    layout->addLayout(linkRow);
    layout->addWidget(m_hint);
    resize(460, 520);

    connect(m_search, &QLineEdit::textChanged, this, &InviteDialog::fillConversations);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_link->text());
        m_copy->setText(tr("Copied"));
        QTimer::singleShot(1500, m_copy, [copy = m_copy] { copy->setText(tr("Copy")); });
    });
    fillConversations({});

    // The official dialog creates the link as soon as it opens.
    QPointer<InviteDialog> guard(this);
    session->createInvite(channelId, [guard](const InviteInfo* invite, const QString& error) {
        if (!guard)
            return;
        if (!invite) {
            guard->m_hint->setText(tr("Could not create an invite: %1").arg(error));
            return;
        }
        guard->m_code = invite->code;
        guard->m_link->setText(Invites::link(invite->code));
        guard->m_copy->setEnabled(true);
        guard->m_hint->setText(tr("Your invite link expires in 7 days."));
        guard->fillConversations(guard->m_search->text());
    });
}

void InviteDialog::fillConversations(const QString& filter)
{
    m_conversations->clear();
    for (const PrivateChannel& channel : m_session->privateChannels()) {
        const QString name = m_session->privateChannelName(channel);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
            continue;
        auto* row = new QWidget;
        auto* label = new QLabel(name);
        label->setObjectName(QStringLiteral("inviteName"));
        const bool sent = m_sentTo.contains(channel.id);
        auto* invite = button(sent ? tr("Sent") : tr("Invite"), sent ? "secondaryButton" : "primaryButton");
        invite->setEnabled(!sent && !m_code.isEmpty());
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 4);
        rowLayout->addWidget(label, 1);
        rowLayout->addWidget(invite);
        auto* item = new QListWidgetItem(m_conversations);
        item->setSizeHint(row->sizeHint());
        m_conversations->setItemWidget(item, row);
        // The official dialog sends the link as a normal message in that conversation.
        connect(invite, &QPushButton::clicked, this, [this, invite, id = channel.id] {
            m_session->messages()->send(id, QString(), Invites::link(m_code), QString());
            m_sentTo.append(id);
            invite->setText(tr("Sent"));
            invite->setObjectName(QStringLiteral("secondaryButton"));
            invite->style()->unpolish(invite); // restyle with the new object name
            invite->style()->polish(invite);
            invite->setEnabled(false);
        });
    }
}

// --- FolderSettingsDialog --------------------------------------------------------------------------

FolderSettingsDialog::FolderSettingsDialog(const GuildFolder& folder, const QString& serverNames, QWidget* parent)
    : QDialog(parent)
    , m_folder(folder)
    , m_name(new QLineEdit)
    , m_custom(new ColorSwatch)
{
    setupDialog(this, tr("Folder Settings"));
    setAttribute(Qt::WA_DeleteOnClose, false); // the caller reads the result
    m_name->setPlaceholderText(serverNames);
    m_name->setText(folder.name);
    m_name->setMaxLength(32);

    auto* colors = new QGridLayout;
    colors->setSpacing(8);
    int index = 0;
    for (const int color : FolderColors) {
        auto* swatch = new ColorSwatch;
        swatch->setSwatchColor(QColor::fromRgb(QRgb(color)));
        connect(swatch, &QPushButton::clicked, this, [this, color] { selectColor(color); });
        // The first one is the default color, bigger like Discord's.
        if (index == 0) {
            swatch->setFixedSize(66, 88);
            colors->addWidget(swatch, 0, 0, 2, 1);
        } else {
            colors->addWidget(swatch, (index - 1) / 10, 1 + (index - 1) % 10);
        }
        m_swatches.append(swatch);
        ++index;
    }
    m_custom->setToolTip(tr("Custom color"));
    colors->addWidget(m_custom, 0, 11, 2, 1);
    m_custom->setFixedSize(66, 88);
    connect(m_custom, &QPushButton::clicked, this, [this] {
        const QColor current = m_folder.color >= 0 ? QColor::fromRgb(QRgb(m_folder.color)) : ServerRail::defaultFolderColor();
        const QColor color = QColorDialog::getColor(current, this, tr("Folder Color"));
        if (color.isValid())
            selectColor(int(color.rgb() & 0xffffff));
    });

    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* done = button(tr("Done"), "brandButton");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(done);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(8);
    layout->addWidget(titleLabel(tr("Folder Settings")));
    layout->addSpacing(8);
    layout->addWidget(sectionLabel(tr("Folder Name")));
    layout->addWidget(m_name);
    layout->addSpacing(8);
    layout->addWidget(sectionLabel(tr("Folder Color")));
    layout->addLayout(colors);
    layout->addStretch();
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(done, &QPushButton::clicked, this, [this] {
        m_folder.name = m_name->text().trimmed();
        QDialog::accept();
    });
    connect(m_name, &QLineEdit::returnPressed, done, &QPushButton::click);
    selectColor(folder.color);
}

void FolderSettingsDialog::selectColor(int color)
{
    // The default color is stored as "no color".
    m_folder.color = color == FolderColors[0] ? -1 : color;
    const int shown = m_folder.color < 0 ? FolderColors[0] : m_folder.color;
    bool preset = false;
    for (ColorSwatch* swatch : std::as_const(m_swatches)) {
        const bool selected = int(swatch->swatchColor().rgb() & 0xffffff) == shown;
        swatch->setSelectedSwatch(selected);
        preset = preset || selected;
    }
    m_custom->setSwatchColor(preset ? Theme::instance().palette().surface : QColor::fromRgb(QRgb(shown)));
    m_custom->setSelectedSwatch(!preset);
}

// --- CreateChannelDialog ---------------------------------------------------------------------------

CreateChannelDialog::CreateChannelDialog(Session* session, const QString& guildId, const QString& categoryId, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_guildId(guildId)
    , m_categoryId(categoryId)
    , m_text(new QRadioButton(tr("Text")))
    , m_voice(new QRadioButton(tr("Voice")))
    , m_name(new QLineEdit)
    , m_error(errorLabel())
    , m_create(button(tr("Create Channel"), "brandButton"))
{
    setupDialog(this, tr("Create Channel"));
    m_text->setChecked(true);
    m_name->setPlaceholderText(QStringLiteral("new-channel"));
    m_name->setMaxLength(100);
    m_create->setEnabled(false);
    keepTextChannelName(m_name, [this] { return m_text->isChecked(); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(6);
    layout->addWidget(titleLabel(tr("Create Channel")));
    if (const Channel* category = session->channel(guildId, categoryId))
        layout->addWidget(hintLabel(tr("in %1").arg(category->name)));
    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Channel Type")));
    layout->addWidget(m_text);
    layout->addWidget(hintLabel(tr("Send messages, images, GIFs, emoji, opinions, and puns")));
    layout->addSpacing(4);
    layout->addWidget(m_voice);
    layout->addWidget(hintLabel(tr("Hang out together with voice")));
    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Channel Name")));
    layout->addWidget(m_name);
    layout->addWidget(m_error);
    layout->addStretch();
    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);
    layout->addLayout(buttons);
    resize(440, 420);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &CreateChannelDialog::create);
    connect(m_name, &QLineEdit::returnPressed, this, &CreateChannelDialog::create);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_create->setEnabled(!text.trimmed().isEmpty());
    });
    connect(m_text, &QRadioButton::toggled, this, [this](bool text) {
        m_name->setPlaceholderText(text ? QStringLiteral("new-channel") : tr("New Voice Channel"));
    });
}

void CreateChannelDialog::create()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty() || !m_create->isEnabled())
        return;
    m_error->hide();
    m_create->setEnabled(false);
    QPointer<CreateChannelDialog> guard(this);
    m_session->createChannel(m_guildId, m_text->isChecked() ? ChannelType::GuildText : ChannelType::GuildVoice, name,
                             m_categoryId, [guard](const QString& channelId, const QString& error) {
                                 if (!guard)
                                     return;
                                 if (channelId.isEmpty()) {
                                     guard->m_create->setEnabled(true);
                                     guard->m_error->setText(error);
                                     guard->m_error->show();
                                     return;
                                 }
                                 emit guard->created(channelId);
                                 guard->accept();
                             });
}

// --- ChannelSettingsDialog -------------------------------------------------------------------------

namespace {

// Slowmode choices of the official client, in seconds.
constexpr int SlowmodeSteps[] = {0, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 21600};

QString slowmodeText(int seconds)
{
    if (seconds == 0)
        return QCoreApplication::translate("ChannelSettingsDialog", "Off");
    if (seconds < 60)
        return QCoreApplication::translate("ChannelSettingsDialog", "%1s").arg(seconds);
    if (seconds < 3600)
        return QCoreApplication::translate("ChannelSettingsDialog", "%1m").arg(seconds / 60);
    return QCoreApplication::translate("ChannelSettingsDialog", "%1h").arg(seconds / 3600);
}

// Highest voice bitrate allowed by the server's boost level, in kbps.
int maxBitrate(int premiumTier)
{
    switch (premiumTier) {
    case 1:
        return 128;
    case 2:
        return 256;
    case 3:
        return 384;
    default:
        return 96;
    }
}

} // namespace

ChannelSettingsDialog::ChannelSettingsDialog(Session* session, const QString& guildId, const QString& channelId, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_guildId(guildId)
    , m_channelId(channelId)
    , m_name(new QLineEdit)
    , m_error(errorLabel())
    , m_save(button(tr("Save Changes"), "brandButton"))
{
    const Channel* found = session->channel(guildId, channelId);
    const Channel channel = found ? *found : Channel();
    const Guild* guild = session->guild(guildId);
    const bool category = channel.type == ChannelType::GuildCategory;
    const bool voice = channel.isVoice();
    setupDialog(this, category ? tr("Edit Category") : tr("Edit Channel"));

    m_name->setText(channel.name);
    m_name->setMaxLength(100);
    keepTextChannelName(m_name, [category, voice] { return !category && !voice; });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(6);
    layout->addWidget(titleLabel(category ? tr("Edit Category") : tr("Edit Channel")));
    layout->addWidget(hintLabel(channelLabel(channel)));
    layout->addSpacing(12);
    layout->addWidget(sectionLabel(category ? tr("Category Name") : tr("Channel Name")));
    layout->addWidget(m_name);

    if (!category && !voice) {
        m_topic = new QPlainTextEdit(channel.topic);
        m_topic->setPlaceholderText(tr("Let everyone know how to use this channel!"));
        m_topic->setFixedHeight(90);
        connect(m_topic, &QPlainTextEdit::textChanged, this, [this] {
            // QPlainTextEdit has no length limit of its own.
            constexpr int MaxTopic = 1024;
            if (m_topic->toPlainText().size() > MaxTopic) {
                QTextCursor cursor = m_topic->textCursor();
                m_topic->setPlainText(m_topic->toPlainText().left(MaxTopic));
                cursor.setPosition(MaxTopic);
                m_topic->setTextCursor(cursor);
            }
        });
        m_slowmode = new QComboBox;
        for (const int seconds : SlowmodeSteps)
            m_slowmode->addItem(slowmodeText(seconds), seconds);
        if (m_slowmode->findData(channel.rateLimitPerUser) < 0)
            m_slowmode->addItem(slowmodeText(channel.rateLimitPerUser), channel.rateLimitPerUser);
        m_slowmode->setCurrentIndex(m_slowmode->findData(channel.rateLimitPerUser));
        m_nsfw = new QCheckBox(tr("Age-Restricted Channel"));
        m_nsfw->setChecked(channel.nsfw);

        layout->addSpacing(8);
        layout->addWidget(sectionLabel(tr("Channel Topic")));
        layout->addWidget(m_topic);
        layout->addSpacing(8);
        layout->addWidget(sectionLabel(tr("Slowmode")));
        layout->addWidget(m_slowmode);
        layout->addWidget(hintLabel(tr("Members will be restricted to sending one message per this interval, unless they have Manage Channel or Manage Messages permissions.")));
        layout->addSpacing(8);
        layout->addWidget(m_nsfw);
        layout->addWidget(hintLabel(tr("Users will need to confirm they are over the legal age to view the content in this channel.")));
    } else if (voice) {
        const int highest = maxBitrate(guild ? guild->premiumTier : 0);
        m_bitrate = new QSlider(Qt::Horizontal);
        m_bitrate->setRange(8, std::max(highest, channel.bitrate / 1000));
        m_bitrate->setValue(channel.bitrate > 0 ? channel.bitrate / 1000 : 64);
        m_userLimit = new QSlider(Qt::Horizontal);
        m_userLimit->setRange(0, 99);
        m_userLimit->setValue(channel.userLimit);
        auto* bitrateValue = hintLabel({});
        auto* limitValue = hintLabel({});
        auto showBitrate = [bitrateValue](int kbps) { bitrateValue->setText(tr("%1 kbps").arg(kbps)); };
        auto showLimit = [limitValue](int users) {
            limitValue->setText(users == 0 ? tr("No limit") : tr("%n user(s)", nullptr, users));
        };
        showBitrate(m_bitrate->value());
        showLimit(m_userLimit->value());
        connect(m_bitrate, &QSlider::valueChanged, this, showBitrate);
        connect(m_userLimit, &QSlider::valueChanged, this, showLimit);

        layout->addSpacing(8);
        layout->addWidget(sectionLabel(tr("Bitrate")));
        auto* bitrateRow = new QHBoxLayout;
        bitrateRow->addWidget(m_bitrate, 1);
        bitrateRow->addWidget(bitrateValue);
        layout->addLayout(bitrateRow);
        layout->addWidget(hintLabel(tr("Going above 64 kbps may adversely affect people on low bandwidth connections.")));
        layout->addSpacing(8);
        layout->addWidget(sectionLabel(tr("User Limit")));
        auto* limitRow = new QHBoxLayout;
        limitRow->addWidget(m_userLimit, 1);
        limitRow->addWidget(limitValue);
        layout->addLayout(limitRow);
        layout->addWidget(hintLabel(tr("Limits the number of users that can connect to this voice channel.")));
    }

    layout->addWidget(m_error);
    layout->addStretch();
    auto* remove = button(category ? tr("Delete Category") : tr("Delete Channel"), "dangerButton");
    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_save);
    layout->addSpacing(12);
    layout->addLayout(buttons);
    resize(480, category ? 280 : voice ? 440 : 560);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &ChannelSettingsDialog::save);
    connect(m_name, &QLineEdit::returnPressed, this, &ChannelSettingsDialog::save);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& text) { m_save->setEnabled(!text.trimmed().isEmpty()); });
    connect(remove, &QPushButton::clicked, this, [this] {
        QPointer<ChannelSettingsDialog> guard(this);
        const bool deleting = confirmDeleteChannel(m_session, m_guildId, m_channelId, this, [guard](const QString& error) {
            if (!guard)
                return;
            if (error.isEmpty()) {
                guard->accept();
                return;
            }
            guard->setEnabled(true);
            guard->m_error->setText(error);
            guard->m_error->show();
        });
        if (deleting)
            setEnabled(false); // until Discord answers
    });
}

void ChannelSettingsDialog::save()
{
    const Channel* channel = m_session->channel(m_guildId, m_channelId);
    if (!channel) {
        reject(); // deleted meanwhile
        return;
    }
    const QString name = m_name->text().trimmed();
    if (name.isEmpty() || !m_save->isEnabled())
        return;

    // Like the official settings page, only what changed is sent.
    QJsonObject changes;
    if (name != channel->name)
        changes.insert(QStringLiteral("name"), name);
    if (m_topic && m_topic->toPlainText() != channel->topic)
        changes.insert(QStringLiteral("topic"), m_topic->toPlainText());
    if (m_slowmode && m_slowmode->currentData().toInt() != channel->rateLimitPerUser)
        changes.insert(QStringLiteral("rate_limit_per_user"), m_slowmode->currentData().toInt());
    if (m_nsfw && m_nsfw->isChecked() != channel->nsfw)
        changes.insert(QStringLiteral("nsfw"), m_nsfw->isChecked());
    if (m_bitrate && m_bitrate->value() * 1000 != channel->bitrate)
        changes.insert(QStringLiteral("bitrate"), m_bitrate->value() * 1000);
    if (m_userLimit && m_userLimit->value() != channel->userLimit)
        changes.insert(QStringLiteral("user_limit"), m_userLimit->value());
    if (changes.isEmpty()) {
        accept();
        return;
    }

    m_error->hide();
    m_save->setEnabled(false);
    QPointer<ChannelSettingsDialog> guard(this);
    m_session->editChannel(m_channelId, changes, [guard](const QString& error) {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            guard->m_save->setEnabled(true);
            guard->m_error->setText(error);
            guard->m_error->show();
            return;
        }
        guard->accept();
    });
}

bool confirmDeleteChannel(Session* session, const QString& guildId, const QString& channelId, QWidget* parent,
                          Session::ResultCallback done)
{
    const Channel* channel = session->channel(guildId, channelId);
    if (!channel)
        return false;
    const bool category = channel->type == ChannelType::GuildCategory;
    const QString name = channelLabel(*channel);
    const QString action = category ? QCoreApplication::translate("ChannelSettingsDialog", "Delete Category")
                                    : QCoreApplication::translate("ChannelSettingsDialog", "Delete Channel");
    const QString question =
        category ? QCoreApplication::translate("ChannelSettingsDialog",
                                               "Are you sure you want to delete %1? The channels inside it will not be deleted.")
                 : QCoreApplication::translate("ChannelSettingsDialog", "Are you sure you want to delete %1? This cannot be undone.");
    QMessageBox box(QMessageBox::Warning, action, question.arg(name), QMessageBox::Cancel, parent);
    QPushButton* confirm = box.addButton(action, QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() != confirm)
        return false;
    if (!done) {
        QPointer<QWidget> window = parent ? parent->window() : nullptr;
        done = [window, action](const QString& error) {
            if (error.isEmpty() || !window)
                return;
            auto* failure = new QMessageBox(QMessageBox::Warning, action, error, QMessageBox::Ok, window);
            failure->setAttribute(Qt::WA_DeleteOnClose);
            failure->show();
        };
    }
    session->deleteChannel(guildId, channelId, done);
    return true;
}
