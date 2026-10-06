#include "Avatar.h"

#include <QHash>
#include <QPainter>

#include <array>

QPixmap makeAvatar(const QString& name, int size, qreal devicePixelRatio, const QColor& statusRing)
{
    static const std::array<QColor, 6> palette = {
        QColor(0x58, 0x65, 0xf2), QColor(0x75, 0x7e, 0x8a), QColor(0x3b, 0xa5, 0x5c),
        QColor(0xfa, 0xa6, 0x1a), QColor(0xed, 0x42, 0x45), QColor(0xeb, 0x45, 0x9f),
    };

    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette[qHash(name) % palette.size()]);
    painter.drawEllipse(QRectF(0, 0, size, size));

    QFont font = painter.font();
    font.setPixelSize(qRound(size * 0.45));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, name.left(1).toUpper());

    if (statusRing.isValid()) {
        const qreal dot = size * 0.375;
        const QRectF ring(size - dot, size - dot, dot, dot);
        painter.setPen(Qt::NoPen);
        painter.setBrush(statusRing);
        painter.drawEllipse(ring);
        painter.setBrush(QColor(0x23, 0xa5, 0x59));
        painter.drawEllipse(ring.adjusted(dot * 0.2, dot * 0.2, -dot * 0.2, -dot * 0.2));
    }

    return pixmap;
}
