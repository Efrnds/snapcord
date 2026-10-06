#include "VoicePanel.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

VoicePanel::VoicePanel(QWidget* parent)
    : QWidget(parent)
    , m_status(new QPushButton)
    , m_location(new QLabel)
    , m_disconnect(new QToolButton)
{
    setObjectName(QStringLiteral("voicePanel"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(56);
    hide();

    m_status->setObjectName(QStringLiteral("voiceStatus"));
    m_status->setCursor(Qt::PointingHandCursor);
    m_status->setFlat(true);
    connect(m_status, &QPushButton::clicked, this, &VoicePanel::detailsRequested);
    m_location->setObjectName(QStringLiteral("voiceLocation"));

    m_disconnect->setIcon(QIcon(QStringLiteral(":/icons/disconnect.svg")));
    m_disconnect->setIconSize({20, 20});
    m_disconnect->setFixedSize(32, 32);
    m_disconnect->setCursor(Qt::PointingHandCursor);
    m_disconnect->setToolTip(tr("Disconnect"));
    connect(m_disconnect, &QToolButton::clicked, this, &VoicePanel::disconnectRequested);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    texts->addStretch();
    texts->addWidget(m_status, 0, Qt::AlignLeft);
    texts->addWidget(m_location);
    texts->addStretch();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->addLayout(texts, 1);
    layout->addWidget(m_disconnect);
}

void VoicePanel::setStatus(Status status)
{
    m_status->setText(status == Status::Connected ? tr("Voice Connected") : tr("Connecting…"));
    m_status->setProperty("connected", status == Status::Connected);
    // Re-polish so the QSS rule depending on the "connected" property is applied.
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void VoicePanel::setLocation(const QString& channelName, const QString& placeName)
{
    const QString text = channelName + QStringLiteral(" / ") + placeName;
    m_location->setText(m_location->fontMetrics().elidedText(text, Qt::ElideRight, 170));
    m_location->setToolTip(text);
}

void VoicePanel::setPing(int milliseconds)
{
    m_status->setToolTip(tr("Ping: %1 ms. Click for connection details.").arg(milliseconds));
}
