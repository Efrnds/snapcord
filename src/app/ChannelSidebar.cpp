#include "ChannelSidebar.h"

#include "Motion.h"
#include "Theme.h"
#include "UserPanel.h"
#include "VoicePanel.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

enum Role {
    IdRole = Qt::UserRole,
    KindRole,
    SpeakingRole,
    MutedRole,
    DeafenedRole,
    UnreadRole,
    MentionsRole,
    ChannelMutedRole,
    CanCreateRole,
};

using ItemKind = ChannelSidebar::ItemKind;

// Red rounded counter used for unread mentions, right-aligned at `right`. Returns its left edge.
int drawMentionBadge(QPainter* painter, int right, int centerY, int count, const QFont& base)
{
    QFont font = base;
    font.setPixelSize(12);
    font.setWeight(QFont::Bold);
    painter->setFont(font);
    const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);
    const int width = std::max(16, painter->fontMetrics().horizontalAdvance(text) + 10);
    const QRect badge(right - width, centerY - 8, width, 16);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::instance().palette().danger);
    painter->drawRoundedRect(badge, 8, 8);
    painter->setPen(Theme::instance().palette().onAccent);
    painter->drawText(badge, Qt::AlignCenter, text);
    return badge.left();
}

// The "+" drawn on a category row that can have new channels.
QRect createButtonRect(const QRect& row)
{
    return QRect(row.right() - 24, row.bottom() - 21, 16, 16);
}

ItemKind kindOf(const QModelIndex& index)
{
    return static_cast<ItemKind>(index.data(KindRole).toInt());
}

// Paints every row of the channel list the way Discord does, since the rows differ a lot by kind.
class ChannelDelegate : public QStyledItemDelegate
{
public:
    explicit ChannelDelegate(QAbstractItemView* view)
        : QStyledItemDelegate(view)
        , m_animator(new Motion::ItemAnimator(view->viewport()))
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        switch (kindOf(index)) {
        case ItemKind::Category:
            return {0, 40};
        case ItemKind::VoiceMember:
            return {0, 32};
        case ItemKind::DirectMessage:
            return {0, 44};
        default:
            return {0, 34};
        }
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool selected = option.state & QStyle::State_Selected;
        const QRect rect = option.rect;
        QFont font = option.font;
        const Theme::Palette& colors = Theme::instance().palette();
        // Hover and selection fade in and out instead of switching at once. Keys include the kind, since a
        // voice member's user ID can also be a DM channel ID.
        const QString key = QString::number(index.data(KindRole).toInt()) + index.data(IdRole).toString();
        const qreal hover = m_animator->level(u'h' + key, hovered, rect);
        const qreal select = m_animator->level(u's' + key, selected, rect);
        // Row background: nothing, then the hover color, then the selection color.
        auto drawRowBackground = [&](const QRect& row) {
            QColor clear = colors.hover;
            clear.setAlpha(0);
            const QColor color = Motion::mix(Motion::mix(clear, colors.hover, hover), colors.selected, select);
            if (color.alpha() == 0)
                return;
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(row, 4, 4);
        };

