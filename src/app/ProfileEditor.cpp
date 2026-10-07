#include "ProfileEditor.h"

#include "EmojiPicker.h"
#include "ProfileCard.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "core/Session.h"

#include <QBuffer>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

constexpr int BioLimit = 190;
constexpr int AvatarMaximumSize = 512;

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("settingsSection"));
    return label;
}

QPushButton* button(const QString& text, const char* objectName)
{
    auto* result = new QPushButton(text);
    result->setObjectName(QLatin1String(objectName));
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

// Discord takes pictures as data URIs. Crop to a centered square and keep it at a sensible size.
QString avatarDataUri(const QImage& source)
{
    const int side = std::min(source.width(), source.height());
    QImage square = source.copy((source.width() - side) / 2, (source.height() - side) / 2, side, side);
    if (side > AvatarMaximumSize)
        square = square.scaled(AvatarMaximumSize, AvatarMaximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    square.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

} // namespace

// --- ProfileEditDialog ------------------------------------------------------------------------------

ProfileEditDialog::ProfileEditDialog(Session* session, ImageCache* images, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_preview(new ProfileCard(images))
    , m_displayName(new QLineEdit)
    , m_pronouns(new QLineEdit)
    , m_bio(new QPlainTextEdit)
    , m_bioCounter(new QLabel)
    , m_bannerColor(new ColorSwatch)
    , m_removeAvatar(button(tr("Remove Avatar"), "secondaryButton"))
    , m_error(new QLabel)
    , m_saveButton(button(tr("Save Changes"), "brandButton"))
{
    setObjectName(QStringLiteral("profileEditor"));
    setWindowTitle(tr("Edit Profile"));
    setAttribute(Qt::WA_StyledBackground);

    m_displayName->setMaxLength(32);
    m_displayName->setPlaceholderText(session->self().username);
    m_pronouns->setMaxLength(40);
    m_pronouns->setPlaceholderText(tr("Add your pronouns"));
    m_bio->setPlaceholderText(tr("Tell people a little about yourself"));
    m_bio->setFixedHeight(110);
    m_bioCounter->setObjectName(QStringLiteral("settingsHint"));
    m_bioCounter->setAlignment(Qt::AlignRight);
    m_error->setObjectName(QStringLiteral("profileError"));
    m_error->setWordWrap(true);
    m_error->hide();

    auto* changeAvatar = button(tr("Change Avatar"), "brandButton");
    auto* avatarRow = new QHBoxLayout;
    avatarRow->addWidget(changeAvatar);
    avatarRow->addWidget(m_removeAvatar);
    avatarRow->addStretch();

    auto* resetColor = button(tr("Default"), "secondaryButton");
    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(m_bannerColor);
    colorRow->addWidget(resetColor);
    colorRow->addStretch();
    auto* colorHint = new QLabel(tr("Banner pictures and profile themes need Discord Nitro and can be set in the official app."));
    colorHint->setObjectName(QStringLiteral("settingsHint"));
    colorHint->setWordWrap(true);

    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_saveButton);

    auto* form = new QVBoxLayout;
    form->setSpacing(6);
    form->addWidget(sectionLabel(tr("Display Name")));
    form->addWidget(m_displayName);
    form->addSpacing(10);
    form->addWidget(sectionLabel(tr("Pronouns")));
    form->addWidget(m_pronouns);
    form->addSpacing(10);
    form->addWidget(sectionLabel(tr("Avatar")));
    form->addLayout(avatarRow);
    form->addSpacing(10);
    form->addWidget(sectionLabel(tr("Banner Color")));
    form->addLayout(colorRow);
    form->addWidget(colorHint);
    form->addSpacing(10);
    form->addWidget(sectionLabel(tr("About Me")));
    form->addWidget(m_bio);
    form->addWidget(m_bioCounter);
    form->addStretch();
    form->addWidget(m_error);
    form->addLayout(buttons);

    m_preview->setActionsVisible(false);
    m_preview->setFixedWidth(340);
    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(sectionLabel(tr("Preview")));
    previewColumn->addWidget(m_preview);
    previewColumn->addStretch();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(32);
    layout->addLayout(form, 1);
    layout->addLayout(previewColumn);
    resize(820, 600);

    connect(m_displayName, &QLineEdit::textChanged, this, &ProfileEditDialog::updatePreview);
    connect(m_pronouns, &QLineEdit::textChanged, this, &ProfileEditDialog::updatePreview);
    connect(m_bio, &QPlainTextEdit::textChanged, this, [this] {
        // QPlainTextEdit has no length limit of its own.
        const QString text = m_bio->toPlainText();
        if (text.size() > BioLimit) {
            QSignalBlocker blocker(m_bio);
            m_bio->setPlainText(text.left(BioLimit));
            m_bio->moveCursor(QTextCursor::End);
        }
        updatePreview();
    });
    connect(changeAvatar, &QPushButton::clicked, this, &ProfileEditDialog::chooseAvatar);
    connect(m_removeAvatar, &QPushButton::clicked, this, [this] {
        m_newAvatar = QImage();
        m_avatarRemoved = true;
        updatePreview();
    });
    connect(m_bannerColor, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(m_bannerColor->swatchColor(), this, tr("Banner Color"));
        if (!color.isValid())
            return;
        m_accentColor = int(color.rgb() & 0xffffff);
        updatePreview();
    });
    connect(resetColor, &QPushButton::clicked, this, [this] {
        m_accentColor = -1;
        updatePreview();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_saveButton, &QPushButton::clicked, this, &ProfileEditDialog::save);

    // Fill in the current values; until they arrive, nothing can be saved.
    m_original.user = session->self();
    m_displayName->setText(session->self().globalName);
    m_saveButton->setEnabled(false);
    updatePreview();
    QPointer<ProfileEditDialog> guard(this);
    session->fetchProfile(session->self().id, QString(), [guard](const UserProfile* profile, const QString& error) {
        if (!guard)
            return;
        if (profile) {
            guard->loadProfile(*profile);
        } else {
            guard->showError(tr("Could not load your profile: %1").arg(error));
            guard->m_saveButton->setEnabled(true);
        }
    });
}

void ProfileEditDialog::loadProfile(const UserProfile& profile)
{
    m_original = profile;
    m_original.user = m_session->self();
    m_accentColor = profile.accentColor;
    m_displayName->setText(m_original.user.globalName);
    m_pronouns->setText(profile.pronouns);
    m_bio->setPlainText(profile.bio);
    m_saveButton->setEnabled(true);
    updatePreview();
}

void ProfileEditDialog::updatePreview()
{
    m_bioCounter->setText(QStringLiteral("%1/%2").arg(m_bio->toPlainText().size()).arg(BioLimit));
    m_bannerColor->setSwatchColor(m_accentColor >= 0 ? QColor::fromRgb(QRgb(m_accentColor))
                                                     : Theme::instance().palette().button);
    m_removeAvatar->setEnabled(!m_newAvatar.isNull() || (!m_original.user.avatar.isEmpty() && !m_avatarRemoved));

    ProfileCardData card;
    card.user = m_original.user;
    card.displayName = m_displayName->text().trimmed().isEmpty() ? card.user.username : m_displayName->text().trimmed();
    card.pronouns = m_pronouns->text().trimmed();
    card.bio = m_bio->toPlainText();
    card.banner = m_original.banner;
    if (m_accentColor >= 0)
        card.bannerColor = QColor::fromRgb(QRgb(m_accentColor));
    card.avatarOverride = m_newAvatar;
    card.avatarRemoved = m_avatarRemoved;
    card.presence = m_session->presence(card.user.id);
    card.badges = m_original.badges;
    card.isSelf = true;
    card.profileLoaded = true;
    m_preview->setData(card);
}

void ProfileEditDialog::chooseAvatar()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose Avatar"),
                                                      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                      tr("Images (%1)").arg(patterns.join(u' ')));
    if (file.isEmpty())
        return;
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QImage picture = reader.read();
    if (picture.isNull()) {
        showError(tr("This file is not a picture Snapcord can open."));
        return;
    }
    const int side = std::min(picture.width(), picture.height());
    m_newAvatar = picture.copy((picture.width() - side) / 2, (picture.height() - side) / 2, side, side)
                      .scaled(AvatarMaximumSize, AvatarMaximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_avatarRemoved = false;
    m_error->hide();
    updatePreview();
}

void ProfileEditDialog::save()
{
    Session::ProfileChanges changes;
    const QString name = m_displayName->text().trimmed();
    if (name != m_original.user.globalName)
        changes.globalName = name;
    if (m_pronouns->text().trimmed() != m_original.pronouns)
        changes.pronouns = m_pronouns->text().trimmed();
    if (m_bio->toPlainText() != m_original.bio)
        changes.bio = m_bio->toPlainText();
    if (m_accentColor != m_original.accentColor)
        changes.accentColor = m_accentColor;
    if (!m_newAvatar.isNull())
        changes.avatar = avatarDataUri(m_newAvatar);
    else if (m_avatarRemoved && !m_original.user.avatar.isEmpty())
        changes.avatar = QString();

    m_saveButton->setEnabled(false);
    m_saveButton->setText(tr("Saving…"));
    m_error->hide();
    QPointer<ProfileEditDialog> guard(this);
    m_session->updateProfile(changes, [guard](const QString& error) {
        if (!guard)
            return;
        if (error.isEmpty()) {
            guard->accept();
            return;
        }
        guard->m_saveButton->setEnabled(true);
        guard->m_saveButton->setText(tr("Save Changes"));
        guard->showError(error);
    });
}

void ProfileEditDialog::showError(const QString& text)
{
    m_error->setText(text);
    m_error->show();
}

// --- CustomStatusDialog -----------------------------------------------------------------------------

CustomStatusDialog::CustomStatusDialog(Session* session, ImageCache* images, QWidget* parent)
    : QDialog(parent)
    , m_session(session)
    , m_images(images)
    , m_emoji(new QPushButton)
    , m_text(new QLineEdit)
    , m_clearAfter(new QComboBox)
{
    setObjectName(QStringLiteral("profileEditor"));
    setWindowTitle(tr("Set a custom status"));
    setAttribute(Qt::WA_StyledBackground);

    const CustomStatus current = session->selfCustomStatus();
    if (current.isActive()) {
        m_text->setText(current.text);
        m_emojiName = current.emojiName;
    }

    m_emoji->setObjectName(QStringLiteral("secondaryButton"));
    m_emoji->setCursor(Qt::PointingHandCursor);
    m_emoji->setFixedWidth(48);
    m_emoji->setToolTip(tr("Choose an emoji"));
    m_text->setMaxLength(128);
    m_text->setPlaceholderText(tr("What's on your mind?"));

    m_clearAfter->addItem(tr("Today"), -1);
    m_clearAfter->addItem(tr("4 hours"), 4 * 60);
    m_clearAfter->addItem(tr("1 hour"), 60);
    m_clearAfter->addItem(tr("30 minutes"), 30);
    m_clearAfter->addItem(tr("Don't clear"), 0);
    m_clearAfter->setCurrentIndex(current.isActive() && !current.expiresAt.isValid() ? 4 : 0);

    auto* row = new QHBoxLayout;
    row->addWidget(m_emoji);
    row->addWidget(m_text, 1);

    auto* clear = button(tr("Clear Status"), "secondaryButton");
    clear->setEnabled(current.isActive());
    auto* cancel = button(tr("Cancel"), "secondaryButton");
    auto* save = button(tr("Save"), "brandButton");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(clear);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addWidget(sectionLabel(tr("What's cookin'?")));
    layout->addLayout(row);
    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Clear after")));
    layout->addWidget(m_clearAfter);
    layout->addSpacing(16);
    layout->addLayout(buttons);
    setMinimumWidth(420);

    connect(m_emoji, &QPushButton::clicked, this, [this] {
        // Unicode emojis only: custom emojis in a status need Nitro.
        auto* picker = new EmojiPicker(m_session, m_images, QString(), this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        connect(picker, &EmojiPicker::picked, this, [this, picker](const QString&, const Emoji& emoji) {
            if (!emoji.isCustom()) {
                m_emojiName = emoji.name;
                updateEmojiButton();
            }
            picker->close();
        });
        picker->popupAt(m_emoji->mapToGlobal(QPoint(m_emoji->width(), 0)));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_session->setCustomStatus({});
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &CustomStatusDialog::save);
    connect(m_text, &QLineEdit::returnPressed, this, &CustomStatusDialog::save);
    updateEmojiButton();
}

void CustomStatusDialog::updateEmojiButton()
{
    m_emoji->setText(m_emojiName.isEmpty() ? QStringLiteral("☺") : m_emojiName);
}

void CustomStatusDialog::save()
{
    CustomStatus status;
    status.text = m_text->text().trimmed();
    status.emojiName = m_emojiName;
    const int minutes = m_clearAfter->currentData().toInt();
    if (minutes < 0) // "Today": until local midnight
        status.expiresAt = QDateTime(QDate::currentDate().addDays(1), QTime(0, 0)).toUTC();
    else if (minutes > 0)
        status.expiresAt = QDateTime::currentDateTimeUtc().addSecs(minutes * 60);
    m_session->setCustomStatus(status.isEmpty() ? CustomStatus() : status);
    accept();
}
