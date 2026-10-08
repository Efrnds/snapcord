#include "app/AttachmentTray.h"
#include "app/ChatView.h"
#include "app/ImageCache.h"
#include "app/MentionPopup.h"
#include "app/MessageView.h"
#include "app/VoiceController.h"
#include "core/Session.h"

#include <QAction>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QTest>

namespace {

const QString First = QStringLiteral("10");
const QString Second = QStringLiteral("20");

struct Conversation
{
    Session session;
    ImageCache images;
    VoiceController voice{&session};
    ChatView view{&session, &images, &voice};
    Composer* composer = view.findChild<Composer*>();

    Conversation()
    {
        const QJsonObject self{{QStringLiteral("id"), QStringLiteral("99")},
                               {QStringLiteral("username"), QStringLiteral("self")}};
        const QJsonObject recipient{{QStringLiteral("id"), QStringLiteral("1")},
                                    {QStringLiteral("username"), QStringLiteral("Ana")}};
        QJsonArray channels;
        for (const QString& id : {First, Second}) {
            channels.append(QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("type"), 1},
                                         {QStringLiteral("recipients"), QJsonArray{recipient}}});
        }
        session.startOffline({{QStringLiteral("READY"),
                               QJsonObject{{QStringLiteral("user"), self},
                                           {QStringLiteral("private_channels"), channels}}}});
        for (const QString& id : {First, Second}) {
            Message message;
            message.id = QStringLiteral("100");
            message.channelId = id;
            message.author = session.self();
            message.content = QStringLiteral("original");
            session.messages()->preload(id, {message});
        }
        view.showChannel(QString(), First);
    }

    void show(const QString& id) { view.showChannel(QString(), id); }
};

} // namespace

class ChatDraftTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Tests must not read or write the real account's settings.
        QCoreApplication::setOrganizationName(QStringLiteral("Snapcord Tests"));
        QCoreApplication::setApplicationName(QStringLiteral("Chat Draft Tests"));
        ImageCache::setOfflineSource([](const QUrl&) { return QImage(); });
    }

    void keepsIndependentDraftsAndSelection()
    {
        Conversation chat;
        chat.composer->setPlainText(QStringLiteral("first draft\nsecond line"));
        const int firstHeight = chat.composer->height();
        QTextCursor cursor = chat.composer->textCursor();
        cursor.setPosition(8);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        chat.composer->setTextCursor(cursor);
        chat.show(Second);
        QCOMPARE(chat.composer->toPlainText(), QString());
        chat.composer->setPlainText(QStringLiteral("second draft"));
        chat.show(First);
        QCOMPARE(chat.composer->toPlainText(), QStringLiteral("first draft\nsecond line"));
        QCOMPARE(chat.composer->textCursor().anchor(), 8);
        QCOMPARE(chat.composer->textCursor().position(), 2);
        QCOMPARE(chat.composer->height(), firstHeight);
        chat.show(Second);
        QCOMPARE(chat.composer->toPlainText(), QStringLiteral("second draft"));
    }

    void clearedDraftDoesNotReturn()
    {
        Conversation chat;
        chat.composer->setPlainText(QStringLiteral("discard me"));
        chat.show(Second);
        chat.show(First);
        chat.composer->clear();
        chat.show(Second);
        chat.show(First);
        QVERIFY(chat.composer->toPlainText().isEmpty());
    }

    void keepsMentionsAndClearsSentDraft()
    {
        Conversation chat;
        chat.composer->setPlainText(QStringLiteral("@Ana"));
        chat.composer->moveCursor(QTextCursor::End);
        MentionSuggestion suggestion;
        suggestion.display = QStringLiteral("@Ana");
        suggestion.raw = QStringLiteral("<@1>");
        chat.view.findChild<MentionPopup*>()->picked(suggestion);
        const QString text = chat.composer->toPlainText();
        chat.show(Second);
        chat.show(First);
        QCOMPARE(chat.composer->toPlainText(), text);
        chat.composer->submitted(text);
        const auto& messages = chat.session.messages()->messages(First);
        QCOMPARE(messages.last().content, QStringLiteral("<@1> "));
        QVERIFY(messages.last().pending);
        chat.show(Second);
        chat.show(First);
        QVERIFY(chat.composer->toPlainText().isEmpty());
    }

    void keepsPastedAttachments()
    {
        Conversation chat;
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::red);
        chat.composer->imagePasted(image);
        auto* tray = chat.view.findChild<AttachmentTray*>();
        QVERIFY(!tray->isHidden());
        chat.show(Second);
        QVERIFY(tray->isHidden());
        chat.show(First);
        QVERIFY(!tray->isHidden());
        chat.composer->submitted(QString());
        const auto& message = chat.session.messages()->messages(First).last();
        QCOMPARE(message.attachments.size(), 1);
        QCOMPARE(message.attachments.first().filename, QStringLiteral("image.png"));
        QVERIFY(tray->isHidden());
        chat.show(Second);
        chat.show(First);
        QVERIFY(tray->isHidden());
    }

    void keepsEditModeAndCancellation()
    {
        Conversation chat;
        chat.composer->editLastRequested();
        QCOMPARE(chat.composer->toPlainText(), QStringLiteral("original"));
        chat.composer->setPlainText(QStringLiteral("edited draft"));
        auto* bar = chat.view.findChild<QWidget*>(QStringLiteral("composerModeBar"));
        chat.show(Second);
        QVERIFY(bar->isHidden());
        chat.show(First);
        QCOMPARE(chat.composer->toPlainText(), QStringLiteral("edited draft"));
        QVERIFY(!bar->isHidden());
        QVERIFY(chat.view.findChild<QLabel*>(QStringLiteral("composerModeLabel"))->text().contains(u"Editing"));
        chat.composer->cancelRequested();
        QVERIFY(chat.composer->toPlainText().isEmpty());
        QVERIFY(bar->isHidden());
        chat.show(Second);
        chat.show(First);
        QVERIFY(chat.composer->toPlainText().isEmpty());
        QVERIFY(bar->isHidden());
    }

    void keepsReplyTarget()
    {
        Conversation chat;
        bool replied = false;
        // Exercise the real message menu rather than reaching into ChatView's private state.
        QTimer::singleShot(0, &chat.view, [&] {
            auto* menu = chat.view.findChild<QMenu*>();
            if (!menu)
                return;
            for (QAction* action : menu->actions()) {
                if (action->text() == u"Reply") {
                    action->trigger();
                    replied = true;
                    break;
                }
            }
            menu->close();
        });
        chat.view.findChild<MessageListView*>()->messageContextMenuRequested(QStringLiteral("100"), QPoint());
        QVERIFY(replied);
        chat.composer->setPlainText(QStringLiteral("reply draft"));
        chat.show(Second);
        chat.show(First);
        chat.composer->submitted(chat.composer->toPlainText());
        const auto& message = chat.session.messages()->messages(First).last();
        QCOMPARE(message.referencedMessageId, QStringLiteral("100"));
        QCOMPARE(message.content, QStringLiteral("reply draft"));
    }
};

QTEST_MAIN(ChatDraftTest)
#include "ChatDraftTest.moc"
