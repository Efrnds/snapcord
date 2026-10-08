#include "ServerRail.h"

#include "Motion.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFrame>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int IconSize = 48;
constexpr int SlotHeight = 56; // height of each button of the rail
constexpr auto MimeType = "application/x-snapcord-server";
constexpr auto OpenFoldersKey = "ui/openFolders";

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

QRectF iconRectOf(const QWidget* widget)
{
    return {(widget->width() - IconSize) / 2.0, (widget->height() - IconSize) / 2.0, IconSize, IconSize};
}

// The pill grows out of the left edge: it starts narrow and reaches full width at the size of the unread dot.
void drawPill(QPainter& painter, const QWidget* widget, qreal pillHeight)
{
    if (pillHeight <= 0.5)
        return;
    const qreal pillWidth = 8.0 * qMin(1.0, pillHeight / 8.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(-4 - (8.0 - pillWidth), (widget->height() - pillHeight) / 2, 8, pillHeight), 4, 4);
}

// Mention counter in the bottom-right corner, cut out of the rail background like Discord's.
void drawMentionBadge(QPainter& painter, const QRectF& iconRect, int mentions)
{
    if (mentions <= 0)
        return;
    QFont font = painter.font();
    font.setPixelSize(12);
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    const QString text = mentions > 99 ? QStringLiteral("99+") : QString::number(mentions);
    const qreal badgeWidth = std::max(16, painter.fontMetrics().horizontalAdvance(text) + 10);
    const QRectF badge(iconRect.right() - badgeWidth + 4, iconRect.bottom() - 12, badgeWidth, 16);
    painter.setPen(QPen(Theme::instance().palette().bg0, 4));
    painter.setBrush(Theme::instance().palette().danger);
    painter.drawRoundedRect(badge, 8, 8);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, text);
}

// The green "+" of "Add a Server", drawn instead of loaded so it can change color.
QIcon plusIcon(const QColor& color)
{
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(24, 8), QPointF(24, 40));
    painter.drawLine(QPointF(8, 24), QPointF(40, 24));
    return QIcon(pixmap);
}

} // namespace

// The scrolling column of the rail. It paints the background of open folders behind their servers and
// receives the servers and folders dropped while dragging.
class RailContent : public QWidget
{
public:
    explicit RailContent(ServerRail* rail)
        : QWidget(rail)
        , m_rail(rail)
        , m_indicator(new Indicator(this))
    {
        setObjectName(QStringLiteral("serverRailContent"));
        setAcceptDrops(true);
    }

    void showPlan(const ServerRail::DropPlan& plan)
    {
        m_indicator->plan = plan;
        m_indicator->raise();
        m_indicator->update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& colors = Theme::instance().palette();
        painter.fillRect(rect(), colors.bg0);
        // Open folders: one rounded column from the folder symbol down to its last server.
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.bg2);
        // It grows with the box of servers while the folder opens.
        for (auto it = m_rail->m_folderGroups.cbegin(); it != m_rail->m_folderGroups.cend(); ++it) {
            const qreal openness = it.value().openness->value();
            const FolderButton* folder = m_rail->m_folderButtons.value(it.key());
            if (openness <= 0.0 || !folder)
                continue;
            const int top = folder->geometry().top() + 4;
            const int bottom = std::max(folder->geometry().bottom(), it.value().box->geometry().bottom());
            painter.setOpacity(std::min(1.0, openness * 2));
            painter.drawRoundedRect(QRect((width() - IconSize) / 2, top, IconSize, bottom - 3 - top), 16, 16);
        }
        painter.setOpacity(1.0);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        m_indicator->setGeometry(rect());
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(QLatin1String(MimeType)) && event->source() == m_rail)
            event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        const ServerRail::DropPlan plan = m_rail->planDrop(dragged(event), event->position().toPoint().y());
        showPlan(plan);
        // Keep accepting so the drop still arrives; an invalid plan is simply ignored.
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override { showPlan({}); }

    void dropEvent(QDropEvent* event) override
    {
        const QString key = dragged(event);
        const ServerRail::DropPlan plan = m_rail->planDrop(key, event->position().toPoint().y());
        showPlan({});
        event->acceptProposedAction();
        // Rearranging rebuilds the buttons, one of which started this drag: do it once the drag is over.
        QTimer::singleShot(0, m_rail, [rail = m_rail, key, plan] { rail->finishDrop(key, plan); });
    }

