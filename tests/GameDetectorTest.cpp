#include "core/GameDetector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

// Reading Discord's detectable applications list and matching process paths against it.
class GameDetectorTest : public QObject
{
    Q_OBJECT

    static QJsonObject executable(const QString& name, const QString& os, bool launcher = false,
                                  const QString& arguments = {})
    {
        QJsonObject json{{QStringLiteral("name"), name}, {QStringLiteral("os"), os},
                         {QStringLiteral("is_launcher"), launcher}};
        if (!arguments.isEmpty())
            json.insert(QStringLiteral("arguments"), arguments);
        return json;
    }

    static QJsonObject application(const QString& id, const QString& name, const QJsonArray& executables)
    {
        return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("executables"), executables}};
    }

#if defined(Q_OS_MACOS)
    const QString Os = QStringLiteral("darwin");
#else
    const QString Os = QStringLiteral("win32");
#endif

    std::shared_ptr<GameDetector::Database> sampleDatabase() const
    {
        const QJsonArray list{
            application(QStringLiteral("1"), QStringLiteral("Overwatch"), {executable(QStringLiteral("Overwatch.exe"), Os)}),
            application(QStringLiteral("2"), QStringLiteral("World of Warcraft"),
                        {executable(QStringLiteral("_retail_/wow.exe"), Os),
                         executable(QStringLiteral("launcher.exe"), Os, true)}),
            application(QStringLiteral("3"), QStringLiteral("Team\tFortress"),
                        {executable(QStringLiteral(">hl2.exe"), Os, false, QStringLiteral("-game tf"))}),
            application(QStringLiteral("4"), QStringLiteral("Elsewhere"),
                        {executable(QStringLiteral("other.exe"), QStringLiteral("nintendo"))}),
        };
        return GameDetector::parseCompactList(GameDetector::compactList(QJsonDocument(list).toJson()));
    }

private slots:
    void readsTheList()
    {
        const auto database = sampleDatabase();
        QVERIFY(database);
        QVERIFY(database->fileNames.contains(QStringLiteral("overwatch.exe")));
        QVERIFY(database->fileNames.contains(QStringLiteral("wow.exe")));
        QVERIFY(database->fileNames.contains(QStringLiteral("hl2.exe")));
        // Launchers and other systems are left out.
        QVERIFY(!database->fileNames.contains(QStringLiteral("launcher.exe")));
        QVERIFY(!database->fileNames.contains(QStringLiteral("other.exe")));
        // Exact names with arguments are kept apart, and tabs in names do not break the format.
        const auto games = database->byArguments.values(QStringLiteral("hl2.exe"));
        QCOMPARE(games.size(), 1);
        QCOMPARE(games.first().arguments, QStringLiteral("-game tf"));
        QCOMPARE(games.first().name, QStringLiteral("Team Fortress"));
    }

    void matchesPathEndings()
    {
        const auto database = sampleDatabase();
        QVERIFY(database);
        const GameDetector::Game* game =
            GameDetector::findByPath(*database, QStringLiteral("c:/games/overwatch/overwatch.exe"));
        QVERIFY(game);
        QCOMPARE(game->applicationId, QStringLiteral("1"));
        game = GameDetector::findByPath(*database, QStringLiteral("d:/blizzard/world of warcraft/_retail_/wow.exe"));
        QVERIFY(game);
        QCOMPARE(game->name, QStringLiteral("World of Warcraft"));
        // The listed path ending must match whole folder names.
        QVERIFY(!GameDetector::findByPath(*database, QStringLiteral("d:/wow/_classic_/wow.exe")));
        QVERIFY(!GameDetector::findByPath(*database, QStringLiteral("d:/x_retail_/wow.exe")));
        QVERIFY(!GameDetector::findByPath(*database, QStringLiteral("c:/tools/notoverwatch.exe")));
        QVERIFY(GameDetector::findByPath(*database, QStringLiteral("overwatch.exe")));
    }

    void rejectsGarbage()
    {
        QVERIFY(!GameDetector::parseCompactList("not a list"));
        QVERIFY(GameDetector::compactList("{}").isEmpty());
    }
};

QTEST_GUILESS_MAIN(GameDetectorTest)
#include "GameDetectorTest.moc"
