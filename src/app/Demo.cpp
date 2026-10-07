#include "Demo.h"

#include "ImageCache.h"
#include "ProfileEditor.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "VoiceController.h"
#include "core/Session.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>

namespace {

// Everything below is made up. IDs only need to be unique and ordered like Discord snowflakes.
const QString SelfId = QStringLiteral("200000000000000001");

struct Person
{
    QString id;
    QString name;
    QColor from;
    QColor to;
};

const QList<Person>& people()
{
    static const QList<Person> list = {
        {SelfId, QStringLiteral("Sam"), QColor(0x58, 0x65, 0xf2), QColor(0x8b, 0x5c, 0xf6)},
        {QStringLiteral("200000000000000002"), QStringLiteral("Alex"), QColor(0xf5, 0x9e, 0x0b), QColor(0xef, 0x44, 0x44)},
        {QStringLiteral("200000000000000003"), QStringLiteral("Bia"), QColor(0xec, 0x48, 0x99), QColor(0xa8, 0x55, 0xf7)},
        {QStringLiteral("200000000000000004"), QStringLiteral("Kenji"), QColor(0x06, 0xb6, 0xd4), QColor(0x3b, 0x82, 0xf6)},
        {QStringLiteral("200000000000000005"), QStringLiteral("Marina"), QColor(0x22, 0xc5, 0x5e), QColor(0x14, 0xb8, 0xa6)},
        {QStringLiteral("200000000000000006"), QStringLiteral("Theo"), QColor(0x64, 0x74, 0x8b), QColor(0x33, 0x41, 0x55)},
        {QStringLiteral("200000000000000007"), QStringLiteral("Lina"), QColor(0xf9, 0x73, 0x16), QColor(0xea, 0xb3, 0x08)},
        {QStringLiteral("200000000000000008"), QStringLiteral("Rafa"), QColor(0x84, 0xcc, 0x16), QColor(0x10, 0xb9, 0x81)},
    };
    return list;
}

const Person& person(const QString& name)
{
    for (const Person& p : people()) {
        if (p.name == name)
            return p;
    }
    return people().first();
}

QJsonObject userJson(const QString& name)
{
    const Person& p = person(name);
    return {{QStringLiteral("id"), p.id},
            {QStringLiteral("username"), p.name.toLower()},
            {QStringLiteral("global_name"), p.name},
            {QStringLiteral("avatar"), QStringLiteral("demo")}};
}

struct Server
{
    QString id;
    QString name;
    QString initials;
    QColor from;
    QColor to;
};

const QList<Server>& servers()
{
    static const QList<Server> list = {
        {QStringLiteral("300000000000000001"), QStringLiteral("Night Owls"), QStringLiteral("NO"), QColor(0x4f, 0x46, 0xe5), QColor(0x0e, 0xa5, 0xe9)},
        {QStringLiteral("300000000000000002"), QStringLiteral("Dev Hangout"), QStringLiteral("</>"), QColor(0x10, 0xb9, 0x81), QColor(0x0f, 0x76, 0x6e)},
        {QStringLiteral("300000000000000003"), QStringLiteral("Music Club"), QStringLiteral("♪"), QColor(0xec, 0x48, 0x99), QColor(0xf9, 0x73, 0x16)},
        {QStringLiteral("300000000000000004"), QStringLiteral("Pixel Art"), QStringLiteral("PX"), QColor(0xa8, 0x55, 0xf7), QColor(0x63, 0x66, 0xf1)},
        {QStringLiteral("300000000000000005"), QStringLiteral("Weekend Gamers"), QStringLiteral("WG"), QColor(0xef, 0x44, 0x44), QColor(0xf5, 0x9e, 0x0b)},
    };
    return list;
}

const QString NightOwls = QStringLiteral("300000000000000001");
const QString General = QStringLiteral("310000000000000003");
const QString Memes = QStringLiteral("310000000000000004");
const QString MusicShare = QStringLiteral("310000000000000005");
const QString Lounge = QStringLiteral("310000000000000011");
const QString Gaming = QStringLiteral("310000000000000012");
const QString DmBia = QStringLiteral("400000000000000001");
const QString GroupTrip = QStringLiteral("400000000000000002");
const QString DmTheo = QStringLiteral("400000000000000003");
const QString DmLina = QStringLiteral("400000000000000004");
const QString DmRafa = QStringLiteral("400000000000000005");
const QString ModeratorRole = QStringLiteral("330000000000000001");
const QString NightShiftRole = QStringLiteral("330000000000000002");

QJsonObject channelJson(const QString& id, const QString& name, int type, int position, const QString& parent = {},
                        const QString& lastMessageId = {})
{
    QJsonObject json{{QStringLiteral("id"), id},
                     {QStringLiteral("name"), name},
                     {QStringLiteral("type"), type},
                     {QStringLiteral("position"), position}};
    if (!parent.isEmpty())
        json.insert(QStringLiteral("parent_id"), parent);
    if (!lastMessageId.isEmpty())
        json.insert(QStringLiteral("last_message_id"), lastMessageId);
    return json;
}

QJsonObject voiceStateJson(const QString& name, const QString& channelId, bool muted = false, bool deafened = false)
{
    return {{QStringLiteral("user_id"), person(name).id},
            {QStringLiteral("channel_id"), channelId},
            {QStringLiteral("self_mute"), muted || deafened},
            {QStringLiteral("self_deaf"), deafened}};
}

QJsonObject nightOwlsJson()
{
    const QString text = QStringLiteral("310000000000000001");
    const QString voice = QStringLiteral("310000000000000010");
    QJsonArray channels{
        channelJson(text, QStringLiteral("Text Channels"), 4, 0),
        channelJson(QStringLiteral("310000000000000002"), QStringLiteral("announcements"), 0, 0, text,
                    QStringLiteral("500000000000000001")),
        channelJson(General, QStringLiteral("general"), 0, 1, text, QStringLiteral("500000000000000108")),
        channelJson(Memes, QStringLiteral("memes"), 0, 2, text, QStringLiteral("500000000000000200")),
        channelJson(MusicShare, QStringLiteral("music-share"), 0, 3, text, QStringLiteral("500000000000000300")),
        channelJson(QStringLiteral("310000000000000006"), QStringLiteral("screenshots"), 0, 4, text,
                    QStringLiteral("500000000000000002")),
        channelJson(voice, QStringLiteral("Voice Channels"), 4, 1),
        channelJson(Lounge, QStringLiteral("Lounge"), 2, 0, voice),
        channelJson(Gaming, QStringLiteral("Gaming"), 2, 1, voice),
        channelJson(QStringLiteral("310000000000000013"), QStringLiteral("Study Room"), 2, 2, voice),
    };
    QJsonArray voiceStates{
        voiceStateJson(QStringLiteral("Alex"), Lounge),
        voiceStateJson(QStringLiteral("Bia"), Lounge),
        voiceStateJson(QStringLiteral("Kenji"), Lounge, true),
        voiceStateJson(QStringLiteral("Sam"), Lounge),
        voiceStateJson(QStringLiteral("Marina"), Gaming),
        voiceStateJson(QStringLiteral("Theo"), Gaming, false, true),
    };
    QJsonArray roles{
        QJsonObject{{QStringLiteral("id"), NightOwls}, {QStringLiteral("name"), QStringLiteral("@everyone")}},
        QJsonObject{{QStringLiteral("id"), ModeratorRole}, {QStringLiteral("name"), QStringLiteral("Moderator")},
                    {QStringLiteral("color"), 0xe67e22}, {QStringLiteral("position"), 2}},
        QJsonObject{{QStringLiteral("id"), NightShiftRole}, {QStringLiteral("name"), QStringLiteral("Night Shift")},
                    {QStringLiteral("color"), 0x1abc9c}, {QStringLiteral("position"), 1}},
    };
    return {{QStringLiteral("id"), NightOwls},
            {QStringLiteral("name"), QStringLiteral("Night Owls")},
            {QStringLiteral("icon"), QStringLiteral("demo")},
            {QStringLiteral("owner_id"), SelfId},
            {QStringLiteral("roles"), roles},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("voice_states"), voiceStates}};
}

