#pragma once

#include "core/Mentions.h"
#include "core/MessageStore.h"

#include <QHash>
#include <QPlainTextEdit>
#include <QTimer>
#include <QWidget>

class AttachmentTray;
class ImageCache;
class MemberListView;
class MentionPopup;
struct MentionSuggestion;
class MessageDelegate;
class MessageListView;
class MessageModel;
class QLabel;
class QPushButton;
class QToolButton;
class Session;
class VoiceController;

// Message input: Enter sends, Shift+Enter adds a line, Up edits the last message, Escape cancels.
// Pasted or dropped files and pictures become attachments instead of text.
class Composer : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Composer(QWidget* parent = nullptr);

    // While the mention list is open, the navigation keys go to it.
    void setMentionPopup(MentionPopup* popup) { m_popup = popup; }

signals:
    void submitted(const QString& text);
    void editLastRequested();
    void cancelRequested();
    void filesPasted(const QStringList& paths);
    void imagePasted(const QImage& image);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    void adjustHeight();

    MentionPopup* m_popup = nullptr;
};

// A text channel or direct message: header, message history and the message composer.
class ChatView : public QWidget
{
    Q_OBJECT

public:
    ChatView(Session* session, ImageCache* images, VoiceController* voice, QWidget* parent = nullptr);
    ~ChatView() override;

    void showChannel(const QString& guildId, const QString& channelId);
    QString channelId() const { return m_channelId; }
    void refreshHeader();
    // Marks the channel as read if the newest message is on screen and the window is active.
    void markReadIfVisible();

signals:
    // A user's name, avatar or mention was clicked. `guildId` is empty in direct messages.
    void profileRequested(const QString& userId, const QString& guildId, const QPoint& globalPosition);
    // From the member sidebar, whose profiles open to its left.
    void memberProfileRequested(const QString& userId, const QString& guildId, const QPoint& globalPosition);
    void memberContextMenuRequested(const QString& userId, const QPoint& globalPosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void saveDraft();
    void restoreDraft();
    void submit(const QString& text);
    void chooseFiles();
    void addFiles(const QStringList& paths);
    void addPastedImage(const QImage& image);
    // Adds a file if Discord's limits allow it; otherwise shows why not.
    bool addFile(const OutgoingFile& file);
    void updateAttachments();
    void updateMentionPopup();
    QList<MentionSuggestion> userSuggestions(const QString& query) const;
    QList<MentionSuggestion> channelSuggestions(const QString& query) const;
    void insertMention(const MentionSuggestion& suggestion);
    bool canMentionEveryone() const;
    void startReply(const QString& messageId);
    void startEdit(const QString& messageId);
    void cancelMode();
    void editLastMessage();
    void showMessageMenu(const QString& messageId, const QPoint& globalPosition);
    void toggleReaction(const QString& messageId, int reactionIndex);
    void pickReaction(const QString& messageId, const QPoint& globalPosition);
    void openLink(const QString& url);
    void openImage(const QString& url, bool video, bool web);
    void jumpTo(const QString& messageId);
    void updateTyping();
    void showError(const QString& text);
    void updateMemberList();

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
    QToolButton* m_membersButton;
    MemberListView* m_memberList;
    QWidget* m_modeBar;
    QLabel* m_modeLabel;
    Composer* m_composer;
    QToolButton* m_emojiButton;
    QToolButton* m_attachButton;
    AttachmentTray* m_tray;
    MentionPopup* m_mentionPopup;
    QWidget* m_inputBox = nullptr;
    QLabel* m_statusLabel;

    QString m_guildId;
    QString m_channelId;
    QString m_replyTo;
    QString m_editing;
    bool m_canAttach = true;
    QList<OutgoingFile> m_files;          // attachments of the next message
    QList<MentionToken> m_mentionTokens;  // mentions picked in the composer
    // Session-local drafts keep private text and pasted images off disk. Channel IDs are globally unique.
    struct Draft
    {
        QString text;
        QList<MentionToken> mentions;
        QList<OutgoingFile> files;
        QString replyTo;
        QString editing;
        QString modeLabel;
        int cursor = 0;
        int anchor = 0;
    };
    QHash<QString, Draft> m_drafts;
    bool m_restoringDraft = false;
    int m_mentionStart = -1;              // where the "@..." / "#..." being completed starts
    QString m_memberQuery;                // last name searched on the server
    QTimer m_memberSearchTimer;
    QHash<QString, qint64> m_typing; // user ID -> when their indicator expires
    QTimer m_typingTimer;
    QTimer m_readTimer;
    int m_scrollBeforePrepend = 0;
    int m_maximumBeforePrepend = 0;
};
