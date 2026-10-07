#include "core/ClientProperties.h"
#include "core/OrderedJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <QtTest>

// The client identification sent to Discord: key order and the values of the web client.
class ClientPropertiesTest : public QObject
{
    Q_OBJECT

    static QStringList keysInOrder(const QByteArray& json)
    {
        // Every key of a flat object is a string followed by a colon.
        QStringList keys;
        static const QRegularExpression key(QStringLiteral("\"([a-z_]+)\":"));
        for (auto it = key.globalMatch(QString::fromUtf8(json)); it.hasNext();)
            keys.append(it.next().captured(1));
        return keys;
    }

private slots:
    void initTestCase()
    {
        // Keeps the settings written here (heartbeat session, build number) apart from the app's.
        QCoreApplication::setOrganizationName(QStringLiteral("SnapcordTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ClientPropertiesTest"));
    }

    void orderedJsonKeepsInsertionOrder()
    {
        const QByteArray json = OrderedJson()
                                    .insert(u"zeta", 1)
                                    .insert(u"alpha", QStringLiteral("a\"b"))
                                    .insert(u"nested", OrderedJson().insert(u"y", true).insert(u"x", QJsonValue()))
                                    .insert(u"list", QJsonArray{1, 2})
                                    .toJson();
        QCOMPARE(json, QByteArray("{\"zeta\":1,\"alpha\":\"a\\\"b\",\"nested\":{\"y\":true,\"x\":null},\"list\":[1,2]}"));
        QVERIFY(QJsonDocument::fromJson(json).isObject());
    }

    void superPropertiesFollowTheWebClient()
    {
        const QByteArray json = QByteArray::fromBase64(ClientProperties::superPropertiesHeader());
        const QJsonObject properties = QJsonDocument::fromJson(json).object();
        QCOMPARE(properties.value(u"browser").toString(), QStringLiteral("Chrome"));
        QVERIFY(properties.value(u"client_build_number").toInt() > 0);
        QVERIFY(properties.value(u"browser_user_agent").toString().contains(
            QStringLiteral("Chrome/") + properties.value(u"browser_version").toString()));

        const QStringList expected{
            QStringLiteral("os"), QStringLiteral("browser"), QStringLiteral("device"), QStringLiteral("system_locale"),
            QStringLiteral("has_client_mods"), QStringLiteral("browser_user_agent"), QStringLiteral("browser_version"),
            QStringLiteral("os_version"), QStringLiteral("referrer"), QStringLiteral("referring_domain"),
            QStringLiteral("referrer_current"), QStringLiteral("referring_domain_current"),
            QStringLiteral("release_channel"), QStringLiteral("client_build_number"),
            QStringLiteral("client_event_source"), QStringLiteral("client_launch_id"),
            QStringLiteral("launch_signature"), QStringLiteral("client_app_state")};
        QCOMPARE(keysInOrder(json).mid(0, expected.size()), expected);
    }

    void identifyAddsConnectionFields()
    {
        const QStringList keys = keysInOrder(ClientProperties::identifyProperties().toJson());
        const int appState = keys.indexOf(u"client_app_state");
        QVERIFY(appState > 0);
        QCOMPARE(keys.mid(appState + 1, 2), (QStringList{QStringLiteral("is_fast_connect"),
                                                         QStringLiteral("gateway_connect_reasons")}));
    }

    void launchSignatureReportsNoClientMods()
    {
        const QJsonObject properties =
            QJsonDocument::fromJson(QByteArray::fromBase64(ClientProperties::superPropertiesHeader())).object();
        const QByteArray bytes = QUuid(properties.value(u"launch_signature").toString()).toRfc4122();
        QCOMPARE(bytes.size(), 16);
        // Bits the official client sets when it detects a client mod.
        static constexpr quint8 Mask[16] = {0xff, 0x7f, 0xef, 0xef, 0xf7, 0xef, 0xf7, 0xff,
                                            0xdf, 0x7e, 0xff, 0xbf, 0xfe, 0xff, 0xf7, 0xff};
        for (int i = 0; i < 16; ++i)
            QCOMPARE(static_cast<quint8>(bytes[i]) & ~Mask[i] & 0xff, 0);
    }

    void heartbeatSessionFollowsActivity()
    {
        QSettings().remove(QStringLiteral("discord/heartbeatSession"));

        ClientProperties::setActivity(true, false);
        const auto first = ClientProperties::touchHeartbeatSession();
        QVERIFY(first == ClientProperties::HeartbeatSessionUpdate::Created
                || first == ClientProperties::HeartbeatSessionUpdate::Touched);
        const auto session = ClientProperties::heartbeatSession();
        QVERIFY(session.has_value());
        QVERIFY(ClientProperties::touchHeartbeatSession() == ClientProperties::HeartbeatSessionUpdate::Touched);
        QCOMPARE(ClientProperties::heartbeatSession()->id, session->id);

        const QJsonObject properties =
            QJsonDocument::fromJson(QByteArray::fromBase64(ClientProperties::superPropertiesHeader())).object();
        QCOMPARE(properties.value(u"client_heartbeat_session_id").toString(), session->id);
        QCOMPARE(properties.value(u"client_app_state").toString(), QStringLiteral("focused"));

        // In the background and outside calls the session is no longer renewed.
        ClientProperties::setActivity(false, false);
        QVERIFY(ClientProperties::touchHeartbeatSession() == ClientProperties::HeartbeatSessionUpdate::Unchanged);
        const QJsonObject unfocused =
            QJsonDocument::fromJson(QByteArray::fromBase64(ClientProperties::superPropertiesHeader())).object();
        QCOMPARE(unfocused.value(u"client_app_state").toString(), QStringLiteral("unfocused"));

        QSettings().remove(QStringLiteral("discord"));
    }
};

QTEST_GUILESS_MAIN(ClientPropertiesTest)
#include "ClientPropertiesTest.moc"