// The member sidebar of Night Owls, as the Gateway sends it after subscribing: the hoisted Moderator role,
// then everyone online, then everyone offline.
QJsonObject memberListJson()
{
    auto group = [](const QString& id, int count) {
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("count"), count}};
    };
    auto member = [](const QString& name, const QStringList& roles = {}) {
        return QJsonObject{{QStringLiteral("member"), QJsonObject{{QStringLiteral("user"), userJson(name)},
                                                                  {QStringLiteral("roles"), QJsonArray::fromStringList(roles)}}}};
    };
    const QJsonArray groups{group(ModeratorRole, 1), group(QStringLiteral("online"), 6), group(QStringLiteral("offline"), 1)};
    const QJsonArray items{
        QJsonObject{{QStringLiteral("group"), groups[0]}},
        member(QStringLiteral("Alex"), {ModeratorRole, NightShiftRole}),
        QJsonObject{{QStringLiteral("group"), groups[1]}},
        member(QStringLiteral("Bia")),
        member(QStringLiteral("Kenji")),
        member(QStringLiteral("Marina")),
        member(QStringLiteral("Rafa")),
        member(QStringLiteral("Sam"), {NightShiftRole}),
        member(QStringLiteral("Theo")),
        QJsonObject{{QStringLiteral("group"), groups[2]}},
        member(QStringLiteral("Lina")),
    };
    const QJsonObject sync{{QStringLiteral("op"), QStringLiteral("SYNC")},
                           {QStringLiteral("range"), QJsonArray{0, 99}},
                           {QStringLiteral("items"), items}};
    return {{QStringLiteral("guild_id"), NightOwls},
            {QStringLiteral("id"), QStringLiteral("everyone")},
            {QStringLiteral("member_count"), 8},
            {QStringLiteral("online_count"), 7},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("ops"), QJsonArray{sync}}};
}

