#include "core/Permissions.h"

#include "core/Models.h"

namespace Permissions {

quint64 guildPermissions(const Guild& guild, const QString& userId, const QStringList& roleIds)
{
    if (guild.ownerId == userId)
        return All;

    quint64 permissions = guild.roles.value(guild.id).permissions;
    for (const QString& roleId : roleIds)
        permissions |= guild.roles.value(roleId).permissions;
    if (permissions & Administrator)
        return All;
    return permissions;
}

quint64 compute(const Guild& guild, const Channel& channel, const QString& userId)
{
    if (guild.ownerId == userId)
        return All;

    // The @everyone role shares the guild's ID.
    quint64 permissions = guild.roles.value(guild.id).permissions;
    for (const QString& roleId : guild.selfRoleIds)
        permissions |= guild.roles.value(roleId).permissions;
    if (permissions & Administrator)
        return All;

    quint64 roleAllow = 0;
    quint64 roleDeny = 0;
    const PermissionOverwrite* memberOverwrite = nullptr;
    for (const PermissionOverwrite& overwrite : channel.overwrites) {
        if (overwrite.type == PermissionOverwrite::Role && overwrite.id == guild.id) {
            permissions &= ~overwrite.deny;
            permissions |= overwrite.allow;
        } else if (overwrite.type == PermissionOverwrite::Role && guild.selfRoleIds.contains(overwrite.id)) {
            roleAllow |= overwrite.allow;
            roleDeny |= overwrite.deny;
        } else if (overwrite.type == PermissionOverwrite::Member && overwrite.id == userId) {
            memberOverwrite = &overwrite;
        }
    }
    permissions &= ~roleDeny;
    permissions |= roleAllow;
    if (memberOverwrite) {
        permissions &= ~memberOverwrite->deny;
        permissions |= memberOverwrite->allow;
    }
    return permissions;
}

} // namespace Permissions
