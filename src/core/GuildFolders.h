#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

class QJsonObject;
class QUrl;

// One entry of the server list as the user arranged it: a folder, or a single server outside folders.
// It is the "guild_folders" user setting, shared with the official clients.
struct GuildFolder
{
    QString id; // empty for a server outside folders
    QString name;
    int color = -1; // RGB, -1 = default color
    QStringList guildIds;

    bool isFolder() const { return !id.isEmpty(); }
    bool operator==(const GuildFolder& other) const = default;
};

namespace GuildFolders {

QList<GuildFolder> fromJson(const QJsonArray& json);
QJsonArray toJson(const QList<GuildFolder>& folders);

// Keeps only the guilds in `guildIds` (dropping empty folders and repeated guilds) and puts the guilds that
// appear nowhere at the top, as single entries, like the official client shows newly joined servers.
QList<GuildFolder> normalized(const QList<GuildFolder>& folders, const QStringList& guildIds);
QStringList flatten(const QList<GuildFolder>& folders);
// Index of the entry holding the guild, or -1.
qsizetype entryOf(const QList<GuildFolder>& folders, const QString& guildId);
qsizetype folderIndex(const QList<GuildFolder>& folders, const QString& folderId);

// Where something dragged in the server list is dropped: before or after the entry under the mouse, or onto
// it (dropping a server on another one makes a folder; on a folder, adds it to the folder).
struct Drop
{
    enum Kind { Before, After, Combine } kind = Before;
    QString guildId;  // the server under the mouse (inside a folder or not), or empty for a folder header
    QString folderId; // the folder header under the mouse when guildId is empty
};

// `newFolderId` names the folder created when a server is dropped on a server outside folders.
QList<GuildFolder> moveGuild(const QList<GuildFolder>& folders, const QString& guildId, const Drop& drop,
                             const QString& newFolderId);
// Folders only move between the top-level entries.
QList<GuildFolder> moveFolder(const QList<GuildFolder>& folders, const QString& folderId, const Drop& drop);

// A random folder ID in the range the official client uses.
QString newFolderId();

} // namespace GuildFolders

// A server or group invite, as seen before joining.
struct InviteInfo
{
    QString code;
    int type = 0; // 0 = server, 1 = group DM, 2 = friend
    QString guildId;
    QString guildName;
    QString guildIcon;
    QString channelId;
    QString channelName;
    int channelType = 0;
    QString inviterName;
    int memberCount = -1;
    int onlineCount = -1;
    QDateTime expiresAt;

    static InviteInfo fromJson(const QJsonObject& json);
};

namespace Invites {

// The code of a Discord invite link (discord.gg/abc, discord.com/invite/abc...), or the text itself when it
// is a bare code; empty when it is neither.
QString codeFromText(const QString& text);
QString codeFromUrl(const QUrl& url);
// Every distinct invite code linked in a message, in order.
QStringList codesInMessage(const QString& content);
QString link(const QString& code);

} // namespace Invites