QJsonObject simpleGuildJson(const Server& server, int index)
{
    // Not QString::arg: "%10…" would be read as placeholder 10.
    const QString base = u'3' + QString::number(index) + QStringLiteral("000000000000");
    const QString category = base + QStringLiteral("0001");
    QJsonArray channels{
        channelJson(category, QStringLiteral("Text Channels"), 4, 0),
        channelJson(base + QStringLiteral("0002"), QStringLiteral("general"), 0, 0, category,
                    u'5' + QString::number(index) + QStringLiteral("0000000000000001")),
        channelJson(base + QStringLiteral("0003"), QStringLiteral("General"), 2, 0, category),
    };
    return {{QStringLiteral("id"), server.id},
            {QStringLiteral("name"), server.name},
            {QStringLiteral("icon"), QStringLiteral("demo")},
            {QStringLiteral("owner_id"), SelfId},
            {QStringLiteral("channels"), channels}};
}

QJsonObject privateChannelJson(const QString& id, int type, const QStringList& recipients, const QString& lastMessageId,
                               const QString& name = {})
{
    QJsonArray users;
    for (const QString& recipient : recipients)
        users.append(userJson(recipient));
    QJsonObject json{{QStringLiteral("id"), id},
                     {QStringLiteral("type"), type},
                     {QStringLiteral("recipients"), users},
                     {QStringLiteral("last_message_id"), lastMessageId}};
    if (!name.isEmpty())
        json.insert(QStringLiteral("name"), name);
    return json;
}

QJsonObject readState(const QString& channelId, const QString& lastReadId, int mentions = 0)
{
    return {{QStringLiteral("id"), channelId},
            {QStringLiteral("last_message_id"), lastReadId},
            {QStringLiteral("mention_count"), mentions}};
}

