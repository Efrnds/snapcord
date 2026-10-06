#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// "Voice Connected" panel shown above the user panel while in a call, with the disconnect button.
class VoicePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VoicePanel(QWidget* parent = nullptr);

    enum class Status { Connecting, Connected };
    void setStatus(Status status);
    void setLocation(const QString& channelName, const QString& guildName);
    void setPing(int milliseconds);

signals:
    void disconnectRequested();

private:
    QLabel* m_status;
    QLabel* m_location;
    QToolButton* m_disconnect;
};
