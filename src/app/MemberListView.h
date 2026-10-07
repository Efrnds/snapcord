#pragma once

#include <QListView>
#include <QTimer>

class ImageCache;
class MemberListModel;
class Session;

// Member sidebar on the right of a guild text channel, like Discord's: members grouped by hoisted role,
// then online and offline, with their status and what they are doing. Rows arrive from the Gateway in
// chunks as the list scrolls; rows not received yet are drawn as placeholders.
class MemberListView : public QListView
{
    Q_OBJECT

public:
    MemberListView(Session* session, ImageCache* images, QWidget* parent = nullptr);

    // Subscribes to the channel's list (again if the Gateway session changed) and shows it.
    void setChannel(const QString& guildId, const QString& channelId);

signals:
    void memberClicked(const QString& userId, const QPoint& globalPosition);
    void memberContextMenuRequested(const QString& userId, const QPoint& globalPosition);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void subscribe();
    void refresh();
    QString userAt(const QPoint& position) const;

    Session* m_session;
    MemberListModel* m_model;
    QString m_guildId;
    QString m_channelId;
    QTimer m_refreshTimer;
    QTimer m_subscribeTimer;
};
