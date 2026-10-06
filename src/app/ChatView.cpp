#include "ChatView.h"

#include "EmojiPicker.h"
#include "ImageCache.h"
#include "MessageView.h"
#include "VoiceController.h"
#include "core/Markdown.h"
#include "core/MessageStore.h"
#include "core/Permissions.h"
#include "core/Session.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr qint64 TypingDurationMs = 10000;
const char* const QuickReactions[] = {"👍", "❤️", "😂", "😮", "😢", "🙏", "🔥", "🎉"};

} // namespace

// --- Composer ---------------------------------------------------------------------------------------

Composer::Composer(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("composer"));
    setTabChangesFocus(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    document()->setDocumentMargin(10);
    connect(this, &QPlainTextEdit::textChanged, this, &Composer::adjustHeight);
    adjustHeight();
}

void Composer::adjustHeight()
{
    // Grow with the text up to about ten lines, then scroll.
    const int lines = std::clamp(static_cast<int>(document()->size().height()), 1, 10);
    setFixedHeight(lines * fontMetrics().lineSpacing() + 22);
}

void Composer::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
        const QString text = toPlainText().trimmed();
        if (!text.isEmpty())
            emit submitted(text);
        return;
    }
    if (event->key() == Qt::Key_Up && toPlainText().isEmpty()) {
        emit editLastRequested();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        emit cancelRequested();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

// --- ChatView ---------------------------------------------------------------------------------------

ChatView::ChatView(Session* session, ImageCache* images, VoiceController* voice, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_images(images)
    , m_voice(voice)
    , m_model(new MessageModel(session->messages(), this))
    , m_delegate(new MessageDelegate(session, images, m_model, this))
    , m_list(new MessageListView(m_delegate))
    , m_icon(new QLabel)
    , m_title(new QLabel)
    , m_topic(new QLabel)
    , m_callButton(new QPushButton)
    , m_modeBar(new QWidget)
    , m_modeLabel(new QLabel)
    , m_composer(new Composer)
    , m_emojiButton(new QToolButton)
    , m_statusLabel(new QLabel)
{
    setObjectName(QStringLiteral("chatArea"));
    setAttribute(Qt::WA_StyledBackground);
    m_list->setModel(m_model);

    // Header.
    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    header->setFixedHeight(48);
    m_icon->setFixedSize(24, 24);
    m_title->setObjectName(QStringLiteral("chatTitle"));
    m_topic->setObjectName(QStringLiteral("chatTopic"));
    m_callButton->setObjectName(QStringLiteral("headerCallButton"));
    m_callButton->setIcon(QIcon(QStringLiteral(":/icons/call.svg")));
    m_callButton->setCursor(Qt::PointingHandCursor);
    connect(m_callButton, &QPushButton::clicked, this, [this] { m_voice->startCall(m_channelId); });
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(m_icon);
    headerLayout->addWidget(m_title);
    headerLayout->addWidget(m_topic, 1);
    headerLayout->addWidget(m_callButton);

    // Reply / edit bar above the composer.
    m_modeBar->setObjectName(QStringLiteral("composerModeBar"));
    m_modeBar->setAttribute(Qt::WA_StyledBackground);
    m_modeLabel->setObjectName(QStringLiteral("composerModeLabel"));
    auto* cancel = new QToolButton;
    cancel->setObjectName(QStringLiteral("composerModeCancel"));
    cancel->setText(QStringLiteral("✕"));
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QToolButton::clicked, this, &ChatView::cancelMode);
    auto* modeLayout = new QHBoxLayout(m_modeBar);
    modeLayout->setContentsMargins(16, 6, 12, 6);
    modeLayout->addWidget(m_modeLabel, 1);
    modeLayout->addWidget(cancel);
    m_modeBar->hide();

    // Composer row.
    m_emojiButton->setObjectName(QStringLiteral("emojiButton"));
    m_emojiButton->setText(QStringLiteral("🙂"));
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    m_emojiButton->setToolTip(tr("Select emoji"));
    connect(m_emojiButton, &QToolButton::clicked, this, [this] {
        auto* picker = new EmojiPicker(m_session, m_images, m_guildId, this);
        connect(picker, &EmojiPicker::picked, this, [this](const QString& text) {
            m_composer->insertPlainText(text);
            m_composer->setFocus();
        });
        picker->popupAt(m_emojiButton->mapToGlobal(QPoint(m_emojiButton->width(), 0)));
    });
    auto* inputBox = new QWidget;
    inputBox->setObjectName(QStringLiteral("composerBox"));
    inputBox->setAttribute(Qt::WA_StyledBackground);
    auto* inputLayout = new QHBoxLayout(inputBox);
    inputLayout->setContentsMargins(4, 0, 8, 0);
    inputLayout->setSpacing(0);
    inputLayout->addWidget(m_composer, 1);
    inputLayout->addWidget(m_emojiButton, 0, Qt::AlignBottom);

    m_statusLabel->setObjectName(QStringLiteral("typingLabel"));
    m_statusLabel->setFixedHeight(22);

    auto* bottom = new QVBoxLayout;
    bottom->setContentsMargins(16, 0, 16, 0);
    bottom->setSpacing(0);
    bottom->addWidget(m_modeBar);
    bottom->addWidget(inputBox);
    bottom->addWidget(m_statusLabel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_list, 1);
    layout->addLayout(bottom);

    // Messages.
    connect(m_composer, &Composer::submitted, this, &ChatView::submit);
    connect(m_composer, &Composer::cancelRequested, this, &ChatView::cancelMode);
    connect(m_composer, &Composer::editLastRequested, this, &ChatView::editLastMessage);
    connect(m_composer, &QPlainTextEdit::textChanged, this, [this] {
        if (m_editing.isEmpty() && !m_composer->toPlainText().isEmpty())
            m_session->sendTyping(m_channelId);
    });
    connect(m_list, &MessageListView::linkActivated, this, &ChatView::openLink);
    connect(m_list, &MessageListView::reactionClicked, this, &ChatView::toggleReaction);
    connect(m_list, &MessageListView::replyClicked, this, &ChatView::jumpTo);
    connect(m_list, &MessageListView::messageContextMenuRequested, this, &ChatView::showMessageMenu);
    connect(m_list, &MessageListView::topReached, this, [this] {
        if (!m_channelId.isEmpty() && m_session->messages()->hasOlder(m_channelId))
            m_session->messages()->loadOlder(m_channelId);
    });

    // Loading older messages must not move what the user is looking at.
    connect(m_model, &MessageModel::aboutToPrepend, this, [this] {
        m_scrollBeforePrepend = m_list->verticalScrollBar()->value();
        m_maximumBeforePrepend = m_list->verticalScrollBar()->maximum();
    });
    connect(m_model, &MessageModel::prepended, this, [this] {
        m_list->doItemsLayout();
        QScrollBar* bar = m_list->verticalScrollBar();
        bar->setValue(m_scrollBeforePrepend + bar->maximum() - m_maximumBeforePrepend);
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        QTimer::singleShot(0, m_list, [this] { m_list->scrollToBottom(); });
        m_readTimer.start();
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first) {
        // A new message from someone ends their typing indicator.
        if (first < m_model->rowCount() && m_typing.remove(m_model->message(first).author.id))
            updateTyping();
        m_readTimer.start();
    });
    connect(m_session->messages(), &MessageStore::sendFailed, this, [this](const QString& channelId, const QString& reason) {
        if (channelId == m_channelId)
            showError(tr("Your message could not be sent: %1").arg(reason));
    });

    connect(m_images, &ImageCache::imageLoaded, this, [this] {
        // Pictures change row heights only for emoji placeholders, which keep their size; a repaint is enough.
        m_delegate->invalidateAll();
        m_list->viewport()->update();
    });

    connect(m_session, &Session::typingStarted, this, [this](const QString& channelId, const QString& userId) {
        if (channelId != m_channelId)
            return;
        m_typing.insert(userId, QDateTime::currentMSecsSinceEpoch() + TypingDurationMs);
        updateTyping();
    });
    m_typingTimer.setInterval(1000);
    connect(&m_typingTimer, &QTimer::timeout, this, &ChatView::updateTyping);

    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(400);
    connect(&m_readTimer, &QTimer::timeout, this, &ChatView::markReadIfVisible);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (m_list->isAtBottom())
            m_readTimer.start();
    });
}