QJsonObject readyJson()
{
    QJsonArray guilds{nightOwlsJson()};
    for (int i = 1; i < servers().size(); ++i)
        guilds.append(simpleGuildJson(servers()[i], i + 1));

    QJsonArray privateChannels{
        privateChannelJson(DmBia, 1, {QStringLiteral("Bia")}, QStringLiteral("600000000000000005")),
        privateChannelJson(GroupTrip, 3, {QStringLiteral("Alex"), QStringLiteral("Kenji"), QStringLiteral("Marina")},
                           QStringLiteral("600000000000000004"), QStringLiteral("Weekend trip")),
        privateChannelJson(DmTheo, 1, {QStringLiteral("Theo")}, QStringLiteral("600000000000000003")),
        privateChannelJson(DmLina, 1, {QStringLiteral("Lina")}, QStringLiteral("600000000000000002")),
        privateChannelJson(DmRafa, 1, {QStringLiteral("Rafa")}, QStringLiteral("600000000000000001")),
    };

    // Some channels are unread or mention the user, so the badges show up.
    QJsonArray readStates{
        readState(General, QStringLiteral("500000000000000108")),
        readState(Memes, QStringLiteral("500000000000000100")),
        readState(MusicShare, QStringLiteral("500000000000000100"), 1),
        readState(QStringLiteral("320000000000000002"), QStringLiteral("500000000000000000"), 2),
        readState(QStringLiteral("340000000000000002"), QStringLiteral("500000000000000000")),
        readState(DmBia, QStringLiteral("600000000000000005")),
        readState(GroupTrip, QStringLiteral("600000000000000000"), 3),
        readState(DmTheo, QStringLiteral("600000000000000003")),
    };

    const QJsonObject settings{
        {QStringLiteral("status"), QStringLiteral("online")},
        {QStringLiteral("custom_status"), QJsonObject{{QStringLiteral("text"), QStringLiteral("Building a voice client")},
                                                      {QStringLiteral("emoji_name"), QStringLiteral("🛠️")}}},
    };

    return {{QStringLiteral("user"), userJson(QStringLiteral("Sam"))},
            {QStringLiteral("user_settings"), settings},
            {QStringLiteral("guilds"), guilds},
            {QStringLiteral("private_channels"), privateChannels},
            {QStringLiteral("read_state"), readStates}};
}

QJsonObject presenceJson(const QString& name, const QString& status, const QJsonArray& activities = {})
{
    return {{QStringLiteral("user_id"), person(name).id},
            {QStringLiteral("status"), status},
            {QStringLiteral("activities"), activities}};
}

// What friends are up to, as READY_SUPPLEMENTAL brings it.
QJsonObject presencesJson()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QJsonObject game{{QStringLiteral("type"), 0},
                           {QStringLiteral("name"), QStringLiteral("Stardew Valley")},
                           {QStringLiteral("details"), QStringLiteral("Year 3, Summer")},
                           {QStringLiteral("state"), QStringLiteral("Co-op farm")},
                           {QStringLiteral("party"), QJsonObject{{QStringLiteral("size"), QJsonArray{2, 4}}}},
                           {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), now - 83 * 60 * 1000}}}};
    const QJsonObject song{{QStringLiteral("type"), 2},
                           {QStringLiteral("name"), QStringLiteral("Spotify")},
                           {QStringLiteral("sync_id"), QStringLiteral("demo")},
                           {QStringLiteral("details"), QStringLiteral("Midnight Drive")},
                           {QStringLiteral("state"), QStringLiteral("The Neon Owls")},
                           {QStringLiteral("assets"), QJsonObject{{QStringLiteral("large_text"), QStringLiteral("City Lights")}}},
                           {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), now - 95 * 1000},
                                                                      {QStringLiteral("end"), now + 129 * 1000}}}};
    const QJsonObject custom{{QStringLiteral("type"), 4},
                             {QStringLiteral("name"), QStringLiteral("Custom Status")},
                             {QStringLiteral("state"), QStringLiteral("Deadline mode, back at 6")},
                             {QStringLiteral("emoji"), QJsonObject{{QStringLiteral("name"), QStringLiteral("📚")}}}};
    const QJsonArray friends{
        presenceJson(QStringLiteral("Alex"), QStringLiteral("online"), {game}),
        presenceJson(QStringLiteral("Bia"), QStringLiteral("online"), {song}),
        presenceJson(QStringLiteral("Kenji"), QStringLiteral("idle")),
        presenceJson(QStringLiteral("Marina"), QStringLiteral("dnd"), {custom}),
        presenceJson(QStringLiteral("Theo"), QStringLiteral("online")),
        presenceJson(QStringLiteral("Lina"), QStringLiteral("offline")),
        presenceJson(QStringLiteral("Rafa"), QStringLiteral("idle")),
    };
    return {{QStringLiteral("merged_presences"), QJsonObject{{QStringLiteral("friends"), friends}}}};
}

UserProfile demoProfile(const QString& name, const QString& guildId, const QString& bio, const QString& pronouns,
                        int accentColor, const QStringList& roles = {})
{
    UserProfile profile = UserProfile::fromJson({{QStringLiteral("user"), userJson(name)}});
    profile.bio = bio;
    profile.pronouns = pronouns;
    profile.accentColor = accentColor;
    profile.guildId = guildId;
    profile.mutualGuildIds = {NightOwls, servers()[2].id};
    if (!guildId.isEmpty()) {
        profile.roleIds = roles;
        profile.joinedAt = QDateTime(QDate(2023, 4, 12), QTime(21, 0));
    }
    return profile;
}

