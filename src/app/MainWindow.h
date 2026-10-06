#pragma once

#include "ChannelSidebar.h"

#include <QMainWindow>

class QLabel;
class QListWidget;
class ServerRail;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QWidget* buildChatArea();
    QWidget* buildMemberList();
    void loadDemoData();
    void showServer(const QString& name);
    void showChannel(const QString& name, ChannelSidebar::ChannelKind kind);
    void showSettingsMenu();
    void changeLanguage(const QString& code);

    ServerRail* m_rail;
    ChannelSidebar* m_sidebar;
    QLabel* m_chatIcon = nullptr;
    QLabel* m_chatTitle = nullptr;
    QListWidget* m_members = nullptr;
};