void ChatView::showChannel(const QString& guildId, const QString& channelId)
{
    if (channelId == m_channelId && guildId == m_guildId) {
        refreshHeader();
        return;
    }
    m_guildId = guildId;
    m_channelId = channelId;
    cancelMode();
    m_composer->clear();
    m_typing.clear();
    m_statusLabel->clear();
    m_delegate->invalidateAll();
    m_session->messages()->open(channelId);
    m_model->setChannel(channelId);
    refreshHeader();
    m_composer->setFocus();
}

void ChatView::refreshHeader()
{
    bool canSend = true;
    if (m_guildId.isEmpty()) {
        const PrivateChannel* channel = m_session->privateChannel(m_channelId);
        const QString name = channel ? m_session->privateChannelName(*channel) : QString();
        m_icon->setPixmap(QIcon(QStringLiteral(":/icons/at.svg")).pixmap(QSize(24, 24), devicePixelRatioF()));
        m_title->setText(name);
        m_topic->clear();
        m_composer->setPlaceholderText(tr("Message @%1").arg(name));
        const Call* call = m_session->call(m_channelId);
        const bool callRunning = call && !call->voiceStates.isEmpty();
        m_callButton->setText(callRunning ? tr("Join Call") : tr("Start Call"));
        m_callButton->setVisible(m_voice->channelId() != m_channelId);
    } else {
        const Channel* channel = m_session->channel(m_guildId, m_channelId);
        const QString name = channel ? channel->name : QString();
        m_icon->setPixmap(QIcon(QStringLiteral(":/icons/hash.svg")).pixmap(QSize(24, 24), devicePixelRatioF()));
        m_title->setText(name);
        m_topic->setText(channel ? m_topic->fontMetrics().elidedText(channel->topic.simplified(), Qt::ElideRight, 500) : QString());
        m_topic->setToolTip(channel ? channel->topic : QString());
        m_composer->setPlaceholderText(tr("Message #%1").arg(name));
        m_callButton->hide();
        const Guild* guild = m_session->guild(m_guildId);
        if (guild && channel)
            canSend = Permissions::compute(*guild, *channel, m_session->self().id) & Permissions::SendMessages;
    }
    m_composer->setReadOnly(!canSend);
    m_emojiButton->setEnabled(canSend);
    if (!canSend)
        m_composer->setPlaceholderText(tr("You do not have permission to send messages in this channel."));
}