QString timestamp(int hour, int minute)
{
    // Local time, sent in UTC like Discord does.
    return QDateTime(QDate::currentDate(), QTime(hour, minute)).toUTC().toString(Qt::ISODateWithMs);
}

QJsonObject messageJson(const QString& id, const QString& channelId, const QString& guildId, const QString& author,
                        int hour, int minute, const QString& content)
{
    QJsonObject json{{QStringLiteral("id"), id},
                     {QStringLiteral("channel_id"), channelId},
                     {QStringLiteral("type"), 0},
                     {QStringLiteral("author"), userJson(author)},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("timestamp"), timestamp(hour, minute)}};
    if (!guildId.isEmpty())
        json.insert(QStringLiteral("guild_id"), guildId);
    return json;
}

QJsonObject reaction(const QString& emoji, int count, bool me)
{
    return {{QStringLiteral("emoji"), QJsonObject{{QStringLiteral("name"), emoji}}},
            {QStringLiteral("count"), count},
            {QStringLiteral("me"), me}};
}

QList<Message> generalMessages()
{
    auto message = [](int n, const QString& author, int hour, int minute, const QString& content) {
        return messageJson(QStringLiteral("5000000000000001%1").arg(n, 2, 10, QLatin1Char('0')), General, NightOwls,
                           author, hour, minute, content);
    };

    QList<QJsonObject> list;
    list.append(message(1, QStringLiteral("Marina"), 20, 41, QStringLiteral("good evening, night owls 🌙")));
    list.append(message(2, QStringLiteral("Theo"), 20, 43,
                        QStringLiteral("did everyone update? it opens before I even let go of the mouse")));
    list.append(message(3, QStringLiteral("Kenji"), 20, 44,
                        QStringLiteral("same here, and it barely touches the CPU 😅")));
    const QJsonObject question = message(4, QStringLiteral("Alex"), 20, 52, QStringLiteral("anyone up for a match tonight?"));
    list.append(question);

    QJsonObject reply = message(5, QStringLiteral("Bia"), 20, 53,
                                QStringLiteral("yes! <@%1> see you in **Lounge**").arg(person(QStringLiteral("Alex")).id));
    reply.insert(QStringLiteral("type"), 19);
    reply.insert(QStringLiteral("referenced_message"), question);
    reply.insert(QStringLiteral("mentions"), QJsonArray{userJson(QStringLiteral("Alex"))});
    list.append(reply);

    QJsonObject photo = message(6, QStringLiteral("Kenji"), 20, 58, QStringLiteral("sunset from my balcony yesterday"));
    const QString photoUrl = QStringLiteral("https://demo.snapcord.invalid/sunset.png");
    photo.insert(QStringLiteral("attachments"), QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("700000000000000001")},
        {QStringLiteral("filename"), QStringLiteral("sunset.png")},
        {QStringLiteral("url"), photoUrl},
        {QStringLiteral("proxy_url"), photoUrl},
        {QStringLiteral("content_type"), QStringLiteral("image/png")},
        {QStringLiteral("size"), 412000},
        {QStringLiteral("width"), 800},
        {QStringLiteral("height"), 450},
    }});
    photo.insert(QStringLiteral("reactions"), QJsonArray{reaction(QStringLiteral("❤️"), 3, true),
                                                          reaction(QStringLiteral("🔥"), 2, false)});
    list.append(photo);

    list.append(message(7, QStringLiteral("Marina"), 21, 1,
                        QStringLiteral("tip: right-click someone in voice to change their volume. "
                                       "`push to talk` is in the settings too")));
    list.append(message(8, QStringLiteral("Sam"), 21, 3,
                        QStringLiteral("joining now, noise suppression is *on* 🎧")));

    QList<Message> messages;
    for (const QJsonObject& json : list)
        messages.append(Message::fromJson(json));
    return messages;
}

