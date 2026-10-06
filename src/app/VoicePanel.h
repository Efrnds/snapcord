#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

// "Voice Connected" panel shown above the user panel while in a call, with the disconnect button.
// Clicking the status opens the connection details.
class VoicePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VoicePanel(QWidget* parent = nullptr);

    enum class Status { Connecting, Connected };
    void setStatus(Status status);
    void setLocation(const QString& channelName, const QString& placeName);
    void setPing(int milliseconds);

signals:
    void disconnectRequested();
    void detailsRequested();

private:
    QPushButton* m_status;
    QLabel* m_location;
    QToolButton* m_disconnect;
};
