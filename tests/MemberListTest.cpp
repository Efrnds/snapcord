#include "core/Session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

// The member sidebar as GUILD_MEMBER_LIST_UPDATE builds it, fed through the Session in offline mode.
class MemberListTest : public QObject
{
    Q_OBJECT

    const QString Guild = QStringLiteral("1");
    const QString Channel = QStringLiteral("2");

    static QJsonObject member(const QString& id)
    {
        const QJsonObject user{{QStringLiteral("id"), id}, {QStringLiteral("username"), QStringLiteral("user") + id}};
        return {{QStringLiteral("member"), QJsonObject{{QStringLiteral("user"), user}}}};
    }

    static QJsonObject group(const QString& id, int count)
    {
        return {{QStringLiteral("group"), QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("count"), count}}}};
    }

    QJsonObject update(const QJsonArray& groups, const QJsonArray& ops) const
    {
        QJsonArray groupList;
        for (const QJsonValue& value : groups)
            groupList.append(value.toObject().value(u"group"));
        return {{QStringLiteral("guild_id"), Guild},
                {QStringLiteral("id"), QStringLiteral("everyone")},
                {QStringLiteral("groups"), groupList},
                {QStringLiteral("ops"), ops}};
    }

    void startSession(Session& session) const
    {
        const QJsonObject guild{{QStringLiteral("id"), Guild},
                                {QStringLiteral("name"), QStringLiteral("Test")},
                                {QStringLiteral("channels"), QJsonArray{QJsonObject{{QStringLiteral("id"), Channel},
                                                                                    {QStringLiteral("type"), 0}}}}};
        const QJsonObject ready{{QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), QStringLiteral("99")}}},
                                {QStringLiteral("guilds"), QJsonArray{guild}}};
        session.startOffline({{QStringLiteral("READY"), ready}});
        session.subscribeMemberList(Guild, Channel);
    }

    static QStringList rows(const MemberList* list)
    {
        QStringList result;
        for (const MemberListItem& item : list->items)
            result.append(item.isGroup() ? u'[' + item.groupId + u']' : item.isMember() ? item.userId : QStringLiteral("?"));
        return result;
    }

private slots:
    void requestsNestedRanges()
    {
        auto json = [](int lastRow) {
            return QString::fromUtf8(QJsonDocument(Session::memberListRanges(lastRow)).toJson(QJsonDocument::Compact));
        };
        QCOMPARE(json(0), QStringLiteral("[[0,99]]"));
        QCOMPARE(json(150), QStringLiteral("[[0,99],[100,199]]"));
        QCOMPARE(json(350), QStringLiteral("[[0,99],[200,299],[300,399]]"));
    }

    void appliesOperations()
    {
        Session session;
        startSession(session);
        QVERIFY(!session.memberList(Guild, Channel));

        const QJsonArray groups{group(QStringLiteral("online"), 2), group(QStringLiteral("offline"), 1)};
        const QJsonObject sync{{QStringLiteral("op"), QStringLiteral("SYNC")},
                               {QStringLiteral("range"), QJsonArray{0, 99}},
                               {QStringLiteral("items"), QJsonArray{groups[0], member("10"), member("11"), groups[1], member("12")}}};
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), update(groups, {sync})}});
        const MemberList* list = session.memberList(Guild, Channel);
        QVERIFY(list);
        QCOMPARE(rows(list), (QStringList{"[online]", "10", "11", "[offline]", "12"}));

        // User 12 comes online: removed from "offline" (now empty, so its header goes too) and inserted.
        const QJsonArray after{group(QStringLiteral("online"), 3)};
        const QJsonArray ops{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("DELETE")}, {QStringLiteral("index"), 4}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("DELETE")}, {QStringLiteral("index"), 3}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("INSERT")}, {QStringLiteral("index"), 1}, {QStringLiteral("item"), member("12")}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("UPDATE")}, {QStringLiteral("index"), 0}, {QStringLiteral("item"), after[0]}},
        };
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), update(after, ops)}});
        QCOMPARE(rows(list), (QStringList{"[online]", "12", "10", "11"}));
        QCOMPARE(list->items.first().groupCount, 3);

        // Invalidated rows become placeholders until synced again.
        const QJsonObject invalidate{{QStringLiteral("op"), QStringLiteral("INVALIDATE")}, {QStringLiteral("range"), QJsonArray{2, 3}}};
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), update(after, {invalidate})}});
        QCOMPARE(rows(list), (QStringList{"[online]", "12", "?", "?"}));
    }

    void ignoresOtherGuilds()
    {
        Session session;
        startSession(session);
        QJsonObject other = update({group(QStringLiteral("online"), 1)},
                                   {QJsonObject{{QStringLiteral("op"), QStringLiteral("SYNC")},
                                                {QStringLiteral("range"), QJsonArray{0, 99}},
                                                {QStringLiteral("items"), QJsonArray{group(QStringLiteral("online"), 1), member("10")}}}});
        other.insert(QStringLiteral("guild_id"), QStringLiteral("5"));
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), other}});
        QVERIFY(!session.memberList(Guild, Channel));
    }

    void keepsListsOfGuildsLeft()
    {
        // The Gateway does not send the list again when the user comes back to a guild.
        Session session;
        startSession(session);
        const QJsonArray groups{group(QStringLiteral("online"), 1)};
        const QJsonObject sync{{QStringLiteral("op"), QStringLiteral("SYNC")},
                               {QStringLiteral("range"), QJsonArray{0, 99}},
                               {QStringLiteral("items"), QJsonArray{groups[0], member("10")}}};
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), update(groups, {sync})}});
        QVERIFY(session.memberList(Guild, Channel));

        session.subscribeMemberList(QStringLiteral("5"), QStringLiteral("6"));
        // Updates keep coming while the user is away.
        const QJsonArray after{group(QStringLiteral("online"), 2)};
        const QJsonArray ops{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("INSERT")}, {QStringLiteral("index"), 2}, {QStringLiteral("item"), member("11")}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("UPDATE")}, {QStringLiteral("index"), 0}, {QStringLiteral("item"), after[0]}},
        };
        session.startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), update(after, ops)}});

        session.subscribeMemberList(Guild, Channel);
        const MemberList* list = session.memberList(Guild, Channel);
        QVERIFY(list);
        QCOMPARE(rows(list), (QStringList{"[online]", "10", "11"}));
    }
};

QTEST_GUILESS_MAIN(MemberListTest)
#include "MemberListTest.moc"
