#include "core/Markdown.h"

#include <QTest>

class MarkdownTest : public QObject
{
    Q_OBJECT

private:
    Markdown::Context context()
    {
        Markdown::Context context;
        context.userName = [](const QString& id) { return id == u"1" ? QStringLiteral("Ana") : QString(); };
        context.channelName = [](const QString&) { return QStringLiteral("general"); };
        context.roleName = [](const QString&) { return QStringLiteral("Mods"); };
        context.selfUserId = QStringLiteral("2");
        return context;
    }

private slots:
    void escapesHtml()
    {
        const QString html = Markdown::toHtml(QStringLiteral("<b>hi</b> & bye"), context()).html;
        QVERIFY(html.contains(u"&lt;b&gt;hi&lt;/b&gt; &amp; bye"));
        QVERIFY(!html.contains(u"<b>hi"));
    }

    void formatsInline()
    {
        const QString html = Markdown::toHtml(QStringLiteral("**bold** *it* __under__ ~~gone~~ snake_case_name"), context()).html;
        QVERIFY(html.contains(u"<b>bold</b>"));
        QVERIFY(html.contains(u"<i>it</i>"));
        QVERIFY(html.contains(u"<u>under</u>"));
        QVERIFY(html.contains(u"<s>gone</s>"));
        QVERIFY2(html.contains(u"snake_case_name"), "underscores inside words are not italics");
    }

    void codeIsVerbatim()
    {
        const QString html = Markdown::toHtml(QStringLiteral("`**not bold**` and ```\n<@1> *x*\n```"), context()).html;
        QVERIFY(html.contains(u"**not bold**"));
        QVERIFY(html.contains(u"&lt;@1&gt; *x*"));
        QVERIFY(!html.contains(u"<b>not bold"));
    }

    void resolvesMentions()
    {
        const QString html = Markdown::toHtml(QStringLiteral("hi <@1>, <@2> in <#9> for <@&5> @everyone"), context()).html;
        QVERIFY(html.contains(u"@Ana"));
        QVERIFY(html.contains(u"#general"));
        QVERIFY(html.contains(u"@Mods"));
        QVERIFY(html.contains(u"@everyone"));
        QVERIFY2(html.contains(u"#5865f2"), "mentions of the current user are highlighted");
    }

    void linksAndEmoji()
    {
        const auto result = Markdown::toHtml(QStringLiteral("see [docs](https://a.b/c?x=1&y=2) or https://example.com/p. <:pog:123>"),
                                             context());
        QVERIFY(result.html.contains(u"href=\"https://a.b/c?x=1&amp;y=2\""));
        QVERIFY(result.html.contains(u">docs</a>"));
        QVERIFY(result.html.contains(u"href=\"https://example.com/p\""));
        QCOMPARE(result.images.size(), 1);
        QVERIFY(result.images.first().toString().contains(u"/emojis/123.png"));
        QVERIFY(!result.jumbo);
    }

    void jumboEmoji()
    {
        QVERIFY(Markdown::toHtml(QStringLiteral("<:pog:123> <a:dance:456>"), context()).jumbo);
        QVERIFY(Markdown::toHtml(QStringLiteral("😀🎉"), context()).jumbo);
        QVERIFY(!Markdown::toHtml(QStringLiteral("ok 😀"), context()).jumbo);
    }

    void blocks()
    {
        const QString html = Markdown::toHtml(QStringLiteral("# Title\n> quoted\n- item\nplain"), context()).html;
        QVERIFY(html.contains(u"font-size:22px"));
        QVERIFY(html.contains(u"bgcolor=\"#4e5058\""));
        QVERIFY(html.contains(u"•"));
        QVERIFY(html.contains(u"plain"));
    }

    void spoilers()
    {
        auto ctx = context();
        QVERIFY(Markdown::toHtml(QStringLiteral("||secret||"), ctx).html.contains(u"spoiler:"));
        ctx.revealSpoilers = true;
        QVERIFY(!Markdown::toHtml(QStringLiteral("||secret||"), ctx).html.contains(u"spoiler:"));
    }

    void plainText()
    {
        QCOMPARE(Markdown::toPlainText(QStringLiteral("**hey** <@1> look <:pog:123>"), context()),
                 QStringLiteral("hey @Ana look :pog:"));
    }
};

QTEST_GUILESS_MAIN(MarkdownTest)
#include "MarkdownTest.moc"
