#pragma once

#include <QWidget>

class QLabel;

// Small always-on-top window shown while someone is calling, with Accept and Decline buttons.
class IncomingCallWindow : public QWidget
{
    Q_OBJECT

public:
    explicit IncomingCallWindow(QWidget* parent = nullptr);

    void setCaller(const QString& channelId, const QString& name, const QPixmap& avatar);
    QString channelId() const { return m_channelId; }

    // Bottom-right corner of the primary screen, like a notification.
    void showAtCorner();

signals:
    void accepted(const QString& channelId);
    void declined(const QString& channelId);

private:
    QString m_channelId;
    QLabel* m_avatar;
    QLabel* m_name;
};