void ChatView::markReadIfVisible()
{
    if (m_channelId.isEmpty() || !isVisible() || !window()->isActiveWindow() || !m_list->isAtBottom())
        return;
    m_session->markRead(m_guildId, m_channelId);
}

void ChatView::submit(const QString& text)
{
    if (!m_editing.isEmpty()) {
        m_session->messages()->edit(m_channelId, m_editing, text);
    } else {
        m_session->messages()->send(m_channelId, m_guildId, text, m_replyTo);
        QTimer::singleShot(0, m_list, [this] { m_list->scrollToBottom(); });
    }
    m_composer->clear();
    cancelMode();
}

void ChatView::startReply(const QString& messageId)
{
    const Message* message = m_session->messages()->message(m_channelId, messageId);
    if (!message)
        return;
    m_editing.clear();
    m_replyTo = messageId;
    m_modeLabel->setText(tr("Replying to <b>%1</b>").arg(message->author.displayName().toHtmlEscaped()));
    m_modeBar->show();
    m_composer->setFocus();
}

void ChatView::startEdit(const QString& messageId)
{
    const Message* message = m_session->messages()->message(m_channelId, messageId);
    if (!message)
        return;
    m_replyTo.clear();
    m_editing = messageId;
    m_modeLabel->setText(tr("Editing message — <b>Escape</b> to cancel, <b>Enter</b> to save"));
    m_modeBar->show();
    m_composer->setPlainText(message->content);
    m_composer->moveCursor(QTextCursor::End);
    m_composer->setFocus();
}

void ChatView::cancelMode()
{
    if (!m_editing.isEmpty())
        m_composer->clear();
    m_replyTo.clear();
    m_editing.clear();
    m_modeBar->hide();
}

void ChatView::editLastMessage()
{
    const auto& messages = m_session->messages()->messages(m_channelId);
    for (qsizetype i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].author.id == m_session->self().id && !messages[i].isSystemMessage() && !messages[i].pending) {
            startEdit(messages[i].id);
            return;
        }
    }
}

