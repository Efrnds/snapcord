#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

// Placeholder avatar: a colored circle with the name's initial.
// If `statusRing` is valid, an "online" dot is drawn with a ring of that color.
QPixmap makeAvatar(const QString& name, int size, qreal devicePixelRatio, const QColor& statusRing = {});