        switch (kindOf(index)) {
        case ItemKind::Category: {
            const bool expanded = option.state & QStyle::State_Open;
            const QColor color = Motion::mix(colors.textMuted, colors.text, hover);
            // Chevron: down when expanded, right when collapsed.
            painter->setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF center(rect.left() + 12, rect.bottom() - 13);
            QPolygonF chevron = expanded
                ? QPolygonF{center + QPointF(-3, -1.5), center + QPointF(0, 1.5), center + QPointF(3, -1.5)}
                : QPolygonF{center + QPointF(-1.5, -3), center + QPointF(1.5, 0), center + QPointF(-1.5, 3)};
            painter->drawPolyline(chevron);
            int textRight = rect.right() - 8;
            if (index.data(CanCreateRole).toBool()) {
                textRight = createButtonRect(rect).left() - 4;
                if (hover > 0.0) {
                    QColor plusColor = colors.text;
                    plusColor.setAlphaF(hover);
                    painter->setPen(QPen(plusColor, 1.6, Qt::SolidLine, Qt::RoundCap));
                    const QPointF c = QRectF(createButtonRect(rect)).center();
                    painter->drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
                    painter->drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
                }
            }
            font.setPixelSize(12);
            font.setWeight(QFont::DemiBold);
            painter->setFont(font);
            painter->setPen(color);
            const QRect textRect = QRect(rect.left() + 20, rect.top(), textRight - rect.left() - 20, rect.height() - 6);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom,
                              painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString().toUpper(),
                                                                Qt::ElideRight, textRect.width()));
            break;
        }
        case ItemKind::TextChannel:
        case ItemKind::VoiceChannel: {
            const QRect row = rect.adjusted(8, 1, -8, -1);
            drawRowBackground(row);
            const bool unread = index.data(UnreadRole).toBool();
            const bool channelMuted = index.data(ChannelMutedRole).toBool();
            const int mentions = index.data(MentionsRole).toInt();
            // Unread pill hanging off the left edge of the column.
            if (unread && !selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(colors.textBright);
                painter->drawRoundedRect(QRectF(rect.left() - 4, row.center().y() - 4, 8, 8), 4, 4);
            }
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            icon.paint(painter, QRect(row.left() + 8, row.center().y() - 10, 20, 20));
            int textRight = row.right() - 8;
            if (mentions > 0)
                textRight = drawMentionBadge(painter, row.right() - 8, row.center().y(), mentions, option.font) - 6;
            font.setPixelSize(15);
            font.setWeight(selected || unread ? QFont::DemiBold : QFont::Medium);
            painter->setFont(font);
            const QColor idle = channelMuted ? colors.button : unread ? colors.text : colors.textMuted;
            painter->setPen(Motion::mix(Motion::mix(idle, channelMuted ? idle : colors.text, hover), colors.textBright, select));
            const QRect textRect(row.left() + 36, row.top(), textRight - row.left() - 36, row.height());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                                Qt::ElideRight, textRect.width()));
            break;
        }
        case ItemKind::DirectMessage: {
            const QRect row = rect.adjusted(8, 1, -8, -1);
            drawRowBackground(row);
            const QPixmap avatar = index.data(Qt::DecorationRole).value<QPixmap>();
            painter->drawPixmap(QRect(row.left() + 8, row.center().y() - 16, 32, 32), avatar);
            int right = row.right() - 8;
            if (const int mentions = index.data(MentionsRole).toInt(); mentions > 0)
                right = drawMentionBadge(painter, right, row.center().y(), mentions, option.font) - 6;
            if (index.data(SpeakingRole).toBool()) { // call in progress
                QIcon(QStringLiteral(":/icons/speaker-active.svg")).paint(painter, QRect(right - 18, row.center().y() - 9, 18, 18));
                right -= 24;
            }
            font.setPixelSize(15);
            font.setWeight(selected ? QFont::DemiBold : QFont::Medium);
            painter->setFont(font);
            painter->setPen(Motion::mix(Motion::mix(colors.textMuted, colors.text, hover), colors.textBright, select));
            const QRect textRect(row.left() + 52, row.top(), right - row.left() - 52, row.height());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                                Qt::ElideRight, textRect.width()));
            break;
        }
        case ItemKind::VoiceMember: {
            const QRect row = rect.adjusted(36, 1, -8, -1);
            drawRowBackground(row);
            // Speaking: the avatar shrinks a little inside a green ring, both easing in and out.
            const qreal speaking = m_animator->level(u'v' + key, index.data(SpeakingRole).toBool(), rect);
            const QRectF avatarRect(row.left() + 8, row.center().y() - 12, 24, 24);
            const qreal inset = avatarRect.width() * 0.09 * speaking;
            const QPixmap avatar = index.data(Qt::DecorationRole).value<QPixmap>();
            painter->drawPixmap(avatarRect.adjusted(inset, inset, -inset, -inset), avatar, QRectF(avatar.rect()));
            if (speaking > 0.0) {
                QColor ring = colors.success;
                ring.setAlphaF(speaking);
                const qreal width = avatarRect.width() * 0.06;
                painter->setPen(QPen(ring, width));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(avatarRect.adjusted(width / 2, width / 2, -width / 2, -width / 2));
            }

            int right = row.right() - 6;
            auto drawStatusIcon = [&](const QString& path) {
                QIcon(path).paint(painter, QRect(right - 16, row.center().y() - 8, 16, 16));
                right -= 20;
            };
            if (index.data(DeafenedRole).toBool())
                drawStatusIcon(QStringLiteral(":/icons/headphones-off.svg"));
            if (index.data(MutedRole).toBool())
                drawStatusIcon(QStringLiteral(":/icons/mic-off.svg"));

            font.setPixelSize(14);
            font.setWeight(QFont::Medium);
            painter->setFont(font);
            painter->setPen(Motion::mix(colors.textMuted, colors.textBright, qMax(speaking, hover)));
            const QRect textRect(row.left() + 40, row.top(), right - row.left() - 40, row.height());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                                Qt::ElideRight, textRect.width()));
            break;
        }
        }
        painter->restore();
    }

private:
    Motion::ItemAnimator* m_animator;
};

} // namespace