void ChatView::showMessageMenu(const QString& messageId, const QPoint& globalPosition)
{
    const Message* message = m_session->messages()->message(m_channelId, messageId);
    if (!message || message->pending)
        return;
    const bool own = message->author.id == m_session->self().id;

    QMenu menu(this);
    QMenu* reactions = menu.addMenu(tr("Add Reaction"));
    for (const char* quick : QuickReactions) {
        const QString emoji = QString::fromUtf8(quick);
        reactions->addAction(emoji, this, [this, messageId, emoji] {
            Emoji reaction;
            reaction.name = emoji;
            m_session->messages()->setReaction(m_channelId, messageId, reaction, true);
        });
    }
    reactions->addSeparator();
    reactions->addAction(tr("Other…"), this, [this, messageId, globalPosition] { pickReaction(messageId, globalPosition); });

    if (!m_composer->isReadOnly())
        menu.addAction(tr("Reply"), this, [this, messageId] { startReply(messageId); });
    if (own && !message->isSystemMessage())
        menu.addAction(tr("Edit Message"), this, [this, messageId] { startEdit(messageId); });
    menu.addSeparator();
    if (!message->content.isEmpty())
        menu.addAction(tr("Copy Text"), this, [content = message->content] { QApplication::clipboard()->setText(content); });
    menu.addAction(tr("Copy Message Link"), this, [this, messageId] {
        QApplication::clipboard()->setText(QStringLiteral("https://discord.com/channels/%1/%2/%3")
                                               .arg(m_guildId.isEmpty() ? QStringLiteral("@me") : m_guildId, m_channelId, messageId));
    });
    menu.addAction(tr("Copy Message ID"), this, [messageId] { QApplication::clipboard()->setText(messageId); });
    if (own) {
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Delete Message"), this, [this, messageId] {
            const auto answer = QMessageBox::question(this, tr("Delete Message"),
                                                      tr("Are you sure you want to delete this message?"));
            if (answer == QMessageBox::Yes)
                m_session->messages()->remove(m_channelId, messageId);
        });
        remove->setObjectName(QStringLiteral("dangerAction"));
    }
    menu.exec(globalPosition);
}

void ChatView::pickReaction(const QString& messageId, const QPoint& globalPosition)
{
    auto* picker = new EmojiPicker(m_session, m_images, m_guildId, this);
    connect(picker, &EmojiPicker::picked, this, [this, messageId](const QString&, const Emoji& emoji) {
        m_session->messages()->setReaction(m_channelId, messageId, emoji, true);
    });
    picker->popupAt(globalPosition + QPoint(picker->width(), picker->height()));
}

void ChatView::toggleReaction(const QString& messageId, int reactionIndex)
{
    const Message* message = m_session->messages()->message(m_channelId, messageId);
    if (!message || reactionIndex < 0 || reactionIndex >= message->reactions.size())
        return;
    const Reaction& reaction = message->reactions[reactionIndex];
    m_session->messages()->setReaction(m_channelId, messageId, reaction.emoji, !reaction.me);
}

void ChatView::openLink(const QString& url)
{
    if (url.startsWith(u"http://") || url.startsWith(u"https://"))
        QDesktopServices::openUrl(QUrl(url));
}

void ChatView::jumpTo(const QString& messageId)
{
    const int row = m_model->rowOf(messageId);
    if (row >= 0)
        m_list->scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
}

void ChatView::updateTyping()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList names;
    for (auto it = m_typing.begin(); it != m_typing.end();) {
        if (it.value() < now) {
            it = m_typing.erase(it);
        } else {
            names.append(m_session->user(it.key()).displayName());
            ++it;
        }
    }
    names.removeAll(QString());
    if (names.isEmpty()) {
        m_typingTimer.stop();
        m_statusLabel->clear();
        return;
    }
    m_typingTimer.start();
    m_statusLabel->setProperty("error", false);
    m_statusLabel->setStyleSheet(QString());
    QString text;
    if (names.size() == 1)
        text = tr("<b>%1</b> is typing…").arg(names[0].toHtmlEscaped());
    else if (names.size() == 2)
        text = tr("<b>%1</b> and <b>%2</b> are typing…").arg(names[0].toHtmlEscaped(), names[1].toHtmlEscaped());
    else
        text = tr("Several people are typing…");
    m_statusLabel->setText(text);
}

void ChatView::showError(const QString& text)
{
    m_statusLabel->setStyleSheet(QStringLiteral("color:#f23f43;"));
    m_statusLabel->setText(text.toHtmlEscaped());
    QTimer::singleShot(6000, this, [this] {
        if (m_typing.isEmpty()) {
            m_statusLabel->setStyleSheet(QString());
            m_statusLabel->clear();
        }
    });
}
