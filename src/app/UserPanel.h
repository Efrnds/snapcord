#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// User panel at the bottom left: avatar, name and the mute, deafen and settings buttons.
class UserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit UserPanel(QWidget* parent = nullptr);

    void setUser(const QString& displayName, const QString& status, const QPixmap& avatar);
    void setVoiceState(bool muted, bool deafened);

signals:
    void muteClicked();
    void deafenClicked();
    void settingsRequested();
    // The avatar and name were clicked: show the user's own profile.
    void profileRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* m_profileArea;
    QLabel* m_avatar;
    QLabel* m_name;
    QLabel* m_status;
    QToolButton* m_mute;
    QToolButton* m_deafen;
    QToolButton* m_settings;
};
