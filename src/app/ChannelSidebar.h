#pragma once

#include <QWidget>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class UserPanel;

// Channel column: server name on top, channels grouped by category and the user panel at the bottom.
class ChannelSidebar : public QWidget
{
    Q_OBJECT

public:
    enum class ChannelKind { Text, Voice };
    Q_ENUM(ChannelKind)

    explicit ChannelSidebar(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void clear();
    void addCategory(const QString& name);
    void addChannel(const QString& id, const QString& name, ChannelKind kind);
    void selectFirstTextChannel();

    UserPanel* userPanel() const { return m_userPanel; }

signals:
    void channelActivated(const QString& id, const QString& name, ChannelSidebar::ChannelKind kind);

private:
    void onItemClicked(QTreeWidgetItem* item);

    QLabel* m_title;
    QTreeWidget* m_tree;
    QTreeWidgetItem* m_currentCategory = nullptr;
    UserPanel* m_userPanel;
};
