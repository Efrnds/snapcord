#include "core/GuildFolders.h"

#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {

QString idString(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toInteger());
    return {};
}

// Takes the guild out of wherever it is; a folder left empty disappears.
void removeGuild(QList<GuildFolder>& folders, const QString& guildId)
{
    for (qsizetype i = 0; i < folders.size(); ++i) {
        if (folders[i].guildIds.removeAll(guildId) && folders[i].guildIds.isEmpty()) {
            folders.removeAt(i);
            return;
        }
    }
}

bool isInviteHost(const QString& host)
{
    static const QStringList hosts{QStringLiteral("discord.gg"), QStringLiteral("discord.com"), QStringLiteral("discordapp.com"),
                                   QStringLiteral("ptb.discord.com"), QStringLiteral("canary.discord.com")};
    QString h = host.toLower();
    if (h.startsWith(u"www."))
        h = h.mid(4);
    return hosts.contains(h);
}

bool isCode(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9-]{2,32}$"));
    return pattern.match(text).hasMatch();
}

} // namespace

namespace GuildFolders {

QList<GuildFolder> fromJson(const QJsonArray& json)
{
    QList<GuildFolder> folders;
    for (const QJsonValue& value : json) {
        const QJsonObject object = value.toObject();
        GuildFolder folder;
        folder.id = idString(object.value(u"id"));
        folder.name = object.value(u"name").toString();
        folder.color = object.value(u"color").isDouble() ? object.value(u"color").toInt() : -1;
        for (const QJsonValue& id : object.value(u"guild_ids").toArray())
            folder.guildIds.append(idString(id));
        folders.append(folder);
    }
    return folders;
}

QJsonArray toJson(const QList<GuildFolder>& folders)
{
    QJsonArray json;
    for (const GuildFolder& folder : folders) {
        QJsonObject object;
        object.insert(QStringLiteral("guild_ids"), QJsonArray::fromStringList(folder.guildIds));
        object.insert(QStringLiteral("id"), folder.isFolder() ? QJsonValue(folder.id.toLongLong()) : QJsonValue());
        object.insert(QStringLiteral("name"), folder.isFolder() && !folder.name.isEmpty() ? QJsonValue(folder.name) : QJsonValue());
        object.insert(QStringLiteral("color"), folder.isFolder() && folder.color >= 0 ? QJsonValue(folder.color) : QJsonValue());
        json.append(object);
    }
    return json;
}

QList<GuildFolder> normalized(const QList<GuildFolder>& folders, const QStringList& guildIds)
{
    const QSet<QString> known(guildIds.begin(), guildIds.end());
    QSet<QString> placed;
    QList<GuildFolder> result;
    for (GuildFolder folder : folders) {
        QStringList ids;
        for (const QString& id : std::as_const(folder.guildIds)) {
            if (known.contains(id) && !placed.contains(id)) {
                ids.append(id);
                placed.insert(id);
            }
        }
        if (ids.isEmpty())
            continue;
        if (!folder.isFolder()) {
            // An entry without a folder holds one server.
            for (const QString& id : std::as_const(ids))
                result.append(GuildFolder{{}, {}, -1, {id}});
            continue;
        }
        folder.guildIds = ids;
        result.append(folder);
    }
    QList<GuildFolder> missing;
    for (const QString& id : guildIds) {
        if (!placed.contains(id)) {
            missing.append(GuildFolder{{}, {}, -1, {id}});
            placed.insert(id);
        }
    }
    return missing + result;
}

QStringList flatten(const QList<GuildFolder>& folders)
{
    QStringList ids;
    for (const GuildFolder& folder : folders)
        ids += folder.guildIds;
    return ids;
}

qsizetype entryOf(const QList<GuildFolder>& folders, const QString& guildId)
{
    for (qsizetype i = 0; i < folders.size(); ++i) {
        if (folders[i].guildIds.contains(guildId))
            return i;
    }
    return -1;
}

qsizetype folderIndex(const QList<GuildFolder>& folders, const QString& folderId)
{
    for (qsizetype i = 0; i < folders.size(); ++i) {
        if (folders[i].isFolder() && folders[i].id == folderId)
            return i;
    }
    return -1;
}

QList<GuildFolder> moveGuild(const QList<GuildFolder>& folders, const QString& guildId, const Drop& drop,
                             const QString& newFolderId)
{
    if (drop.guildId == guildId || entryOf(folders, guildId) < 0)
        return folders;
    QList<GuildFolder> result = folders;
    removeGuild(result, guildId);

    if (drop.guildId.isEmpty()) {
        const qsizetype target = folderIndex(result, drop.folderId);
        if (target < 0)
            return folders;
        if (drop.kind == Drop::Combine)
            result[target].guildIds.append(guildId);
        else
            result.insert(drop.kind == Drop::Before ? target : target + 1, GuildFolder{{}, {}, -1, {guildId}});
        return result;
    }

    const qsizetype target = entryOf(result, drop.guildId);
    if (target < 0)
        return folders;
    GuildFolder& entry = result[target];
    if (entry.isFolder()) {
        // Next to (or onto) a server inside a folder: into that folder.
        const qsizetype position = entry.guildIds.indexOf(drop.guildId);
        entry.guildIds.insert(drop.kind == Drop::Before ? position : position + 1, guildId);
    } else if (drop.kind == Drop::Combine) {
        entry.id = newFolderId;
        entry.guildIds.append(guildId);
    } else {
        result.insert(drop.kind == Drop::Before ? target : target + 1, GuildFolder{{}, {}, -1, {guildId}});
    }
    return result;
}

QList<GuildFolder> moveFolder(const QList<GuildFolder>& folders, const QString& folderId, const Drop& drop)
{
    const qsizetype source = folderIndex(folders, folderId);
    if (source < 0 || (drop.guildId.isEmpty() && drop.folderId == folderId))
        return folders;
    QList<GuildFolder> result = folders;
    const GuildFolder folder = result.takeAt(source);
    const qsizetype target = drop.guildId.isEmpty() ? folderIndex(result, drop.folderId) : entryOf(result, drop.guildId);
    if (target < 0)
        return folders;
    result.insert(drop.kind == Drop::Before ? target : target + 1, folder);
    return result;
}

QString newFolderId()
{
    // The official client picks a random 32-bit number.
    return QString::number(QRandomGenerator::global()->bounded(1u, 0xFFFFFFFFu));
}

} // namespace GuildFolders

