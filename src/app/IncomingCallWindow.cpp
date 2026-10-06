#include "IncomingCallWindow.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

IncomingCallWindow::IncomingCallWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_avatar(new QLabel)
    , m_name(new QLabel)
{
    setObjectName(QStringLiteral("incomingCall"));
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(260, 290);

    m_avatar->setFixedSize(80, 80);
    m_name->setObjectName(QStringLiteral("incomingCallName"));
    m_name->setAlignment(Qt::AlignCenter);
    auto* status = new QLabel(tr("Incoming call…"));
    status->setObjectName(QStringLiteral("incomingCallStatus"));
    status->setAlignment(Qt::AlignCenter);

    auto* accept = new QPushButton;
    accept->setObjectName(QStringLiteral("acceptCallButton"));
    accept->setIcon(QIcon(QStringLiteral(":/icons/call.svg")));
    accept->setIconSize({24, 24});
    accept->setFixedSize(56, 56);
    accept->setCursor(Qt::PointingHandCursor);
    accept->setToolTip(tr("Accept"));
    auto* decline = new QPushButton;
    decline->setObjectName(QStringLiteral("declineCallButton"));
    decline->setIcon(QIcon(QStringLiteral(":/icons/disconnect.svg")));
    decline->setIconSize({24, 24});
    decline->setFixedSize(56, 56);
    decline->setCursor(Qt::PointingHandCursor);
    decline->setToolTip(tr("Decline"));
    connect(accept, &QPushButton::clicked, this, [this] { emit accepted(m_channelId); });
    connect(decline, &QPushButton::clicked, this, [this] { emit declined(m_channelId); });

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(decline);
    buttons->addSpacing(32);
    buttons->addWidget(accept);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 24, 16, 24);
    layout->addWidget(m_avatar, 0, Qt::AlignHCenter);
    layout->addSpacing(12);
    layout->addWidget(m_name);
    layout->addWidget(status);
    layout->addStretch();
    layout->addLayout(buttons);
}

void IncomingCallWindow::setCaller(const QString& channelId, const QString& name, const QPixmap& avatar)
{
    m_channelId = channelId;
    m_name->setText(m_name->fontMetrics().elidedText(name, Qt::ElideRight, width() - 32));
    m_avatar->setPixmap(avatar);
}

void IncomingCallWindow::showAtCorner()
{
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.right() - width() - 24, area.bottom() - height() - 24);
    }
    show();
    raise();
}
