#pragma once

#include <QtGlobal>

struct Channel;
struct Guild;
class QString;

namespace Permissions {

constexpr quint64 CreateInstantInvite = 1ull << 0;
constexpr quint64 Administrator = 1ull << 3;
constexpr quint64 ManageChannels = 1ull << 4;
constexpr quint64 ViewChannel = 1ull << 10;
constexpr quint64 SendMessages = 1ull << 11;
constexpr quint64 AttachFiles = 1ull << 15;
constexpr quint64 MentionEveryone = 1ull << 17;
constexpr quint64 Connect = 1ull << 20;
constexpr quint64 Speak = 1ull << 21;
constexpr quint64 All = ~0ull;

// Effective permissions of a guild member in a channel, following Discord's documented algorithm:
// base role permissions, then @everyone, role and member overwrites.
quint64 compute(const Guild& guild, const Channel& channel, const QString& userId);

} // namespace Permissions
