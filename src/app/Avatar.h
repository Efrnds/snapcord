#pragma once

#include "core/Models.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>

class QPainter;
class QRectF;

// Draws a round avatar: the user's picture if loaded, otherwise a colored circle with their initial.
// `speakingRing` adds Discord's green "speaking" ring; a valid `statusRing` color adds a status dot
// (online, idle, do not disturb or offline) cut out of the avatar with that background color.
QPixmap makeAvatar(const QString& name, const QImage& picture, int size, qreal devicePixelRatio,
                   bool speakingRing = false, const QColor& statusRing = {},
                   UserStatus status = UserStatus::Online);

// The status dot alone, inside `rect`, with a `background`-colored border around it.
void drawStatusDot(QPainter& painter, const QRectF& rect, UserStatus status, const QColor& background);
QColor statusColor(UserStatus status);
