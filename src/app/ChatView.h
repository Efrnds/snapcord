#pragma once

#include <QHash>
#include <QPlainTextEdit>
#include <QTimer>
#include <QWidget>

class ImageCache;
class MessageDelegate;
class MessageListView;
class MessageModel;
class QLabel;
class QPushButton;
class QToolButton;
class Session;
class VoiceController;

// Message input: Enter sends, Shift+Enter adds a line, Up edits the last message, Escape cancels.
class Composer : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Composer(QWidget* parent = nullptr);

signals:
    void submitted(const QString& text);
    void editLastRequested();
    void cancelRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void adjustHeight();
};

// A text channel or direct message: header, message history and the message composer.
class ChatView : public QWidget
{
    Q_OBJECT

public:
    ChatView(Session* session, ImageCache* images, VoiceController* voice, QWidget* parent = nullptr);

    void showChannel(const QString& guildId, const QString& channelId);
    QString channelId() const { return m_channelId; }
    void refreshHeader();
    // Marks the channel as read if the newest message is on screen and the window is active.
    void markReadIfVisible();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void submit(const QString& text);
    void startReply(const QString& messageId);
    void startEdit(const QString& messageId);
    void cancelMode();
    void editLastMessage();
    void showMessageMenu(const QString& messageId, const QPoint& globalPosition);
    void toggleReaction(const QString& messageId, int reactionIndex);
    void pickReaction(const QString& messageId, const QPoint& globalPosition);
    void openLink(const QString& url);
    void jumpTo(const QString& messageId);
    void updateTyping();
    void showError(const QString& text);

    Session* m_session;
    ImageCache* m_images;
    VoiceController* m_voice;
    MessageModel* m_model;
    MessageDelegate* m_delegate;
    MessageListView* m_list;
    QLabel* m_icon;
    QLabel* m_title;
    QLabel* m_topic;
    QPushButton* m_callButton;
    QWidget* m_modeBar;
    QLabel* m_modeLabel;
    Composer* m_composer;
    QToolButton* m_emojiButton;
    QLabel* m_statusLabel;

    QString m_guildId;
    QString m_channelId;
    QString m_replyTo;
    QString m_editing;
    QHash<QString, qint64> m_typing; // user ID -> when their indicator expires
    QTimer m_typingTimer;
    QTimer m_readTimer;
    int m_scrollBeforePrepend = 0;
    int m_maximumBeforePrepend = 0;
};
