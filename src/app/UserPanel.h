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

    void setUser(const QString& displayName, const QString& status);

    bool isMuted() const { return m_muted; }
    bool isDeafened() const { return m_deafened; }

signals:
    void muteChanged(bool muted);
    void deafenChanged(bool deafened);
    void settingsRequested();

private:
    void toggleMute();
    void toggleDeafen();
    void setState(bool muted, bool deafened);
    void updateButtons();

    QLabel* m_avatar;
    QLabel* m_name;
    QLabel* m_status;
    QToolButton* m_mute;
    QToolButton* m_deafen;
    QToolButton* m_settings;
    bool m_muted = false;
    bool m_deafened = false;
    bool m_mutedBeforeDeafen = false;
};
