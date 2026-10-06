#include "ServerRail.h"

#include <QButtonGroup>
#include <QFrame>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int ButtonSize = 48;

QPushButton* makeRailButton(const QString& tooltip)
{
    auto* button = new QPushButton;
    button->setFixedSize(ButtonSize, ButtonSize);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

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

ServerRail::ServerRail(QWidget* parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    setObjectName(QStringLiteral("serverRail"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(72);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("serverRailContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* home = makeRailButton(tr("Direct Messages"));
    home->setObjectName(QStringLiteral("homeButton"));
    home->setIcon(QIcon(QStringLiteral(":/icons/home.svg")));
    home->setIconSize({28, 28});
    home->setChecked(true);
    m_group->addButton(home);
    connect(home, &QPushButton::clicked, this, &ServerRail::homeSelected);
    layout->addWidget(home, 0, Qt::AlignHCenter);

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("railSeparator"));
    separator->setFixedSize(32, 2);
    layout->addWidget(separator, 0, Qt::AlignHCenter);

    m_serverLayout = new QVBoxLayout;
    m_serverLayout->setSpacing(8);
    layout->addLayout(m_serverLayout);

    auto* add = new QPushButton;
    add->setObjectName(QStringLiteral("addServerButton"));
    add->setFixedSize(ButtonSize, ButtonSize);
    add->setIcon(QIcon(QStringLiteral(":/icons/plus.svg")));
    add->setIconSize({24, 24});
    add->setCursor(Qt::PointingHandCursor);
    add->setToolTip(tr("Add a Server"));
    layout->addWidget(add, 0, Qt::AlignHCenter);
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

void ServerRail::addServer(const QString& id, const QString& name)
{
    auto* button = makeRailButton(name);
    button->setText(initials(name));
    m_group->addButton(button);
    connect(button, &QPushButton::clicked, this, [this, id] { emit serverSelected(id); });
    m_serverLayout->addWidget(button, 0, Qt::AlignHCenter);
}
