#pragma once

#include <QTimer>
#include <QWidget>

class ImageCache;
struct Presence;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class Session;

// The home screen of direct messages: friends who are online, the full list, pending requests,
// and a form to add someone by username.
class FriendsView : public QWidget
{
    Q_OBJECT

public:
    FriendsView(Session* session, ImageCache* images, QWidget* parent = nullptr);

signals:
    void conversationRequested(const QString& userId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class Tab { Online, All, Pending, Add };

    void setTab(Tab tab);
    void rebuild();
    void scheduleRebuild();
    void sendRequest();
    QString statusLine(const Presence& presence) const;

    Session* m_session;
    ImageCache* m_images;
    QButtonGroup* m_tabs;
    QStackedWidget* m_stack;
    QScrollArea* m_scroll;
    QWidget* m_addPage;
    QLineEdit* m_username;
    QLabel* m_addStatus;
    Tab m_tab = Tab::Online;
    QTimer m_rebuildTimer;
};
