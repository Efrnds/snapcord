#pragma once

#include <QAbstractButton>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QWidget>

class QButtonGroup;
class QVBoxLayout;

// One entry of the server rail: a round icon that turns into a rounded square on hover/selection,
// with the white "pill" indicator on the left edge, like Discord.
class ServerButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ServerButton(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setLabel(const QString& label) { m_label = label; update(); }
    void setIconImage(const QIcon& icon) { m_icon = icon; update(); }
    void setAccent(const QColor& color) { m_accent = color; update(); }
    void setUnreadState(bool unread, int mentions);

    QSize sizeHint() const override { return {72, 56}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QImage m_image;
    QString m_label;
    QIcon m_icon;
    QColor m_accent;
    bool m_hovered = false;
    bool m_unread = false;
    int m_mentions = 0;
};

// Vertical server bar on the left: direct messages button, separator and the user's servers.
class ServerRail : public QWidget
{
    Q_OBJECT

public:
    explicit ServerRail(QWidget* parent = nullptr);

    void clearServers();
    void addServer(const QString& id, const QString& name, const QImage& icon);
    void setServerIcon(const QString& id, const QImage& icon);
    void setServerUnread(const QString& id, bool unread, int mentions);
    void setHomeMentions(int mentions);
    void select(const QString& id); // empty = direct messages

signals:
    void homeSelected();
    void serverSelected(const QString& id);

private:
    QButtonGroup* m_group;
    ServerButton* m_home;
    QVBoxLayout* m_serverLayout;
    QHash<QString, ServerButton*> m_buttons;
};
