#pragma once

#include "core/Message.h"

#include <QAbstractListModel>
#include <QCache>
#include <QHash>
#include <QListView>
#include <QPixmap>
#include <QSet>
#include <QStyledItemDelegate>

#include <memory>

class ImageCache;
class MessageStore;
class QTextDocument;
class Session;

// Rows of one channel's messages, read straight from the MessageStore.
class MessageModel : public QAbstractListModel
{
    Q_OBJECT

public:
    MessageModel(MessageStore* store, QObject* parent = nullptr);

    void setChannel(const QString& channelId);
    QString channelId() const { return m_channelId; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    const Message& message(int row) const;
    int rowOf(const QString& messageId) const;
    // Consecutive messages from the same author within a few minutes are shown as one group.
    bool startsGroup(int row) const;
    bool startsDay(int row) const;

signals:
    void aboutToPrepend();
    void prepended();
    void messageChanged(const QString& messageId);

private:
    MessageStore* m_store;
    QString m_channelId;
};

// Paints messages like Discord and answers "what is under the mouse" for clicks.
class MessageDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    struct Hit
    {
        enum Kind { None, Link, Spoiler, Image, File, Reaction, Reply, Author, Invite } kind = None;
        QString url;
        QString messageId;
        int reactionIndex = -1;
        bool video = false; // Discord mp4/webm — native player
        bool web = false;   // YouTube etc. — in-app WebEngine embed
    };

    MessageDelegate(Session* session, ImageCache* images, MessageModel* model, QObject* parent = nullptr);
    ~MessageDelegate() override;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    Hit hitTest(const QModelIndex& index, const QRect& itemRect, const QPoint& position) const;

    void invalidate(const QString& messageId);
    void invalidateAll();
    void revealSpoilers(const QString& messageId);
    // Messages span the whole viewport; its width decides wrapping and therefore row heights.
    void setViewWidth(int width) { m_viewWidth = width; }

private:
    struct Layout;
    Layout& layout(const QModelIndex& index, int width) const;
    QString systemText(const Message& message) const;
    QPixmap avatar(const User& user, int size) const;
    void paintInvite(QPainter* painter, const QFont& base, const QRect& box, const QRect& button, const QString& code) const;

    Session* m_session;
    ImageCache* m_images;
    MessageModel* m_model;
    mutable QCache<QString, Layout> m_layouts;
    mutable QHash<QString, QPixmap> m_avatars; // by user ID + size
    QSet<QString> m_revealedSpoilers;
    int m_viewWidth = 600;
    mutable QSet<QString> m_requestedInvites; // looked up once per code, even when it fails
};

// The scrolling list: keeps the view pinned to the newest message, loads history at the top and
// turns clicks into actions.
class MessageListView : public QListView
{
    Q_OBJECT

public:
    MessageListView(MessageDelegate* delegate, QWidget* parent = nullptr);

    bool isAtBottom() const;

signals:
    void linkActivated(const QString& url);
    void imageActivated(const QString& url, bool video, bool web);
    void reactionClicked(const QString& messageId, int reactionIndex);
    void replyClicked(const QString& messageId);
    // The "Join" button of an invite card.
    void inviteClicked(const QString& code, const QString& messageId);
    void userClicked(const QString& userId, const QPoint& globalPosition);
    void messageContextMenuRequested(const QString& messageId, const QPoint& globalPosition);
    void topReached();

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void rowsInserted(const QModelIndex& parent, int start, int end) override;

private:
    MessageDelegate::Hit hitAt(const QPoint& position) const;

    MessageDelegate* m_delegate;
};