QList<Message> directMessages()
{
    auto message = [](int n, const QString& author, int minute, const QString& content) {
        return Message::fromJson(messageJson(QStringLiteral("60000000000000000%1").arg(n), DmBia, {}, author, 19, minute, content));
    };
    return {
        message(1, QStringLiteral("Bia"), 12, QStringLiteral("are you coming to the call later?")),
        message(2, QStringLiteral("Sam"), 14, QStringLiteral("yep! just finishing dinner 🍜")),
        message(3, QStringLiteral("Bia"), 15, QStringLiteral("cool, I'll save you a spot in Lounge 🎮")),
        message(4, QStringLiteral("Bia"), 15, QStringLiteral("also, this app is *so* snappy on my old laptop")),
        message(5, QStringLiteral("Sam"), 17, QStringLiteral("right? no browser inside, so it stays light ✨")),
    };
}

QImage gradientSquare(const QColor& from, const QColor& to, const QString& label, qreal fontScale)
{
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    QLinearGradient gradient(0, 0, 256, 256);
    gradient.setColorAt(0, from);
    gradient.setColorAt(1, to);
    painter.fillRect(image.rect(), gradient);
    QFont font = QApplication::font();
    font.setPixelSize(qRound(256 * fontScale));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, label);
    return image;
}

