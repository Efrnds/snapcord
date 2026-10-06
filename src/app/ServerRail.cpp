#include "ServerRail.h"

#include <QButtonGroup>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int IconSize = 48;

QString initials(const QString& name)
{
    QString result;
    for (const QString& word : name.split(u' ', Qt::SkipEmptyParts)) {
        result += word.front();
        if (result.size() == 3)
            break;
    }
    return result;
}

} // namespace

ServerButton::ServerButton(QWidget* parent)
    : QAbstractButton(parent)
    , m_accent(0x58, 0x65, 0xf2)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

void ServerButton::setImage(const QImage& image)
{
    m_image = image;
    update();
}

void ServerButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ServerButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ServerButton::setUnreadState(bool unread, int mentions)
{
    if (unread == m_unread && mentions == m_mentions)
        return;
    m_unread = unread;
    m_mentions = mentions;
    update();
}

void ServerButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const bool active = isChecked() || m_hovered;
    const QRectF iconRect((width() - IconSize) / 2.0, (height() - IconSize) / 2.0, IconSize, IconSize);
    const qreal radius = active ? 16.0 : IconSize / 2.0;

    // Pill on the left edge: tall when selected, medium on hover, a small dot when there is something unread.
    if (isChecked() || m_hovered || m_unread) {
        const qreal pillHeight = isChecked() ? 40.0 : m_hovered ? 20.0 : 8.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(-4, (height() - pillHeight) / 2, 8, pillHeight), 4, 4);
    }

    QPainterPath shape;
    shape.addRoundedRect(iconRect, radius, radius);

    painter.save();
    if (!m_image.isNull()) {
        painter.setClipPath(shape);
        painter.drawImage(iconRect, m_image);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? m_accent : QColor(0x31, 0x33, 0x38));
        painter.drawPath(shape);
        if (!m_icon.isNull()) {
            m_icon.paint(&painter, iconRect.adjusted(12, 12, -12, -12).toRect());
        } else {
            QFont font = painter.font();
            font.setPixelSize(m_label.size() > 2 ? 14 : 16);
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            painter.setPen(active ? Qt::white : QColor(0xdb, 0xde, 0xe1));
            painter.drawText(iconRect, Qt::AlignCenter, m_label);
        }
    }
    painter.restore();

    // Mention counter in the bottom-right corner, cut out of the rail background like Discord's.
    if (m_mentions > 0) {
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setWeight(QFont::Bold);
        painter.setFont(font);
        const QString text = m_mentions > 99 ? QStringLiteral("99+") : QString::number(m_mentions);
        const qreal badgeWidth = std::max(16, painter.fontMetrics().horizontalAdvance(text) + 10);
        const QRectF badge(iconRect.right() - badgeWidth + 4, iconRect.bottom() - 12, badgeWidth, 16);
        painter.setPen(QPen(QColor(0x1e, 0x1f, 0x22), 4));
        painter.setBrush(QColor(0xf2, 0x3f, 0x43));
        painter.drawRoundedRect(badge, 8, 8);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, text);
    }
}

ServerRail::ServerRail(QWidget* parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
    , m_home(new ServerButton)
{
    setObjectName(QStringLiteral("serverRail"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(72);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("serverRailContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(0);

    m_home->setToolTip(tr("Direct Messages"));
    m_home->setIconImage(QIcon(QStringLiteral(":/icons/home.svg")));
    m_group->addButton(m_home);
    connect(m_home, &QAbstractButton::clicked, this, &ServerRail::homeSelected);
    layout->addWidget(m_home);

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("railSeparator"));
    separator->setFixedSize(32, 2);
    layout->addSpacing(4);
    layout->addWidget(separator, 0, Qt::AlignHCenter);
    layout->addSpacing(4);

    m_serverLayout = new QVBoxLayout;
    m_serverLayout->setSpacing(0);
    layout->addLayout(m_serverLayout);
    layout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void ServerRail::clearServers()
{
    for (ServerButton* button : std::as_const(m_buttons)) {
        m_group->removeButton(button);
        delete button;
    }
    m_buttons.clear();
}

void ServerRail::addServer(const QString& id, const QString& name, const QImage& icon)
{
    auto* button = new ServerButton;
    button->setToolTip(name);
    button->setLabel(initials(name));
    button->setImage(icon);
    m_group->addButton(button);
    connect(button, &QAbstractButton::clicked, this, [this, id] { emit serverSelected(id); });
    m_serverLayout->addWidget(button);
    m_buttons.insert(id, button);
}

void ServerRail::setServerIcon(const QString& id, const QImage& icon)
{
    if (ServerButton* button = m_buttons.value(id))
        button->setImage(icon);
}

void ServerRail::setServerUnread(const QString& id, bool unread, int mentions)
{
    if (ServerButton* button = m_buttons.value(id))
        button->setUnreadState(unread, mentions);
}

void ServerRail::setHomeMentions(int mentions)
{
    m_home->setUnreadState(false, mentions);
}

void ServerRail::select(const QString& id)
{
    if (id.isEmpty())
        m_home->setChecked(true);
    else if (ServerButton* button = m_buttons.value(id))
        button->setChecked(true);
}
