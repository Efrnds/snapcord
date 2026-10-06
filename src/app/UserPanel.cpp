#include "UserPanel.h"

#include "Avatar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int AvatarSize = 32;

QToolButton* makePanelButton()
{
    auto* button = new QToolButton;
    button->setFixedSize(32, 32);
    button->setIconSize({20, 20});
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    return button;
}

} // namespace

UserPanel::UserPanel(QWidget* parent)
    : QWidget(parent)
    , m_avatar(new QLabel)
    , m_name(new QLabel)
    , m_status(new QLabel)
    , m_mute(makePanelButton())
    , m_deafen(makePanelButton())
    , m_settings(makePanelButton())
{
    setObjectName(QStringLiteral("userPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(52);

    m_avatar->setFixedSize(AvatarSize, AvatarSize);
    m_name->setObjectName(QStringLiteral("userName"));
    m_status->setObjectName(QStringLiteral("userStatus"));

    auto* names = new QVBoxLayout;
    names->setContentsMargins(0, 0, 0, 0);
    names->setSpacing(0);
    names->addStretch();
    names->addWidget(m_name);
    names->addWidget(m_status);
    names->addStretch();

    m_settings->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    m_settings->setToolTip(tr("User Settings"));

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(0);
    buttons->addWidget(m_mute);
    buttons->addWidget(m_deafen);
    buttons->addWidget(m_settings);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);
    layout->addWidget(m_avatar);
    layout->addLayout(names, 1);
    layout->addLayout(buttons);

    connect(m_mute, &QToolButton::clicked, this, &UserPanel::toggleMute);
    connect(m_deafen, &QToolButton::clicked, this, &UserPanel::toggleDeafen);
    connect(m_settings, &QToolButton::clicked, this, &UserPanel::settingsRequested);

    updateButtons();
}

void UserPanel::setUser(const QString& displayName, const QString& status)
{
    m_name->setText(displayName);
    m_status->setText(status);
    m_avatar->setPixmap(makeAvatar(displayName, AvatarSize, devicePixelRatioF(), QColor(0x23, 0x24, 0x28)));
}

// Same behavior as Discord: unmuting while deafened also undeafens.
void UserPanel::toggleMute()
{
    if (m_deafened)
        setState(false, false);
    else
        setState(!m_muted, false);
}

// Deafening also mutes; undeafening restores the previous mute state.
void UserPanel::toggleDeafen()
{
    if (m_deafened) {
        setState(m_mutedBeforeDeafen, false);
    } else {
        m_mutedBeforeDeafen = m_muted;
        setState(true, true);
    }
}

void UserPanel::setState(bool muted, bool deafened)
{
    const bool muteChangedNow = muted != m_muted;
    const bool deafenChangedNow = deafened != m_deafened;
    m_muted = muted;
    m_deafened = deafened;
    updateButtons();
    if (muteChangedNow)
        emit muteChanged(m_muted);
    if (deafenChangedNow)
        emit deafenChanged(m_deafened);
}

void UserPanel::updateButtons()
{
    m_mute->setIcon(QIcon(m_muted ? QStringLiteral(":/icons/mic-off.svg") : QStringLiteral(":/icons/mic.svg")));
    m_mute->setToolTip(m_muted ? tr("Unmute") : tr("Mute"));
    m_deafen->setIcon(QIcon(m_deafened ? QStringLiteral(":/icons/headphones-off.svg")
                                       : QStringLiteral(":/icons/headphones.svg")));
    m_deafen->setToolTip(m_deafened ? tr("Undeafen") : tr("Deafen"));
}