ChannelSidebar::ChannelSidebar(QWidget* parent)
    : QWidget(parent)
    , m_title(new QLabel)
    , m_tree(new QTreeWidget)
    , m_voicePanel(new VoicePanel)
    , m_userPanel(new UserPanel)
{
    setObjectName(QStringLiteral("channelSidebar"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(240);

    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("sidebarHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(48);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->addWidget(m_title);

    m_tree->setObjectName(QStringLiteral("channelTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(0);
    m_tree->setItemDelegate(new ChannelDelegate(m_tree));
    m_tree->setMouseTracking(true);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ChannelSidebar::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QTreeWidgetItem* item = m_tree->itemAt(position);
        const QPoint global = m_tree->viewport()->mapToGlobal(position);
        if (!item) {
            emit channelContextMenuRequested(QString(), ItemKind::Category, global);
            return;
        }
        const auto kind = static_cast<ItemKind>(item->data(0, KindRole).toInt());
        const QString id = item->data(0, IdRole).toString();
        if (kind == ItemKind::VoiceMember)
            emit memberContextMenuRequested(id, global);
        else if (kind != ItemKind::DirectMessage)
            emit channelContextMenuRequested(id, kind, global);
    });
    connect(&Theme::instance(), &Theme::changed, m_tree->viewport(), QOverload<>::of(&QWidget::update));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_voicePanel);
    layout->addWidget(m_userPanel);
}

void ChannelSidebar::setTitle(const QString& title)
{
    m_title->setText(title);
}

void ChannelSidebar::beginRebuild()
{
    m_savedScroll = m_tree->verticalScrollBar()->value();
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    m_currentCategory = nullptr;
    m_currentVoiceChannel = nullptr;
}

void ChannelSidebar::addCategory(const QString& id, const QString& name, bool canCreate)
{
    auto* item = new QTreeWidgetItem(m_tree, {name});
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, IdRole, id);
    item->setData(0, KindRole, static_cast<int>(ItemKind::Category));
    item->setData(0, CanCreateRole, canCreate);
    m_currentCategory = item;
    m_currentVoiceChannel = nullptr;
}

void ChannelSidebar::addChannel(const QString& id, const QString& name, ItemKind kind, bool unread, int mentions,
                                bool muted)
{
    auto* item = m_currentCategory ? new QTreeWidgetItem(m_currentCategory, {name}) : new QTreeWidgetItem(m_tree, {name});
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(0, IdRole, id);
    item->setData(0, KindRole, static_cast<int>(kind));
    item->setData(0, UnreadRole, unread);
    item->setData(0, MentionsRole, mentions);
    item->setData(0, ChannelMutedRole, muted);
    item->setIcon(0, QIcon(kind == ItemKind::VoiceChannel ? QStringLiteral(":/icons/speaker.svg")
                                                          : QStringLiteral(":/icons/hash.svg")));
    m_currentVoiceChannel = kind == ItemKind::VoiceChannel ? item : nullptr;
    if (id == m_selectedChannel)
        item->setSelected(true);
}

void ChannelSidebar::addDirectMessage(const QString& id, const QString& name, const QPixmap& avatar, bool inCall,
                                      int mentions)
{
    auto* item = m_currentCategory ? new QTreeWidgetItem(m_currentCategory, {name}) : new QTreeWidgetItem(m_tree, {name});
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(0, IdRole, id);
    item->setData(0, KindRole, static_cast<int>(ItemKind::DirectMessage));
    item->setData(0, MentionsRole, mentions);
    item->setData(0, Qt::DecorationRole, avatar);
    item->setData(0, SpeakingRole, inCall);
    m_currentVoiceChannel = item;
    if (id == m_selectedChannel)
        item->setSelected(true);
}

void ChannelSidebar::addVoiceMember(const Member& member)
{
    if (!m_currentVoiceChannel)
        return;
    auto* item = new QTreeWidgetItem(m_currentVoiceChannel, {member.name});
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, IdRole, member.userId);
    item->setData(0, KindRole, static_cast<int>(ItemKind::VoiceMember));
    item->setData(0, Qt::DecorationRole, member.avatar);
    item->setData(0, SpeakingRole, member.speaking);
    item->setData(0, MutedRole, member.muted);
    item->setData(0, DeafenedRole, member.deafened);
}

void ChannelSidebar::endRebuild()
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const bool isCategory = static_cast<ItemKind>(item->data(0, KindRole).toInt()) == ItemKind::Category;
        item->setExpanded(!isCategory || !m_collapsedCategories.contains(item->data(0, IdRole).toString()));
    }
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(m_savedScroll);
}

void ChannelSidebar::setSelectedChannel(const QString& channelId)
{
    m_selectedChannel = channelId;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        (*it)->setSelected((*it)->data(0, IdRole).toString() == channelId);
}

void ChannelSidebar::setMemberSpeaking(const QString& userId, bool speaking)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (static_cast<ItemKind>(item->data(0, KindRole).toInt()) == ItemKind::VoiceMember
            && item->data(0, IdRole).toString() == userId) {
            item->setData(0, SpeakingRole, speaking);
        }
    }
}

void ChannelSidebar::onItemClicked(QTreeWidgetItem* item)
{
    const auto kind = static_cast<ItemKind>(item->data(0, KindRole).toInt());
    const QString id = item->data(0, IdRole).toString();
    if (kind == ItemKind::Category) {
        if (item->data(0, CanCreateRole).toBool()
            && createButtonRect(m_tree->visualItemRect(item)).contains(m_tree->viewport()->mapFromGlobal(QCursor::pos()))) {
            emit createChannelRequested(id);
            return;
        }
        const bool collapse = item->isExpanded();
        item->setExpanded(!collapse);
        if (collapse)
            m_collapsedCategories.insert(id);
        else
            m_collapsedCategories.remove(id);
        return;
    }
    if (kind == ItemKind::VoiceMember) {
        emit memberClicked(id, QCursor::pos());
        return;
    }
    setSelectedChannel(id);
    emit channelClicked(id, kind);
}
