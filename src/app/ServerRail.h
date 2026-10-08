#pragma once

#include "core/GuildFolders.h"

#include <QAbstractButton>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QButtonGroup;
class QVBoxLayout;
class RailContent;

namespace Motion {
class Value;
}

// One entry of the server rail: a round icon that turns into a rounded square on hover/selection,
// with the white "pill" indicator on the left edge, like Discord.
class ServerButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ServerButton(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setLabel(const QString& label) { m_label = label; update(); }
    // `activeIcon` replaces the icon on hover/selection (e.g. a green "+" that turns white).
    void setIconImage(const QIcon& icon, const QIcon& activeIcon = {});
    void setAccent(const QColor& color) { m_accent = color; update(); }
    void setUnreadState(bool unread, int mentions);

    QSize sizeHint() const override { return {72, 56}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateTargets();

    QImage m_image;
    QString m_label;
    QIcon m_icon;
    QIcon m_activeIcon;
    QColor m_accent;
    bool m_hovered = false;
    bool m_unread = false;
    int m_mentions = 0;
    Motion::Value* m_pill;   // pill height on the left edge
    Motion::Value* m_active; // 0 = round and grey, 1 = rounded square in the accent color
};

// A server folder: closed, a tile with the first four server icons; open, a folder symbol above its servers.
class FolderButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit FolderButton(QWidget* parent = nullptr);

    void setColor(const QColor& color) { m_color = color; update(); }
    void setOpen(bool open);
    // Small pictures of the first servers (null image = initials on a plain circle).
    void setPreviews(const QList<std::pair<QImage, QString>>& previews) { m_previews = previews; update(); }
    // A server of the closed folder is the one on screen.
    void setSelected(bool selected);
    void setUnreadState(bool unread, int mentions);

    QSize sizeHint() const override { return {72, 56}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateTargets();

    QColor m_color;
    QList<std::pair<QImage, QString>> m_previews;
    bool m_open = false;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_unread = false;
    int m_mentions = 0;
    Motion::Value* m_pill;
    Motion::Value* m_openness; // 0 = the tile of icons, 1 = the folder symbol
};

// Vertical server bar on the left: direct messages button, separator, the user's servers and folders, and the
// "Add a Server" button. Servers and folders can be dragged to reorder them; dropping a server on another one
// makes a folder.
class ServerRail : public QWidget
{
    Q_OBJECT

public:
    explicit ServerRail(QWidget* parent = nullptr);

    struct ServerInfo
    {
        QString name;
        QImage icon;
    };
    void setServers(const QList<GuildFolder>& folders, const QHash<QString, ServerInfo>& servers);
    void setServerIcon(const QString& id, const QImage& icon);
    void setServerUnread(const QString& id, bool unread, int mentions);
    void setHomeMentions(int mentions);
    void select(const QString& id); // empty = direct messages

    // What a dragged item would land on, and where the indicator goes.
    struct DropPlan
    {
        bool valid = false;
        GuildFolders::Drop drop;
        QRect line; // insertion line, or
        QRect ring; // the icon it would be combined with
    };

    // A draggable button of the rail and what it stands for.
    struct Slot
    {
        QAbstractButton* button = nullptr;
        QString guildId;  // a server (inside a folder or not)
        QString folderId; // a folder header, or the folder a server is in
        qsizetype entry = -1; // index in the folder list
    };

    // Default color of folders without one (Discord's blurple).
    static QColor defaultFolderColor();

signals:
    void homeSelected();
    void serverSelected(const QString& id);
    void addServerRequested();
    // The user rearranged the list by dragging.
    void foldersChanged(const QList<GuildFolder>& folders);
    void serverContextMenuRequested(const QString& guildId, const QPoint& globalPosition);
    void folderContextMenuRequested(const QString& folderId, const QPoint& globalPosition);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class RailContent;

    void rebuild();
    void toggleFolder(const QString& folderId);
    void refreshFolderBadges();
    QRect slotRect(const QAbstractButton* button) const; // in m_content coordinates
    bool isShown(const Slot& slot) const; // false for servers of closed folders
    void startDrag(QAbstractButton* button);
    DropPlan planDrop(const QString& dragged, int y) const;
    void finishDrop(const QString& dragged, const DropPlan& plan);

    QButtonGroup* m_group;
    ServerButton* m_home;
    ServerButton* m_add;
    RailContent* m_content;
    QVBoxLayout* m_serverLayout;
    QList<GuildFolder> m_folders;
    QHash<QString, ServerInfo> m_servers;
    QHash<QString, std::pair<bool, int>> m_unread; // guild ID -> unread, mentions
    QSet<QString> m_openFolders;
    QString m_selected;
    QHash<QString, ServerButton*> m_buttons;      // by guild ID
    QHash<QString, FolderButton*> m_folderButtons; // by folder ID
    // The servers of each folder, in a box that slides open and closed.
    struct FolderGroup
    {
        QWidget* box = nullptr;
        Motion::Value* openness = nullptr;
    };
    QHash<QString, FolderGroup> m_folderGroups; // by folder ID
    QList<Slot> m_slots; // every draggable button, top to bottom
    QPoint m_pressPosition;
    QPointer<QAbstractButton> m_pressed;
};
