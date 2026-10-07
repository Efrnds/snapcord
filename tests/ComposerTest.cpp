#include "core/Mentions.h"
#include "core/UploadLimits.h"

#include <QTest>

class ComposerTest : public QObject
{
    Q_OBJECT

private slots:
    void encodesMentions()
    {
        const QList<MentionToken> tokens{{QStringLiteral("@Ana"), QStringLiteral("<@1>")},
                                         {QStringLiteral("@Ana Paula"), QStringLiteral("<@2>")},
                                         {QStringLiteral("#general"), QStringLiteral("<#3>")}};
        QCOMPARE(Mentions::encode(QStringLiteral("hi @Ana, see #general"), tokens), QStringLiteral("hi <@1>, see <#3>"));
        QCOMPARE(Mentions::encode(QStringLiteral("@Ana Paula!"), tokens), QStringLiteral("<@2>!"));
        // Only whole words: a longer name typed by hand stays text.
        QCOMPARE(Mentions::encode(QStringLiteral("@Anastasia"), tokens), QStringLiteral("@Anastasia"));
        QCOMPARE(Mentions::encode(QStringLiteral("@Ana@Ana"), tokens), QStringLiteral("<@1><@1>"));
    }

    void decodesMentions()
    {
        const auto nameOf = [](QChar kind, const QString& id) -> QString {
            if (kind == u'@' && id == u"1")
                return QStringLiteral("@Ana");
            if (kind == u'&')
                return QStringLiteral("@Mods");
            if (kind == u'#')
                return QStringLiteral("#general");
            return {};
        };
        QList<MentionToken> tokens;
        const QString text = Mentions::decode(QStringLiteral("<@!1> <@1> <@&5> <#3> <@9>"), nameOf, &tokens);
        QCOMPARE(text, QStringLiteral("@Ana @Ana @Mods #general <@9>"));
        QCOMPARE(tokens.size(), 3);
        QCOMPARE(Mentions::encode(text, tokens), QStringLiteral("<@!1> <@!1> <@&5> <#3> <@9>"));
    }

    void uploadLimits()
    {
        using namespace UploadLimits;
        QCOMPARE(maxFileSize(0, 0), 10 * MiB);
        QCOMPARE(maxFileSize(3, 0), 50 * MiB);
        QCOMPARE(maxFileSize(1, 0), 50 * MiB);
        QCOMPARE(maxFileSize(2, 0), 500 * MiB);
        QCOMPARE(maxFileSize(0, 2), 50 * MiB);
        QCOMPARE(maxFileSize(0, 3), 100 * MiB);
        QCOMPARE(maxFileSize(2, 3), 500 * MiB);
        QCOMPARE(maxMessageLength(0), 2000);
        QCOMPARE(maxMessageLength(2), 4000);
    }
};

QTEST_GUILESS_MAIN(ComposerTest)
#include "ComposerTest.moc"