QImage sunset()
{
    QImage image(800, 450, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, 450);
    sky.setColorAt(0, QColor(0x3b, 0x1d, 0x8f));
    sky.setColorAt(0.55, QColor(0xe8, 0x5d, 0x75));
    sky.setColorAt(1, QColor(0xff, 0xb3, 0x47));
    painter.fillRect(image.rect(), sky);

    QRadialGradient glow(QPointF(520, 250), 160);
    glow.setColorAt(0, QColor(255, 230, 150, 200));
    glow.setColorAt(1, QColor(255, 230, 150, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(QPointF(520, 250), 160, 160);
    painter.setBrush(QColor(0xff, 0xe2, 0x8a));
    painter.drawEllipse(QPointF(520, 250), 58, 58);

    QPainterPath far;
    far.moveTo(0, 330);
    far.cubicTo(180, 250, 330, 300, 470, 290);
    far.cubicTo(600, 280, 700, 240, 800, 280);
    far.lineTo(800, 450);
    far.lineTo(0, 450);
    painter.setBrush(QColor(0x5b, 0x2a, 0x6e));
    painter.drawPath(far);

    QPainterPath near;
    near.moveTo(0, 380);
    near.cubicTo(200, 330, 380, 400, 560, 360);
    near.cubicTo(680, 335, 740, 350, 800, 345);
    near.lineTo(800, 450);
    near.lineTo(0, 450);
    painter.setBrush(QColor(0x2a, 0x16, 0x3d));
    painter.drawPath(near);
    return image;
}

// Pictures for the demo, drawn here instead of downloaded.
QImage demoImage(const QUrl& url)
{
    const QString path = url.path();
    if (url.host() == u"demo.snapcord.invalid")
        return sunset();
    if (path.startsWith(u"/avatars/")) {
        const QString id = path.section(u'/', 2, 2);
        for (const Person& p : people()) {
            if (p.id == id)
                return gradientSquare(p.from, p.to, p.name.left(1), 0.5);
        }
    }
    if (path.startsWith(u"/icons/")) {
        const QString id = path.section(u'/', 2, 2);
        for (const Server& s : servers()) {
            if (s.id == id)
                return gradientSquare(s.from, s.to, s.initials, s.initials.size() > 2 ? 0.3 : 0.36);
        }
    }
    return {};
}

} // namespace

DemoController::DemoController(QObject* parent)
    : QObject(parent)
{
}

DemoController::~DemoController()
{
    delete m_main;
}

void DemoController::start(const QString& screenshotFolder)
{
    ImageCache::setOfflineSource(demoImage);

    m_session = new Session(this);
    m_voice = new VoiceController(m_session, this);
    m_main = new MainWindow(m_session, m_voice);
    m_main->resize(1280, 760);

    m_session->startOffline({{QStringLiteral("READY"), readyJson()},
                             {QStringLiteral("READY_SUPPLEMENTAL"), presencesJson()}});
    const QString aboutAlex = QStringLiteral("Night owl, part-time farmer. Ask me about **co-op runs** 🌙");
    const QString aboutSam = QStringLiteral("Making calls lighter, one frame at a time.");
    m_session->cacheProfile(demoProfile(QStringLiteral("Alex"), NightOwls, aboutAlex, QStringLiteral("he/him"), 0x8e44ad,
                                        {ModeratorRole, NightShiftRole}));
    m_session->cacheProfile(demoProfile(QStringLiteral("Alex"), QString(), aboutAlex, QStringLiteral("he/him"), 0x8e44ad));
    m_session->cacheProfile(demoProfile(QStringLiteral("Bia"), QString(), QStringLiteral("Playlist curator ♪"),
                                        QStringLiteral("she/her"), -1));
    m_session->cacheProfile(demoProfile(QStringLiteral("Sam"), NightOwls, aboutSam, QStringLiteral("they/them"), 0x2d7d9a,
                                        {NightShiftRole}));
    m_session->cacheProfile(demoProfile(QStringLiteral("Sam"), QString(), aboutSam, QStringLiteral("they/them"), 0x2d7d9a));
    m_session->messages()->preload(General, generalMessages());
    m_session->messages()->preload(DmBia, directMessages());
    m_voice->showDemoCall(NightOwls, Lounge, {person(QStringLiteral("Alex")).id, SelfId},
                          {42, 41, 44, 40, 43, 41, 39, 42, 45, 41, 40, 42});
    m_main->showChannel(NightOwls, General);
    m_main->show();

    // The member list, and someone typing to show the indicator under the message box.
    m_session->startOffline({{QStringLiteral("GUILD_MEMBER_LIST_UPDATE"), memberListJson()},
                             {QStringLiteral("TYPING_START"),
                              QJsonObject{{QStringLiteral("channel_id"), General},
                                          {QStringLiteral("guild_id"), NightOwls},
                                          {QStringLiteral("user_id"), person(QStringLiteral("Theo")).id}}}});

    if (!screenshotFolder.isEmpty())
        takeScreenshots(screenshotFolder);
}

void DemoController::takeScreenshots(const QString& folder)
{
    QDir().mkpath(folder);
    auto save = [folder](QWidget* widget, const QString& name) {
        widget->grab().save(QDir(folder).filePath(name + QStringLiteral(".png")));
    };

    // Captures the open profile popout, then closes it.
    auto savePopup = [this, save](const QString& name) {
        if (auto* popup = m_main->findChild<QFrame*>(QStringLiteral("profilePopup"))) {
            save(popup, name);
            popup->close();
        }
    };
    const QPoint popupPosition = m_main->mapToGlobal(QPoint(420, 120));

    // Each step waits a little so layouts and pictures settle before the capture.
    m_steps = {
        {800, [this, save] {
             save(m_main, QStringLiteral("chat"));
             m_main->showChannel(NightOwls, Lounge);
         }},
        {500, [this, save] {
             save(m_main, QStringLiteral("voice"));
             m_main->showChannel(QString(), DmBia);
         }},
        {500, [this, save, popupPosition] {
             save(m_main, QStringLiteral("direct-messages"));
             m_main->showChannel(NightOwls, General);
             m_main->showProfile(person(QStringLiteral("Alex")).id, NightOwls, popupPosition);
         }},
        {400, [this, savePopup, popupPosition] {
             savePopup(QStringLiteral("profile"));
             m_main->showProfile(person(QStringLiteral("Bia")).id, QString(), popupPosition);
         }},
        {400, [this, savePopup] {
             savePopup(QStringLiteral("profile-music"));
             m_main->showOwnProfile();
         }},
        {400, [this, savePopup] {
             savePopup(QStringLiteral("profile-self"));
             m_editor = new ProfileEditDialog(m_session, new ImageCache(m_main), m_main);
             m_editor->setAttribute(Qt::WA_DeleteOnClose);
             m_editor->show();
         }},
        {600, [this, save] {
             save(m_editor, QStringLiteral("profile-editor"));
             m_editor->close();
             m_settings = new SettingsDialog(m_voice, m_main);
             m_settings->setAttribute(Qt::WA_DeleteOnClose);
             m_settings->show();
         }},
        {800, [this, save] {
             save(m_settings, QStringLiteral("settings"));
             m_settings->close();
             QApplication::quit();
         }},
    };
    runNextStep();
}

void DemoController::runNextStep()
{
    if (m_steps.isEmpty())
        return;
    const Step step = m_steps.takeFirst();
    QTimer::singleShot(step.delayMs, this, [this, step] {
        step.action();
        runNextStep();
    });
}
