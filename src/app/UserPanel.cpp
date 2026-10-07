#include "UserPanel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

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
    , m_profileArea(new QWidget)
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

    m_avatar->setFixedSize(32, 32);
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

    // Avatar and names form one clickable area, like Discord's.
    m_profileArea->setObjectName(QStringLiteral("userPanelProfile"));
    m_profileArea->setAttribute(Qt::WA_StyledBackground);
    m_profileArea->setAttribute(Qt::WA_Hover);
    m_profileArea->setCursor(Qt::PointingHandCursor);
    m_profileArea->setToolTip(tr("Profile and status"));
    m_profileArea->installEventFilter(this);
    auto* profile = new QHBoxLayout(m_profileArea);
    profile->setContentsMargins(2, 2, 6, 2);
    profile->setSpacing(8);
    profile->addWidget(m_avatar);
    profile->addLayout(names, 1);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 8, 0);
    layout->setSpacing(4);
    layout->addWidget(m_profileArea, 1);
    layout->addLayout(buttons);

    connect(m_mute, &QToolButton::clicked, this, &UserPanel::muteClicked);
    connect(m_deafen, &QToolButton::clicked, this, &UserPanel::deafenClicked);
    connect(m_settings, &QToolButton::clicked, this, &UserPanel::settingsRequested);

    setVoiceState(false, false);
}

void UserPanel::setUser(const QString& displayName, const QString& status, const QPixmap& avatar)
{
    m_name->setText(m_name->fontMetrics().elidedText(displayName, Qt::ElideRight, 90));
    m_status->setText(m_status->fontMetrics().elidedText(status, Qt::ElideRight, 90));
    m_status->setToolTip(status);
    m_avatar->setPixmap(avatar);
}

bool UserPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_profileArea && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        emit profileRequested();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void UserPanel::setVoiceState(bool muted, bool deafened)
{
    m_mute->setIcon(QIcon(muted ? QStringLiteral(":/icons/mic-off.svg") : QStringLiteral(":/icons/mic.svg")));
    m_mute->setToolTip(muted ? tr("Unmute") : tr("Mute"));
    m_deafen->setIcon(QIcon(deafened ? QStringLiteral(":/icons/headphones-off.svg")
                                     : QStringLiteral(":/icons/headphones.svg")));
    m_deafen->setToolTip(deafened ? tr("Undeafen") : tr("Deafen"));
}
