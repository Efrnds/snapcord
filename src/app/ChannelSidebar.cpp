#include "ChannelSidebar.h"

#include "UserPanel.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

constexpr int IdRole = Qt::UserRole;
constexpr int KindRole = Qt::UserRole + 1;

} // namespace

ChannelSidebar::ChannelSidebar(QWidget* parent)
    : QWidget(parent)
    , m_title(new QLabel)
    , m_tree(new QTreeWidget)
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
    m_tree->setIconSize({20, 20});
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ChannelSidebar::onItemClicked);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_userPanel);
}

void ChannelSidebar::setTitle(const QString& title)
{
    m_title->setText(title);
}

void ChannelSidebar::clear()
{
    m_tree->clear();
    m_currentCategory = nullptr;
}

void ChannelSidebar::addCategory(const QString& name)
{
    auto* item = new QTreeWidgetItem(m_tree, {name.toUpper()});
    item->setFlags(Qt::ItemIsEnabled);
    QFont font = item->font(0);
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    item->setFont(0, font);
    item->setSizeHint(0, {0, 40});
    item->setExpanded(true);
    m_currentCategory = item;
}

void ChannelSidebar::addChannel(const QString& id, const QString& name, ChannelKind kind)
{
    auto* item = m_currentCategory ? new QTreeWidgetItem(m_currentCategory, {name})
                                   : new QTreeWidgetItem(m_tree, {name});
    item->setIcon(0, QIcon(kind == ChannelKind::Voice ? QStringLiteral(":/icons/speaker.svg")
                                                      : QStringLiteral(":/icons/hash.svg")));
    item->setData(0, IdRole, id);
    item->setData(0, KindRole, static_cast<int>(kind));
    item->setSizeHint(0, {0, 34});
}

void ChannelSidebar::selectFirstTextChannel()
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (!item->data(0, IdRole).isNull()
            && item->data(0, KindRole).toInt() == static_cast<int>(ChannelKind::Text)) {
            m_tree->setCurrentItem(item);
            onItemClicked(item);
            return;
        }
    }
}

void ChannelSidebar::onItemClicked(QTreeWidgetItem* item)
{
    const QVariant id = item->data(0, IdRole);
    if (id.isNull()) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    emit channelActivated(id.toString(), item->text(0), static_cast<ChannelKind>(item->data(0, KindRole).toInt()));
}