InviteInfo InviteInfo::fromJson(const QJsonObject& json)
{
    InviteInfo invite;
    invite.code = json.value(u"code").toString();
    invite.type = json.value(u"type").toInt();
    const QJsonObject guild = json.value(u"guild").toObject();
    invite.guildId = guild.value(u"id").toString(json.value(u"guild_id").toString());
    invite.guildName = guild.value(u"name").toString();
    invite.guildIcon = guild.value(u"icon").toString();
    const QJsonObject channel = json.value(u"channel").toObject();
    invite.channelId = channel.value(u"id").toString();
    invite.channelName = channel.value(u"name").toString();
    invite.channelType = channel.value(u"type").toInt();
    const QJsonObject inviter = json.value(u"inviter").toObject();
    invite.inviterName = inviter.value(u"global_name").toString();
    if (invite.inviterName.isEmpty())
        invite.inviterName = inviter.value(u"username").toString();
    invite.memberCount = json.value(u"approximate_member_count").toInt(-1);
    invite.onlineCount = json.value(u"approximate_presence_count").toInt(-1);
    const QString expires = json.value(u"expires_at").toString();
    if (!expires.isEmpty())
        invite.expiresAt = QDateTime::fromString(expires, Qt::ISODateWithMs);
    return invite;
}

namespace Invites {

QString codeFromUrl(const QUrl& url)
{
    if ((url.scheme() != u"https" && url.scheme() != u"http") || !isInviteHost(url.host()))
        return {};
    const QStringList parts = url.path().split(u'/', Qt::SkipEmptyParts);
    QString code;
    if (url.host().toLower().endsWith(u"discord.gg") && parts.size() == 1)
        code = parts[0];
    else if (parts.size() == 2 && parts[0] == u"invite")
        code = parts[1];
    return isCode(code) ? code : QString();
}

QString codeFromText(const QString& text)
{
    QString trimmed = text.trimmed();
    if (isCode(trimmed))
        return trimmed;
    if (!trimmed.contains(u"://"))
        trimmed.prepend(QStringLiteral("https://"));
    return codeFromUrl(QUrl(trimmed));
}

QStringList codesInMessage(const QString& content)
{
    static const QRegularExpression link(
        QStringLiteral(R"((?:https?://)?(?:www\.)?(?:discord\.gg/|(?:(?:ptb\.|canary\.)?discord(?:app)?\.com)/invite/)([A-Za-z0-9-]{2,32})(?![A-Za-z0-9-]))"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList codes;
    for (auto it = link.globalMatch(content); it.hasNext();) {
        const QString code = it.next().captured(1);
        if (!codes.contains(code))
            codes.append(code);
    }
    return codes;
}

QString link(const QString& code)
{
    return QStringLiteral("https://discord.gg/") + code;
}

} // namespace Invites
