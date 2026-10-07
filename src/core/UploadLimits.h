#pragma once

#include <QtGlobal>

#include <algorithm>

// What Discord accepts in one message. Checked before sending so the user gets a clear message instead of
// a failed upload; the server stays the final judge.
namespace UploadLimits {

constexpr qint64 MiB = 1024 * 1024;
constexpr int MaxFiles = 10;
constexpr qint64 MaxTotalSize = 500 * MiB;

// The largest single file: the better of the user's Nitro plan and the server's boost level.
// `premiumType`: 0 none, 1 Nitro Classic, 2 Nitro, 3 Nitro Basic. `guildTier`: 0-3, 0 outside servers.
inline qint64 maxFileSize(int premiumType, int guildTier)
{
    qint64 user = 10 * MiB;
    if (premiumType == 2)
        user = 500 * MiB;
    else if (premiumType == 1 || premiumType == 3)
        user = 50 * MiB;
    qint64 guild = 10 * MiB;
    if (guildTier >= 3)
        guild = 100 * MiB;
    else if (guildTier == 2)
        guild = 50 * MiB;
    return std::max(user, guild);
}

// Characters per message: Nitro doubles the limit.
inline int maxMessageLength(int premiumType)
{
    return premiumType == 2 ? 4000 : 2000;
}

} // namespace UploadLimits