private:
    class Indicator : public QWidget
    {
    public:
        explicit Indicator(QWidget* parent)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
        }

        ServerRail::DropPlan plan;

    protected:
        void paintEvent(QPaintEvent*) override
        {
            if (!plan.valid)
                return;
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            const QColor color = Theme::instance().palette().success;
            if (!plan.ring.isNull()) {
                painter.setPen(QPen(color, 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(QRectF(plan.ring).adjusted(1, 1, -1, -1), 16, 16);
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawRoundedRect(plan.line, 1, 1);
            }
        }
    };

    static QString dragged(const QDropEvent* event)
    {
        return QString::fromUtf8(event->mimeData()->data(QLatin1String(MimeType)));
    }

    ServerRail* m_rail;
    Indicator* m_indicator;
};

// --- ServerButton ----------------------------------------------------------------------------------

ServerButton::ServerButton(QWidget* parent)
    : QAbstractButton(parent)
    , m_accent(Theme::instance().accent())
    , m_pill(new Motion::Value(this))
    , m_active(new Motion::Value(this))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    connect(this, &QAbstractButton::toggled, this, &ServerButton::updateTargets);
}

void ServerButton::updateTargets()
{
    // Pill: tall when selected, medium on hover, a small dot when there is something unread.
    m_pill->animateTo(isChecked() ? 40.0 : m_hovered ? 20.0 : m_unread ? 8.0 : 0.0);
    m_active->animateTo(isChecked() || m_hovered ? 1.0 : 0.0);
}

void ServerButton::setImage(const QImage& image)
{
    m_image = image;
    update();
}

void ServerButton::setIconImage(const QIcon& icon, const QIcon& activeIcon)
{
    m_icon = icon;
    m_activeIcon = activeIcon;
    update();
}

void ServerButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateTargets();
    QAbstractButton::enterEvent(event);
}

void ServerButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateTargets();
    QAbstractButton::leaveEvent(event);
}

void ServerButton::setUnreadState(bool unread, int mentions)
{
    if (unread == m_unread && mentions == m_mentions)
        return;
    m_unread = unread;
    m_mentions = mentions;
    updateTargets();
    update();
}

void ServerButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const qreal active = m_active->value();
    const QRectF iconRect = iconRectOf(this);
    const qreal radius = IconSize / 2.0 + (16.0 - IconSize / 2.0) * active;

    drawPill(painter, this, m_pill->value());

    QPainterPath shape;
    shape.addRoundedRect(iconRect, radius, radius);

    painter.save();
    if (!m_image.isNull()) {
        painter.setClipPath(shape);
        painter.drawImage(iconRect, m_image);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Motion::mix(Theme::instance().palette().bg2, m_accent, active));
        painter.drawPath(shape);
        if (!m_icon.isNull()) {
            const QIcon& icon = !m_activeIcon.isNull() && active > 0.5 ? m_activeIcon : m_icon;
            icon.paint(&painter, iconRect.adjusted(12, 12, -12, -12).toRect());
        } else {
            QFont font = painter.font();
            font.setPixelSize(m_label.size() > 2 ? 14 : 16);
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            painter.setPen(Motion::mix(Theme::instance().palette().text, Qt::white, active));
            painter.drawText(iconRect, Qt::AlignCenter, m_label);
        }
    }
    painter.restore();

    drawMentionBadge(painter, iconRect, m_mentions);
}

// --- FolderButton ----------------------------------------------------------------------------------

FolderButton::FolderButton(QWidget* parent)
    : QAbstractButton(parent)
    , m_color(ServerRail::defaultFolderColor())
    , m_pill(new Motion::Value(this))
    , m_openness(new Motion::Value(this, 0.0, Motion::Normal))
{
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

void FolderButton::updateTargets()
{
    // An open folder shows its servers' own pills instead.
    m_pill->animateTo(m_open ? 0.0 : m_selected ? 40.0 : m_hovered ? 20.0 : m_unread ? 8.0 : 0.0);
}

void FolderButton::setOpen(bool open)
{
    m_open = open;
    m_openness->animateTo(open ? 1.0 : 0.0);
    updateTargets();
    update();
}

void FolderButton::setSelected(bool selected)
{
    m_selected = selected;
    updateTargets();
}

void FolderButton::setUnreadState(bool unread, int mentions)
{
    if (unread == m_unread && mentions == m_mentions)
        return;
    m_unread = unread;
    m_mentions = mentions;
    updateTargets();
    update();
}

void FolderButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateTargets();
    update();
    QAbstractButton::enterEvent(event);
}

void FolderButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateTargets();
    update();
    QAbstractButton::leaveEvent(event);
}

void FolderButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRectF iconRect = iconRectOf(this);
    drawPill(painter, this, m_pill->value());

    // Opening fades the tile of icons into the folder symbol, and closing back.
    const qreal openness = m_openness->value();
    if (openness > 0.0) {
        painter.save();
        painter.setOpacity(openness);
        // A folder symbol in the folder's color, on the open folder's background.
        const QPointF o = iconRect.center() - QPointF(12, 10);
        QPainterPath folder;
        folder.moveTo(o + QPointF(0, 2));
        folder.lineTo(o + QPointF(9, 2));
        folder.lineTo(o + QPointF(11, 5));
        folder.lineTo(o + QPointF(24, 5));
        folder.lineTo(o + QPointF(24, 20));
        folder.lineTo(o + QPointF(0, 20));
        folder.closeSubpath();
        painter.setPen(QPen(m_color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(m_color);
        painter.drawPath(folder);
        if (m_hovered && m_open) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 18));
            painter.drawRoundedRect(iconRect, 16, 16);
        }
        painter.restore();
    }
    if (openness >= 1.0)
        return;
    painter.setOpacity(1.0 - openness);

    // Closed: a tile tinted with the folder color holding the first four server icons.
    QColor tint = m_color;
    tint.setAlphaF(m_hovered ? 0.5 : 0.4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tint);
    painter.drawRoundedRect(iconRect, 16, 16);

    const auto& colors = Theme::instance().palette();
    QFont font = painter.font();
    font.setPixelSize(7);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    for (qsizetype i = 0; i < std::min<qsizetype>(4, m_previews.size()); ++i) {
        const QRectF cell(iconRect.left() + 6 + (i % 2) * 20, iconRect.top() + 6 + (i / 2) * 20, 16, 16);
        const auto& [image, label] = m_previews[i];
        QPainterPath circle;
        circle.addEllipse(cell);
        if (!image.isNull()) {
            painter.save();
            painter.setClipPath(circle);
            painter.drawImage(cell, image);
            painter.restore();
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.bg2);
            painter.drawPath(circle);
            painter.setPen(colors.text);
            painter.drawText(cell, Qt::AlignCenter, label.left(2));
        }
    }

    drawMentionBadge(painter, iconRect, m_mentions);
}

// --- ServerRail ------------------------------------------------------------------------------------

QColor ServerRail::defaultFolderColor()
{
    return QColor(0x58, 0x65, 0xF2);
}

