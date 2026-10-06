#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>

// Draws a round avatar: the user's picture if loaded, otherwise a colored circle with their initial.
// `speakingRing` adds Discord's green "speaking" ring; a valid `statusRing` color adds an online dot.
QPixmap makeAvatar(const QString& name, const QImage& picture, int size, qreal devicePixelRatio,
                   bool speakingRing = false, const QColor& statusRing = {});
