#pragma once

#include <QWidget>

class QButtonGroup;
class QVBoxLayout;

// Vertical server bar on the left: direct messages button, separator, servers and "add server" button.
class ServerRail : public QWidget
{
    Q_OBJECT

public:
    explicit ServerRail(QWidget* parent = nullptr);

    void addServer(const QString& id, const QString& name);

signals:
    void homeSelected();
    void serverSelected(const QString& id);

private:
    QButtonGroup* m_group;
    QVBoxLayout* m_serverLayout;
};