ServerRail::ServerRail(QWidget* parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
    , m_home(new ServerButton)
    , m_add(new ServerButton)
    , m_content(new RailContent(this))
{
    setObjectName(QStringLiteral("serverRail"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(72);

    const QStringList open = QSettings().value(QLatin1String(OpenFoldersKey)).toStringList();
    m_openFolders = QSet<QString>(open.begin(), open.end());

    auto* layout = new QVBoxLayout(m_content);
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

    // "Add a Server": not checkable, it opens a dialog.
    auto applyAddColors = [this] {
        const auto& colors = Theme::instance().palette();
        m_add->setAccent(colors.success);
        m_add->setIconImage(plusIcon(colors.success), plusIcon(Qt::white));
    };
    m_add->setCheckable(false);
    m_add->setToolTip(tr("Add a Server"));
    applyAddColors();
    connect(m_add, &QAbstractButton::clicked, this, &ServerRail::addServerRequested);
    layout->addWidget(m_add);
    layout->addStretch();

    connect(&Theme::instance(), &Theme::changed, this, [this, applyAddColors] {
        const QColor accent = Theme::instance().accent();
        m_home->setAccent(accent);
        for (ServerButton* button : std::as_const(m_buttons))
            button->setAccent(accent);
        applyAddColors();
        m_content->update();
        update();
    });

    auto* scroll = new QScrollArea;
    scroll->setWidget(m_content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void ServerRail::setServers(const QList<GuildFolder>& folders, const QHash<QString, ServerInfo>& servers)
{
    m_folders = folders;
    m_servers = servers;
    rebuild();
}

void ServerRail::rebuild()
{
    for (QAbstractButton* button : m_group->buttons()) {
        if (button != m_home)
            m_group->removeButton(button);
    }
    while (QLayoutItem* item = m_serverLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Deleted later: this may run while one of them is still handling an event.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_buttons.clear();
    m_folderButtons.clear();
    m_folderGroups.clear();
    m_slots.clear();

    const QColor accent = Theme::instance().accent();
    auto addServer = [&](const QString& id, const QString& folderId, qsizetype entry, QWidget* box) {
        const ServerInfo info = m_servers.value(id);
        auto* button = new ServerButton;
        button->setToolTip(info.name);
        button->setLabel(initials(info.name));
        button->setImage(info.icon);
        button->setAccent(accent);
        const auto unread = m_unread.value(id);
        button->setUnreadState(unread.first, unread.second);
        button->installEventFilter(this);
        m_group->addButton(button);
        connect(button, &QAbstractButton::clicked, this, [this, id] { emit serverSelected(id); });
        if (box) {
            // Servers of a folder are placed by hand: the box clips them while it slides.
            button->setParent(box);
            button->move(0, (int(box->children().size()) - 1) * SlotHeight);
        } else {
            m_serverLayout->addWidget(button);
        }
        m_buttons.insert(id, button);
        m_slots.append({button, id, folderId, entry});
    };

    for (qsizetype i = 0; i < m_folders.size(); ++i) {
        const GuildFolder& entry = m_folders[i];
        QStringList shown;
        for (const QString& id : entry.guildIds) {
            if (m_servers.contains(id))
                shown.append(id);
        }
        if (shown.isEmpty())
            continue;
        if (!entry.isFolder()) {
            addServer(shown.first(), {}, i, nullptr);
            continue;
        }

        const bool open = m_openFolders.contains(entry.id);
        auto* folder = new FolderButton;
        folder->setColor(entry.color >= 0 ? QColor::fromRgb(QRgb(entry.color)) : defaultFolderColor());
        folder->setOpen(open);
        QStringList names;
        QList<std::pair<QImage, QString>> previews;
        for (const QString& id : std::as_const(shown)) {
            const ServerInfo info = m_servers.value(id);
            names.append(info.name);
            if (previews.size() < 4)
                previews.append({info.icon, initials(info.name)});
        }
        folder->setPreviews(previews);
        folder->setToolTip(entry.name.isEmpty() ? names.join(QStringLiteral(", ")) : entry.name);
        folder->installEventFilter(this);
        connect(folder, &QAbstractButton::clicked, this, [this, id = entry.id] { toggleFolder(id); });
        m_serverLayout->addWidget(folder);
        m_folderButtons.insert(entry.id, folder);
        m_slots.append({folder, {}, entry.id, i});

        auto* box = new QWidget;
        for (const QString& id : std::as_const(shown))
            addServer(id, entry.id, i, box);
        const int fullHeight = int(shown.size()) * SlotHeight;
        // Owned by the content, which repaints the folder background on every step.
        auto* openness = new Motion::Value(m_content, open ? 1.0 : 0.0, Motion::Normal);
        auto resize = [box, fullHeight](qreal value) {
            const int height = qRound(value * fullHeight);
            box->setFixedSize(72, height);
            // Hidden when closed, so its servers take no clicks or keyboard focus.
            box->setVisible(height > 0);
        };
        openness->setOnChange(resize);
        resize(openness->value());
        m_serverLayout->addWidget(box);
        m_folderGroups.insert(entry.id, {box, openness});
    }

    select(m_selected);
    refreshFolderBadges();
    m_content->update();
}

void ServerRail::toggleFolder(const QString& folderId)
{
    if (!m_openFolders.remove(folderId))
        m_openFolders.insert(folderId);
    QSettings().setValue(QLatin1String(OpenFoldersKey), QStringList(m_openFolders.begin(), m_openFolders.end()));
    const bool open = m_openFolders.contains(folderId);
    if (FolderButton* folder = m_folderButtons.value(folderId))
        folder->setOpen(open);
    if (const FolderGroup group = m_folderGroups.value(folderId); group.openness)
        group.openness->animateTo(open ? 1.0 : 0.0);
    refreshFolderBadges();
}

QRect ServerRail::slotRect(const QAbstractButton* button) const
{
    return {button->mapTo(m_content, QPoint(0, 0)), button->size()};
}

bool ServerRail::isShown(const Slot& slot) const
{
    return slot.guildId.isEmpty() || slot.folderId.isEmpty() || m_openFolders.contains(slot.folderId);
}

void ServerRail::setServerIcon(const QString& id, const QImage& icon)
{
    if (m_servers.contains(id))
        m_servers[id].icon = icon;
    if (ServerButton* button = m_buttons.value(id))
        button->setImage(icon);
    // Closed folders show the icon too.
    const qsizetype entry = GuildFolders::entryOf(m_folders, id);
    if (entry >= 0 && m_folders[entry].isFolder()) {
        if (FolderButton* folder = m_folderButtons.value(m_folders[entry].id)) {
            QList<std::pair<QImage, QString>> previews;
            for (const QString& guildId : m_folders[entry].guildIds) {
                if (previews.size() == 4)
                    break;
                if (m_servers.contains(guildId))
                    previews.append({m_servers[guildId].icon, initials(m_servers[guildId].name)});
            }
            folder->setPreviews(previews);
        }
    }
}

void ServerRail::setServerUnread(const QString& id, bool unread, int mentions)
{
    m_unread.insert(id, {unread, mentions});
    if (ServerButton* button = m_buttons.value(id))
        button->setUnreadState(unread, mentions);
    const qsizetype entry = GuildFolders::entryOf(m_folders, id);
    if (entry >= 0 && m_folders[entry].isFolder()) {
        if (FolderButton* folder = m_folderButtons.value(m_folders[entry].id)) {
            bool anyUnread = false;
            int total = 0;
            for (const QString& guildId : m_folders[entry].guildIds) {
                const auto state = m_unread.value(guildId);
                anyUnread = anyUnread || state.first;
                total += state.second;
            }
            // An open folder leaves the counters to its servers.
            const bool open = m_openFolders.contains(m_folders[entry].id);
            folder->setUnreadState(anyUnread, open ? 0 : total);
        }
    }
}

void ServerRail::refreshFolderBadges()
{
    for (const GuildFolder& entry : std::as_const(m_folders)) {
        if (entry.isFolder() && !entry.guildIds.isEmpty()) {
            const auto state = m_unread.value(entry.guildIds.first());
            setServerUnread(entry.guildIds.first(), state.first, state.second);
        }
    }
}

void ServerRail::setHomeMentions(int mentions)
{
    m_home->setUnreadState(false, mentions);
}

void ServerRail::select(const QString& id)
{
    m_selected = id;
    if (id.isEmpty()) {
        m_home->setChecked(true);
    } else if (ServerButton* button = m_buttons.value(id)) {
        button->setChecked(true);
    } else if (QAbstractButton* checked = m_group->checkedButton()) {
        // The server is inside a closed folder: no button is selected, the folder shows the pill.
        m_group->setExclusive(false);
        checked->setChecked(false);
        m_group->setExclusive(true);
    }
    for (auto it = m_folderButtons.cbegin(); it != m_folderButtons.cend(); ++it) {
        const qsizetype entry = GuildFolders::folderIndex(m_folders, it.key());
        it.value()->setSelected(!id.isEmpty() && entry >= 0 && m_folders[entry].guildIds.contains(id));
    }
}

bool ServerRail::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QAbstractButton*>(watched);
    if (!button)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_pressed = button;
            m_pressPosition = mouse->position().toPoint();
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_pressed == button && (mouse->buttons() & Qt::LeftButton)
            && (mouse->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = nullptr;
            startDrag(button);
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        m_pressed = nullptr;
        break;
    case QEvent::ContextMenu: {
        const QPoint position = static_cast<QContextMenuEvent*>(event)->globalPos();
        for (const Slot& slot : std::as_const(m_slots)) {
            if (slot.button != button)
                continue;
            if (!slot.guildId.isEmpty())
                emit serverContextMenuRequested(slot.guildId, position);
            else
                emit folderContextMenuRequested(slot.folderId, position);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void ServerRail::startDrag(QAbstractButton* button)
{
    QString key;
    for (const Slot& slot : std::as_const(m_slots)) {
        if (slot.button == button)
            key = slot.guildId.isEmpty() ? QStringLiteral("f:") + slot.folderId : QStringLiteral("g:") + slot.guildId;
    }
    if (key.isEmpty())
        return;

    auto* mime = new QMimeData;
    mime->setData(QLatin1String(MimeType), key.toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const QRect iconRect = iconRectOf(button).toRect();
    drag->setPixmap(button->grab(iconRect));
    drag->setHotSpot(m_pressPosition - iconRect.topLeft());
    // The press never gets its release: the drag takes the mouse.
    button->setDown(false);
    drag->exec(Qt::MoveAction);
    drag->deleteLater();
}

ServerRail::DropPlan ServerRail::planDrop(const QString& dragged, int y) const
{
    DropPlan plan;
    if (m_slots.isEmpty() || dragged.size() < 3)
        return plan;
    const bool draggingFolder = dragged.startsWith(u"f:");
    const QString id = dragged.mid(2);
    const int left = (m_content->width() - IconSize) / 2;
    auto lineAt = [&](int lineY) { return QRect(left, lineY - 1, IconSize, 3); };

    // The button under the mouse (or the nearest one above/below the list).
    qsizetype index = -1;
    for (qsizetype i = 0; i < m_slots.size(); ++i) {
        if (!isShown(m_slots[i]))
            continue;
        index = i;
        if (y <= slotRect(m_slots[i].button).bottom())
            break;
    }
    if (index < 0)
        return plan;
    const Slot& slot = m_slots[index];
    const QRect rect = slotRect(slot.button);
    const qreal fraction = qBound(0.0, (y - rect.top()) / qreal(rect.height()), 1.0);
    using Drop = GuildFolders::Drop;

    if (draggingFolder) {
        // Folders move between top-level entries: an open folder counts as one block with its servers.
        QRect block;
        for (const Slot& other : m_slots) {
            if (other.entry == slot.entry && isShown(other))
                block = block.isNull() ? slotRect(other.button) : block.united(slotRect(other.button));
        }
        const GuildFolder& entry = m_folders[slot.entry];
        const qreal blockFraction = (y - block.top()) / qreal(block.height());
        plan.drop.kind = blockFraction < 0.5 ? Drop::Before : Drop::After;
        if (entry.isFolder())
            plan.drop.folderId = entry.id;
        else
            plan.drop.guildId = entry.guildIds.first();
        plan.line = lineAt(plan.drop.kind == Drop::Before ? block.top() : block.bottom() + 1);
    } else if (!slot.guildId.isEmpty()) {
        plan.drop.guildId = slot.guildId;
        if (!slot.folderId.isEmpty()) {
            // Inside an open folder: between its servers.
            plan.drop.kind = fraction < 0.5 ? Drop::Before : Drop::After;
        } else {
            plan.drop.kind = fraction < 0.25 ? Drop::Before : fraction > 0.75 ? Drop::After : Drop::Combine;
        }
    } else if (m_openFolders.contains(slot.folderId)) {
        // An open folder's symbol: above the folder, or into it as its first server.
        const qsizetype entry = GuildFolders::folderIndex(m_folders, slot.folderId);
        if (fraction < 0.5 || entry < 0) {
            plan.drop = {Drop::Before, {}, slot.folderId};
        } else {
            plan.drop = {Drop::Before, m_folders[entry].guildIds.first(), {}};
            plan.line = lineAt(rect.bottom() + 1);
        }
    } else {
        plan.drop.folderId = slot.folderId;
        plan.drop.kind = fraction < 0.25 ? Drop::Before : fraction > 0.75 ? Drop::After : Drop::Combine;
    }

    if (plan.line.isNull()) {
        if (plan.drop.kind == Drop::Combine)
            plan.ring = iconRectOf(slot.button).toRect().translated(rect.topLeft()).adjusted(-3, -3, 3, 3);
        else
            plan.line = lineAt(plan.drop.kind == Drop::Before ? rect.top() : rect.bottom() + 1);
    }

    // Only a drop that changes something is shown.
    const QList<GuildFolder> result = draggingFolder ? GuildFolders::moveFolder(m_folders, id, plan.drop)
                                                     : GuildFolders::moveGuild(m_folders, id, plan.drop, QStringLiteral("0"));
    plan.valid = result != m_folders;
    return plan;
}

void ServerRail::finishDrop(const QString& dragged, const DropPlan& plan)
{
    if (!plan.valid)
        return;
    const QString id = dragged.mid(2);
    const QList<GuildFolder> result = dragged.startsWith(u"f:")
        ? GuildFolders::moveFolder(m_folders, id, plan.drop)
        : GuildFolders::moveGuild(m_folders, id, plan.drop, GuildFolders::newFolderId());
    emit foldersChanged(result);
}
