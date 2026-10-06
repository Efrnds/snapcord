#include "MainWindow.h"

#include "Avatar.h"
#include "Language.h"
#include "ServerRail.h"
#include "UserPanel.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_rail(new ServerRail)
    , m_sidebar(new ChannelSidebar)
{
    setWindowTitle(QStringLiteral("Snapcord"));
    resize(1280, 720);
    setMinimumSize(940, 500);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);
    layout->addWidget(m_sidebar);
    layout->addWidget(buildChatArea(), 1);
    layout->addWidget(buildMemberList());
    setCentralWidget(central);

    connect(m_sidebar, &ChannelSidebar::channelActivated, this,
            [this](const QString&, const QString& name, ChannelSidebar::ChannelKind kind) { showChannel(name, kind); });
    connect(m_sidebar->userPanel(), &UserPanel::settingsRequested, this, &MainWindow::showSettingsMenu);

    loadDemoData();
}

QWidget* MainWindow::buildChatArea()
{
    auto* area = new QWidget;
    area->setObjectName(QStringLiteral("chatArea"));
    area->setAttribute(Qt::WA_StyledBackground);

    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(48);
    m_chatIcon = new QLabel;
    m_chatIcon->setFixedSize(24, 24);
    m_chatTitle = new QLabel;
    m_chatTitle->setObjectName(QStringLiteral("chatTitle"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(m_chatIcon);
    headerLayout->addWidget(m_chatTitle);
    headerLayout->addStretch();

    auto* welcome = new QLabel(tr("Welcome to Snapcord"));
    welcome->setObjectName(QStringLiteral("welcomeTitle"));
    welcome->setAlignment(Qt::AlignCenter);
    auto* subtitle = new QLabel(tr("This is a preview of the interface (Phase 0).\n"
                                   "QR code login and voice calls arrive in Phase 1."));
    subtitle->setObjectName(QStringLiteral("welcomeSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);

    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addStretch();
    layout->addWidget(welcome);
    layout->addSpacing(8);
    layout->addWidget(subtitle);
    layout->addStretch();
    return area;
}

QWidget* MainWindow::buildMemberList()
{
    m_members = new QListWidget;
    m_members->setObjectName(QStringLiteral("memberList"));
    m_members->setFixedWidth(240);
    m_members->setIconSize({32, 32});
    m_members->setFocusPolicy(Qt::NoFocus);
    m_members->setSelectionMode(QAbstractItemView::NoSelection);
    m_members->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return m_members;
}

// Demo data, only to preview the layout. Replaced by real data in Phase 1.
void MainWindow::loadDemoData()
{
    static const QHash<QString, QString> servers = {
        {QStringLiteral("1"), QStringLiteral("Snapcord Dev")},
        {QStringLiteral("2"), QStringLiteral("Gaming Group")},
        {QStringLiteral("3"), QStringLiteral("Community")},
    };
    for (const QString& id : {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")})
        m_rail->addServer(id, servers.value(id));
    connect(m_rail, &ServerRail::serverSelected, this, [this](const QString& id) { showServer(servers.value(id)); });
    connect(m_rail, &ServerRail::homeSelected, this, [this] { showServer(tr("Direct Messages")); });

    m_sidebar->userPanel()->setUser(QStringLiteral("Pedro"), tr("Online"));
    showServer(tr("Direct Messages"));

    const QStringList members = {QStringLiteral("Pedro"), QStringLiteral("Ana"), QStringLiteral("Lucas")};
    auto* header = new QListWidgetItem(tr("ONLINE — %1").arg(members.size()), m_members);
    header->setFlags(Qt::NoItemFlags);
    QFont headerFont = header->font();
    headerFont.setPixelSize(12);
    headerFont.setWeight(QFont::DemiBold);
    header->setFont(headerFont);
    header->setSizeHint({0, 40});
    for (const QString& name : members) {
        auto* item = new QListWidgetItem(QIcon(makeAvatar(name, 32, devicePixelRatioF(), QColor(0x2b, 0x2d, 0x31))),
                                         name, m_members);
        item->setSizeHint({0, 44});
    }
}

void MainWindow::showServer(const QString& name)
{
    m_sidebar->setTitle(name);
    m_sidebar->clear();
    m_sidebar->addCategory(tr("Text Channels"));
    m_sidebar->addChannel(QStringLiteral("t1"), QStringLiteral("general"), ChannelSidebar::ChannelKind::Text);
    m_sidebar->addChannel(QStringLiteral("t2"), QStringLiteral("announcements"), ChannelSidebar::ChannelKind::Text);
    m_sidebar->addCategory(tr("Voice Channels"));
    m_sidebar->addChannel(QStringLiteral("v1"), QStringLiteral("General"), ChannelSidebar::ChannelKind::Voice);
    m_sidebar->addChannel(QStringLiteral("v2"), QStringLiteral("Gaming"), ChannelSidebar::ChannelKind::Voice);
    m_sidebar->selectFirstTextChannel();
}

void MainWindow::showChannel(const QString& name, ChannelSidebar::ChannelKind kind)
{
    const QIcon icon(kind == ChannelSidebar::ChannelKind::Voice ? QStringLiteral(":/icons/speaker.svg")
                                                                : QStringLiteral(":/icons/hash.svg"));
    m_chatIcon->setPixmap(icon.pixmap(QSize(24, 24), devicePixelRatioF()));
    m_chatTitle->setText(name);
}

void MainWindow::showSettingsMenu()
{
    QMenu menu(this);
    QMenu* languageMenu = menu.addMenu(tr("Language"));
    auto* group = new QActionGroup(languageMenu);
    const QString current = Language::current();
    for (const auto& [code, name] : Language::available()) {
        QAction* action = languageMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(code == current);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }
    menu.exec(QCursor::pos());
}

void MainWindow::changeLanguage(const QString& code)
{
    if (code == Language::current())
        return;
    Language::setCurrent(code);

    QMessageBox box(this);
    box.setWindowTitle(tr("Language"));
    box.setText(tr("Restart Snapcord to apply the new language."));
    QPushButton* restartNow = box.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == restartNow) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    }
}
