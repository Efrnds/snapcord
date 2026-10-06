#include "VoiceChannelView.h"

#include "Avatar.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int TileSpacing = 8;
constexpr int MinimumTileWidth = 260;
constexpr int GridMargin = 16;

} // namespace

ParticipantTile::ParticipantTile(QWidget* parent)
    : QWidget(parent)
{
}

void ParticipantTile::setParticipant(const Participant& participant)
{
    m_participant = participant;
    update();
}

void ParticipantTile::setSpeaking(bool speaking)
{
    if (m_participant.speaking == speaking)
        return;
    m_participant.speaking = speaking;
    update();
}

void ParticipantTile::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(m_participant.userId, event->globalPos());
}

void ParticipantTile::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    const QRectF tile = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x23, 0x24, 0x28));
    painter.drawRoundedRect(tile, 8, 8);

    const int avatarSize = qBound(48, height() / 3, 96);
    const QPixmap avatar = makeAvatar(m_participant.name, m_participant.picture, avatarSize, devicePixelRatioF());
    painter.drawPixmap(QPointF((width() - avatarSize) / 2.0, (height() - avatarSize) / 2.0), avatar);

    // Name tag in the bottom-left corner.
    QFont font = painter.font();
    font.setPixelSize(14);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const QString name = painter.fontMetrics().elidedText(m_participant.name, Qt::ElideRight, width() - 80);
    const QRectF tag(tile.left() + 8, tile.bottom() - 32, painter.fontMetrics().horizontalAdvance(name) + 16, 24);
    painter.setBrush(QColor(0, 0, 0, 120));
    painter.drawRoundedRect(tag, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(tag, Qt::AlignCenter, name);

    // Mute and deafen indicators in the bottom-right corner.
    qreal x = tile.right() - 32;
    auto drawIndicator = [&](const QString& path) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 120));
        painter.drawEllipse(QRectF(x, tile.bottom() - 32, 24, 24));
        QIcon(path).paint(&painter, QRect(int(x) + 4, int(tile.bottom()) - 28, 16, 16));
        x -= 28;
    };
    if (m_participant.deafened)
        drawIndicator(QStringLiteral(":/icons/headphones-off.svg"));
    if (m_participant.muted)
        drawIndicator(QStringLiteral(":/icons/mic-off.svg"));

    if (m_participant.speaking) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0x23, 0xa5, 0x59), 3));
        painter.drawRoundedRect(tile.adjusted(1.5, 1.5, -1.5, -1.5), 8, 8);
    }
}

VoiceChannelView::VoiceChannelView(QWidget* parent)
    : QWidget(parent)
    , m_title(new QLabel)
    , m_grid(new QWidget)
    , m_emptyLabel(new QLabel(tr("No one is here yet.")))
    , m_joinButton(new QPushButton(tr("Join Voice")))
{
    setObjectName(QStringLiteral("voiceChannelView"));
    setAttribute(Qt::WA_StyledBackground);

    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(48);
    auto* icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/icons/speaker.svg")).pixmap(QSize(24, 24), devicePixelRatioF()));
    m_title->setObjectName(QStringLiteral("chatTitle"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(icon);
    headerLayout->addWidget(m_title);
    headerLayout->addStretch();

    m_emptyLabel->setObjectName(QStringLiteral("welcomeSubtitle"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_joinButton->setObjectName(QStringLiteral("joinVoiceButton"));
    m_joinButton->setCursor(Qt::PointingHandCursor);
    m_joinButton->setFixedHeight(40);
    m_joinButton->setMinimumWidth(160);
    connect(m_joinButton, &QPushButton::clicked, this, &VoiceChannelView::joinRequested);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 16, 0, 24);
    footer->addStretch();
    footer->addWidget(m_joinButton);
    footer->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_grid, 1);
    layout->addLayout(footer);

    m_emptyLabel->setParent(m_grid);
}

void VoiceChannelView::setChannelName(const QString& name)
{
    m_title->setText(name);
}

void VoiceChannelView::setParticipants(const QList<ParticipantTile::Participant>& participants)
{
    // Reuse tiles where possible; a voice channel rarely has more than a handful of people.
    while (m_tiles.size() > participants.size())
        delete m_tiles.takeLast();
    while (m_tiles.size() < participants.size()) {
        auto* tile = new ParticipantTile(m_grid);
        connect(tile, &ParticipantTile::contextMenuRequested, this, &VoiceChannelView::participantContextMenuRequested);
        tile->show();
        m_tiles.append(tile);
    }
    for (qsizetype i = 0; i < participants.size(); ++i)
        m_tiles[i]->setParticipant(participants[i]);
    m_emptyLabel->setVisible(participants.isEmpty());
    layoutTiles();
}

void VoiceChannelView::setSpeaking(const QString& userId, bool speaking)
{
    for (ParticipantTile* tile : std::as_const(m_tiles)) {
        if (tile->participant().userId == userId)
            tile->setSpeaking(speaking);
    }
}

void VoiceChannelView::setJoinState(bool joined, bool canJoin)
{
    m_joinButton->setVisible(!joined);
    m_joinButton->setEnabled(canJoin);
    m_joinButton->setToolTip(canJoin ? QString() : tr("You don't have permission to join this channel."));
}

void VoiceChannelView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutTiles();
}

void VoiceChannelView::layoutTiles()
{
    const QRect area = m_grid->rect().adjusted(GridMargin, GridMargin, -GridMargin, -GridMargin);
    m_emptyLabel->setGeometry(area);
    if (m_tiles.isEmpty() || area.width() <= 0)
        return;

    // As many 16:9 columns as fit, then center the whole grid.
    const int count = static_cast<int>(m_tiles.size());
    int columns = qBound(1, (area.width() + TileSpacing) / (MinimumTileWidth + TileSpacing), count);
    int rows = (count + columns - 1) / columns;
    int tileWidth = (area.width() - (columns - 1) * TileSpacing) / columns;
    int tileHeight = tileWidth * 9 / 16;
    const int maxHeight = (area.height() - (rows - 1) * TileSpacing) / rows;
    if (tileHeight > maxHeight) {
        tileHeight = qMax(90, maxHeight);
        tileWidth = tileHeight * 16 / 9;
    }
    const int gridHeight = rows * tileHeight + (rows - 1) * TileSpacing;
    const int top = area.top() + qMax(0, (area.height() - gridHeight) / 2);

    for (int i = 0; i < count; ++i) {
        const int row = i / columns;
        const int column = i % columns;
        const int inRow = qMin(columns, count - row * columns);
        const int rowWidth = inRow * tileWidth + (inRow - 1) * TileSpacing;
        const int left = area.left() + (area.width() - rowWidth) / 2;
        m_tiles[i]->setGeometry(left + column * (tileWidth + TileSpacing), top + row * (tileHeight + TileSpacing),
                                tileWidth, tileHeight);
    }
}
