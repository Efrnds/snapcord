#pragma once

#include <QUrl>

class QWidget;

// Direct Discord videos (.mp4 / .webm on the CDN). Playback runs in a separate
// process so the media libraries are not kept inside Snapcord after the window closes.
namespace DiscordVideo {

bool isDiscordFile(const QUrl& url);
void open(const QUrl& url, QWidget* parent);

} // namespace DiscordVideo
