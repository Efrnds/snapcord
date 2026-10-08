#include "core/GuildFolders.h"

#include <QJsonDocument>
#include <QTest>

namespace {

GuildFolder single(const QString& id)
{
    return GuildFolder{{}, {}, -1, {id}};
}

GuildFolder folder(const QString& id, const QStringList& guilds)
{
    return GuildFolder{id, {}, -1, guilds};
}

using Drop = GuildFolders::Drop;

} // namespace

class GuildFoldersTest : public QObject
{
    Q_OBJECT

private slots:
    void readsAndWritesTheSetting()
    {
        const QByteArray json = R"([{"guild_ids":["1","2"],"id":42,"name":"Games","color":16711680},
                                    {"guild_ids":["3"],"id":null,"name":null,"color":null}])";
        const QList<GuildFolder> folders = GuildFolders::fromJson(QJsonDocument::fromJson(json).array());
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders[0].id, QStringLiteral("42"));
        QCOMPARE(folders[0].name, QStringLiteral("Games"));
        QCOMPARE(folders[0].color, 0xFF0000);
        QCOMPARE(folders[0].guildIds, QStringList({"1", "2"}));
        QVERIFY(!folders[1].isFolder());
        QCOMPARE(GuildFolders::fromJson(GuildFolders::toJson(folders)), folders);
    }

    void normalizes()
    {
        // Unknown and repeated guilds go away, empty folders too, and new guilds come first.
        const QList<GuildFolder> folders{folder("9", {"1", "gone"}), folder("8", {"gone"}), single("1"), single("2")};
        const QList<GuildFolder> result = GuildFolders::normalized(folders, {"1", "2", "3"});
        QCOMPARE(result, QList<GuildFolder>({single("3"), folder("9", {"1"}), single("2")}));
        QCOMPARE(GuildFolders::flatten(result), QStringList({"3", "1", "2"}));
    }

    void reordersServers()
    {
        const QList<GuildFolder> folders{single("1"), single("2"), single("3")};
        QCOMPARE(GuildFolders::moveGuild(folders, "3", {Drop::Before, "1", {}}, "f"),
                 QList<GuildFolder>({single("3"), single("1"), single("2")}));
        QCOMPARE(GuildFolders::moveGuild(folders, "1", {Drop::After, "3", {}}, "f"),
                 QList<GuildFolder>({single("2"), single("3"), single("1")}));
        // Dropping on itself changes nothing.
        QCOMPARE(GuildFolders::moveGuild(folders, "2", {Drop::Combine, "2", {}}, "f"), folders);
    }

    void makesFolders()
    {
        const QList<GuildFolder> folders{single("1"), single("2"), single("3")};
        QCOMPARE(GuildFolders::moveGuild(folders, "3", {Drop::Combine, "1", {}}, "f"),
                 QList<GuildFolder>({folder("f", {"1", "3"}), single("2")}));
    }

    void movesIntoAndOutOfFolders()
    {
        const QList<GuildFolder> folders{folder("f", {"1", "2"}), single("3")};
        // Onto the folder: appended.
        QCOMPARE(GuildFolders::moveGuild(folders, "3", {Drop::Combine, {}, "f"}, "n"),
                 QList<GuildFolder>({folder("f", {"1", "2", "3"})}));
        // Next to a server inside the folder: into the folder at that place.
        QCOMPARE(GuildFolders::moveGuild(folders, "3", {Drop::Before, "2", {}}, "n"),
                 QList<GuildFolder>({folder("f", {"1", "3", "2"})}));
        // Out of the folder.
        QCOMPARE(GuildFolders::moveGuild(folders, "1", {Drop::After, "3", {}}, "n"),
                 QList<GuildFolder>({folder("f", {"2"}), single("3"), single("1")}));
        // The last server leaving a folder removes it.
        const QList<GuildFolder> lonely{folder("f", {"1"}), single("2")};
        QCOMPARE(GuildFolders::moveGuild(lonely, "1", {Drop::After, "2", {}}, "n"),
                 QList<GuildFolder>({single("2"), single("1")}));
        // Before the folder header: out of it, above the folder.
        QCOMPARE(GuildFolders::moveGuild(folders, "2", {Drop::Before, {}, "f"}, "n"),
                 QList<GuildFolder>({single("2"), folder("f", {"1"}), single("3")}));
    }

    void movesFolders()
    {
        const QList<GuildFolder> folders{folder("f", {"1", "2"}), single("3"), single("4")};
        QCOMPARE(GuildFolders::moveFolder(folders, "f", {Drop::After, "3", {}}),
                 QList<GuildFolder>({single("3"), folder("f", {"1", "2"}), single("4")}));
        QCOMPARE(GuildFolders::moveFolder(folders, "f", {Drop::After, "4", {}}),
                 QList<GuildFolder>({single("3"), single("4"), folder("f", {"1", "2"})}));
        QCOMPARE(GuildFolders::moveFolder(folders, "f", {Drop::Before, {}, "f"}), folders);
    }

    void findsInviteCodes()
    {
        QCOMPARE(Invites::codeFromText(QStringLiteral("https://discord.gg/abc123")), QStringLiteral("abc123"));
        QCOMPARE(Invites::codeFromText(QStringLiteral("discord.com/invite/Hello-World")), QStringLiteral("Hello-World"));
        QCOMPARE(Invites::codeFromText(QStringLiteral(" abc123 ")), QStringLiteral("abc123"));
        QCOMPARE(Invites::codeFromText(QStringLiteral("https://example.com/invite/abc")), QString());
        QCOMPARE(Invites::codeFromText(QStringLiteral("https://discord.com/channels/1/2")), QString());
        QCOMPARE(Invites::codesInMessage(QStringLiteral("join https://discord.gg/abc and discord.com/invite/xyz, "
                                                        "again discord.gg/abc")),
                 QStringList({"abc", "xyz"}));
    }
};

QTEST_GUILESS_MAIN(GuildFoldersTest)
#include "GuildFoldersTest.moc"
