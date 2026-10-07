#include "Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QVariant>
#include <QWidget>

namespace {

QColor colorFromHsvDelta(QColor base, int satDelta, int valueDelta)
{
    int h = 0;
    int s = 0;
    int v = 0;
    int a = 0;
    base.getHsv(&h, &s, &v, &a);
    return QColor::fromHsv(h, qBound(0, s + satDelta, 255), qBound(0, v + valueDelta, 255), a);
}

QString hex(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QColor* paletteField(Theme::Palette& palette, const QString& id)
{
    if (id == u"bg0")
        return &palette.bg0;
    if (id == u"bg1")
        return &palette.bg1;
    if (id == u"bg2")
        return &palette.bg2;
    if (id == u"bg3")
        return &palette.bg3;
    if (id == u"bg4")
        return &palette.bg4;
    if (id == u"surface")
        return &palette.surface;
    if (id == u"hover")
        return &palette.hover;
    if (id == u"selected")
        return &palette.selected;
    if (id == u"border")
        return &palette.border;
    if (id == u"text")
        return &palette.text;
    if (id == u"textBright")
        return &palette.textBright;
    if (id == u"textMuted")
        return &palette.textMuted;
    if (id == u"textDim")
        return &palette.textDim;
    if (id == u"accent")
        return &palette.accent;
    if (id == u"accentHover")
        return &palette.accentHover;
    if (id == u"accentMuted")
        return &palette.accentMuted;
    if (id == u"button")
        return &palette.button;
    if (id == u"buttonHover")
        return &palette.buttonHover;
    if (id == u"success")
        return &palette.success;
    if (id == u"successHover")
        return &palette.successHover;
    if (id == u"danger")
        return &palette.danger;
    if (id == u"dangerHover")
        return &palette.dangerHover;
    if (id == u"warning")
        return &palette.warning;
    if (id == u"link")
        return &palette.link;
    if (id == u"onAccent")
        return &palette.onAccent;
    return nullptr;
}

const QColor* paletteField(const Theme::Palette& palette, const QString& id)
{
    return paletteField(const_cast<Theme::Palette&>(palette), id);
}

QColor readColor(const QSettings& settings, const QString& key)
{
    const QVariant value = settings.value(key);
    if (!value.isValid())
        return {};
    const QColor color(value.toString());
    return color.isValid() ? color : QColor();
}

void writeColor(QSettings& settings, const QString& key, const QColor& color)
{
    if (color.isValid())
        settings.setValue(key, color.name(QColor::HexRgb));
    else
        settings.remove(key);
}

QColor colorFromJson(const QJsonValue& value)
{
    if (value.isNull() || !value.isString())
        return {};
    const QColor color(value.toString());
    return color.isValid() ? color : QColor();
}

QJsonValue colorToJson(const QColor& color)
{
    return color.isValid() ? QJsonValue(color.name(QColor::HexRgb)) : QJsonValue();
}

// Cheap soft blur: downscale then upscale (good enough for wallpaper frosted look).
QImage softBlur(QImage image, int strength)
{
    if (strength <= 0 || image.isNull())
        return image;
    const int factor = qBound(2, strength, 12);
    const QSize small(qMax(1, image.width() / factor), qMax(1, image.height() / factor));
    return image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

bool sameColor(const QColor& a, const QColor& b)
{
    if (!a.isValid() && !b.isValid())
        return true;
    if (!a.isValid() || !b.isValid())
        return false;
    return a.rgb() == b.rgb();
}

bool sameTokenMap(const QMap<QString, QColor>& a, const QMap<QString, QColor>& b)
{
    if (a.size() != b.size())
        return false;
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (!b.contains(it.key()) || !sameColor(it.value(), b.value(it.key())))
            return false;
    }
    return true;
}

QColor shiftValue(QColor color, int valueDelta)
{
    return colorFromHsvDelta(color, 0, valueDelta);
}

QColor shiftSaturation(QColor color, int satDelta)
{
    return colorFromHsvDelta(color, satDelta, 0);
}

QString rgba(const QColor& color, int opacityPercent)
{
    const int alpha = qBound(0, qRound(255 * opacityPercent / 100.0), 255);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

Theme::Palette withAccent(Theme::Palette palette, const QColor& accent)
{
    palette.accent = accent;
    palette.accentHover = colorFromHsvDelta(accent, 0, -28);
    palette.accentMuted = colorFromHsvDelta(accent, -40, -50);
    return palette;
}

Theme::Palette withStatusColors(Theme::Palette palette)
{
    palette.successHover = colorFromHsvDelta(palette.success, 0, -36);
    palette.dangerHover = colorFromHsvDelta(palette.danger, 0, -36);
    return palette;
}

Theme::Palette discordPalette()
{
    Theme::Palette p;
    p.bg0 = QColor(0x1e, 0x1f, 0x22);
    p.bg1 = QColor(0x2b, 0x2d, 0x31);
    p.bg2 = QColor(0x31, 0x33, 0x38);
    p.bg3 = QColor(0x23, 0x24, 0x28);
    p.bg4 = QColor(0x11, 0x12, 0x14);
    p.surface = QColor(0x1e, 0x1f, 0x22);
    p.hover = QColor(0x35, 0x37, 0x3c);
    p.selected = QColor(0x40, 0x42, 0x49);
    p.border = QColor(0x1f, 0x20, 0x23);
    p.text = QColor(0xdb, 0xde, 0xe1);
    p.textBright = QColor(0xf2, 0xf3, 0xf5);
    p.textMuted = QColor(0x94, 0x9b, 0xa4);
    p.textDim = QColor(0xb5, 0xba, 0xc1);
    p.button = QColor(0x4e, 0x50, 0x58);
    p.buttonHover = QColor(0x6d, 0x6f, 0x78);
    p.success = QColor(0x23, 0xa5, 0x59);
    p.danger = QColor(0xf2, 0x3f, 0x43);
    p.warning = QColor(0xf0, 0xb2, 0x32);
    p.link = QColor(0x00, 0xa8, 0xfc);
    p.onAccent = QColor(0xff, 0xff, 0xff);
    return withStatusColors(withAccent(p, QColor(0x58, 0x65, 0xf2)));
}

Theme::Palette lightPalette()
{
    Theme::Palette p;
    p.bg0 = QColor(0xe3, 0xe5, 0xe8);
    p.bg1 = QColor(0xf2, 0xf3, 0xf5);
    p.bg2 = QColor(0xff, 0xff, 0xff);
    p.bg3 = QColor(0xeb, 0xed, 0xf0);
    p.bg4 = QColor(0xff, 0xff, 0xff);
    p.surface = QColor(0xe3, 0xe5, 0xe8);
    p.hover = QColor(0xd8, 0xdb, 0xdf);
    p.selected = QColor(0xd0, 0xd4, 0xd9);
    p.border = QColor(0xd8, 0xdb, 0xdf);
    p.text = QColor(0x31, 0x33, 0x38);
    p.textBright = QColor(0x06, 0x06, 0x07);
    p.textMuted = QColor(0x5c, 0x5e, 0x66);
    p.textDim = QColor(0x4e, 0x50, 0x58);
    p.button = QColor(0xd0, 0xd4, 0xd9);
    p.buttonHover = QColor(0xbc, 0xc1, 0xc7);
    p.success = QColor(0x24, 0x8a, 0x46);
    p.danger = QColor(0xda, 0x37, 0x3c);
    p.warning = QColor(0xb5, 0x78, 0x0c);
    p.link = QColor(0x00, 0x6c, 0xe7);
    p.onAccent = QColor(0xff, 0xff, 0xff);
    return withStatusColors(withAccent(p, QColor(0x58, 0x65, 0xf2)));
}

// Community palettes mapped onto Snapcord's Discord-like layers (rail / sidebar / chat).
Theme::Palette themedPalette(QColor bg0, QColor bg1, QColor bg2, QColor bg3, QColor bg4,
                             QColor surface, QColor hover, QColor selected, QColor border,
                             QColor text, QColor textBright, QColor textMuted, QColor textDim,
                             QColor button, QColor buttonHover, QColor success, QColor danger,
                             QColor warning, QColor link, QColor accent, QColor onAccent = Qt::white)
{
    Theme::Palette p;
    p.bg0 = bg0;
    p.bg1 = bg1;
    p.bg2 = bg2;
    p.bg3 = bg3;
    p.bg4 = bg4;
    p.surface = surface;
    p.hover = hover;
    p.selected = selected;
    p.border = border;
    p.text = text;
    p.textBright = textBright;
    p.textMuted = textMuted;
    p.textDim = textDim;
    p.button = button;
    p.buttonHover = buttonHover;
    p.success = success;
    p.danger = danger;
    p.warning = warning;
    p.link = link;
    p.onAccent = onAccent;
    return withStatusColors(withAccent(p, accent));
}

QVector<Theme::Preset> buildPresets()
{
    QVector<Theme::Preset> list;

    list.push_back({QStringLiteral("discord"), QStringLiteral("Discord"), discordPalette()});

    {
        Theme::Palette p = discordPalette();
        p.bg0 = QColor(0x0b, 0x0c, 0x0f);
        p.bg1 = QColor(0x12, 0x14, 0x1a);
        p.bg2 = QColor(0x16, 0x18, 0x20);
        p.bg3 = QColor(0x0e, 0x10, 0x16);
        p.bg4 = QColor(0x07, 0x08, 0x0c);
        p.surface = QColor(0x0b, 0x0c, 0x0f);
        p.hover = QColor(0x1c, 0x1f, 0x2a);
        p.selected = QColor(0x26, 0x2a, 0x38);
        p.border = QColor(0x1a, 0x1c, 0x24);
        list.push_back({QStringLiteral("midnight"), QStringLiteral("Midnight"),
                        withStatusColors(withAccent(p, QColor(0x6c, 0x79, 0xff)))});
    }
    {
        Theme::Palette p = discordPalette();
        p.bg0 = QColor(0x00, 0x00, 0x00);
        p.bg1 = QColor(0x08, 0x08, 0x08);
        p.bg2 = QColor(0x0c, 0x0c, 0x0c);
        p.bg3 = QColor(0x05, 0x05, 0x05);
        p.bg4 = QColor(0x00, 0x00, 0x00);
        p.surface = QColor(0x12, 0x12, 0x12);
        p.hover = QColor(0x1a, 0x1a, 0x1a);
        p.selected = QColor(0x24, 0x24, 0x24);
        p.border = QColor(0x1a, 0x1a, 0x1a);
        list.push_back({QStringLiteral("amoled"), QStringLiteral("AMOLED"),
                        withStatusColors(withAccent(p, QColor(0x58, 0x65, 0xf2)))});
    }
    {
        Theme::Palette p = discordPalette();
        p.bg0 = QColor(0x1a, 0x1c, 0x1e);
        p.bg1 = QColor(0x24, 0x27, 0x2a);
        p.bg2 = QColor(0x2a, 0x2e, 0x32);
        p.bg3 = QColor(0x1e, 0x21, 0x24);
        p.bg4 = QColor(0x12, 0x14, 0x16);
        p.surface = QColor(0x1a, 0x1c, 0x1e);
        p.hover = QColor(0x32, 0x36, 0x3b);
        p.selected = QColor(0x3a, 0x3f, 0x45);
        list.push_back({QStringLiteral("ash"), QStringLiteral("Ash"),
                        withStatusColors(withAccent(p, QColor(0x8b, 0x9c, 0xf7)))});
    }

    // --- Popular community palettes -----------------------------------------------------------
    list.push_back({QStringLiteral("catppuccin-mocha"), QStringLiteral("Catppuccin Mocha"),
                    themedPalette(QColor(0x11, 0x11, 0x1b), QColor(0x18, 0x18, 0x25), QColor(0x1e, 0x1e, 0x2e),
                                  QColor(0x18, 0x18, 0x25), QColor(0x11, 0x11, 0x1b), QColor(0x31, 0x32, 0x44),
                                  QColor(0x45, 0x47, 0x5a), QColor(0x58, 0x5b, 0x70), QColor(0x31, 0x32, 0x44),
                                  QColor(0xcd, 0xd6, 0xf4), QColor(0xcd, 0xd6, 0xf4), QColor(0x6c, 0x70, 0x86),
                                  QColor(0xa6, 0xad, 0xc8), QColor(0x45, 0x47, 0x5a), QColor(0x58, 0x5b, 0x70),
                                  QColor(0xa6, 0xe3, 0xa1), QColor(0xf3, 0x8b, 0xa8), QColor(0xf9, 0xe2, 0xaf),
                                  QColor(0x89, 0xdc, 0xeb), QColor(0xcb, 0xa6, 0xf7))});

    list.push_back({QStringLiteral("catppuccin-latte"), QStringLiteral("Catppuccin Latte"),
                    themedPalette(QColor(0xdc, 0xe0, 0xe8), QColor(0xe6, 0xe9, 0xef), QColor(0xef, 0xf1, 0xf5),
                                  QColor(0xe6, 0xe9, 0xef), QColor(0xdc, 0xe0, 0xe8), QColor(0xcc, 0xd0, 0xda),
                                  QColor(0xbc, 0xc0, 0xcc), QColor(0xac, 0xb0, 0xbe), QColor(0xcc, 0xd0, 0xda),
                                  QColor(0x4c, 0x4f, 0x69), QColor(0x4c, 0x4f, 0x69), QColor(0x8c, 0x8f, 0xa1),
                                  QColor(0x6c, 0x6f, 0x85), QColor(0xbc, 0xc0, 0xcc), QColor(0xac, 0xb0, 0xbe),
                                  QColor(0x40, 0xa0, 0x2b), QColor(0xd2, 0x0f, 0x39), QColor(0xdf, 0x8e, 0x1d),
                                  QColor(0x04, 0xa5, 0xe5), QColor(0x88, 0x39, 0xef))});

    list.push_back({QStringLiteral("nord"), QStringLiteral("Nord"),
                    themedPalette(QColor(0x2e, 0x34, 0x40), QColor(0x3b, 0x42, 0x52), QColor(0x43, 0x4c, 0x5e),
                                  QColor(0x3b, 0x42, 0x52), QColor(0x2e, 0x34, 0x40), QColor(0x3b, 0x42, 0x52),
                                  QColor(0x43, 0x4c, 0x5e), QColor(0x4c, 0x56, 0x6a), QColor(0x4c, 0x56, 0x6a),
                                  QColor(0xd8, 0xde, 0xe9), QColor(0xec, 0xef, 0xf4), QColor(0x4c, 0x56, 0x6a),
                                  QColor(0xe5, 0xe9, 0xf0), QColor(0x4c, 0x56, 0x6a), QColor(0x5e, 0x81, 0xac),
                                  QColor(0xa3, 0xbe, 0x8c), QColor(0xbf, 0x61, 0x6a), QColor(0xeb, 0xcb, 0x8b),
                                  QColor(0x88, 0xc0, 0xd0), QColor(0x81, 0xa1, 0xc1))});

    list.push_back({QStringLiteral("dracula"), QStringLiteral("Dracula"),
                    themedPalette(QColor(0x21, 0x22, 0x2c), QColor(0x28, 0x2a, 0x36), QColor(0x28, 0x2a, 0x36),
                                  QColor(0x21, 0x22, 0x2c), QColor(0x19, 0x1a, 0x21), QColor(0x44, 0x47, 0x5a),
                                  QColor(0x44, 0x47, 0x5a), QColor(0x62, 0x72, 0xa4), QColor(0x44, 0x47, 0x5a),
                                  QColor(0xf8, 0xf8, 0xf2), QColor(0xf8, 0xf8, 0xf2), QColor(0x62, 0x72, 0xa4),
                                  QColor(0xbd, 0x93, 0xf9), QColor(0x44, 0x47, 0x5a), QColor(0x62, 0x72, 0xa4),
                                  QColor(0x50, 0xfa, 0x7b), QColor(0xff, 0x55, 0x55), QColor(0xf1, 0xfa, 0x8c),
                                  QColor(0x8b, 0xe9, 0xfd), QColor(0xbd, 0x93, 0xf9))});

    list.push_back({QStringLiteral("gruvbox"), QStringLiteral("Gruvbox"),
                    themedPalette(QColor(0x1d, 0x20, 0x21), QColor(0x28, 0x28, 0x28), QColor(0x3c, 0x38, 0x36),
                                  QColor(0x28, 0x28, 0x28), QColor(0x1d, 0x20, 0x21), QColor(0x3c, 0x38, 0x36),
                                  QColor(0x50, 0x49, 0x45), QColor(0x66, 0x5c, 0x54), QColor(0x50, 0x49, 0x45),
                                  QColor(0xeb, 0xdb, 0xb2), QColor(0xfb, 0xf1, 0xc7), QColor(0x92, 0x83, 0x74),
                                  QColor(0xd5, 0xc4, 0xa1), QColor(0x50, 0x49, 0x45), QColor(0x66, 0x5c, 0x54),
                                  QColor(0xb8, 0xbb, 0x26), QColor(0xfb, 0x49, 0x34), QColor(0xfa, 0xbd, 0x2f),
                                  QColor(0x83, 0xa5, 0x98), QColor(0xfe, 0x80, 0x19))});

    list.push_back({QStringLiteral("tokyo-night"), QStringLiteral("Tokyo Night"),
                    themedPalette(QColor(0x16, 0x16, 0x1e), QColor(0x1a, 0x1b, 0x26), QColor(0x1a, 0x1b, 0x26),
                                  QColor(0x16, 0x16, 0x1e), QColor(0x11, 0x11, 0x17), QColor(0x24, 0x28, 0x3b),
                                  QColor(0x29, 0x2e, 0x42), QColor(0x3b, 0x42, 0x61), QColor(0x29, 0x2e, 0x42),
                                  QColor(0xa9, 0xb1, 0xd6), QColor(0xc0, 0xca, 0xf5), QColor(0x56, 0x5f, 0x89),
                                  QColor(0x9a, 0xa5, 0xce), QColor(0x29, 0x2e, 0x42), QColor(0x3b, 0x42, 0x61),
                                  QColor(0x9e, 0xce, 0x6a), QColor(0xf7, 0x76, 0x8e), QColor(0xe0, 0xaf, 0x68),
                                  QColor(0x7d, 0xcf, 0xff), QColor(0x7a, 0xa2, 0xf7))});

    list.push_back({QStringLiteral("rose-pine"), QStringLiteral("Rosé Pine"),
                    themedPalette(QColor(0x19, 0x17, 0x24), QColor(0x1f, 0x1d, 0x2e), QColor(0x1f, 0x1d, 0x2e),
                                  QColor(0x26, 0x23, 0x3a), QColor(0x19, 0x17, 0x24), QColor(0x26, 0x23, 0x3a),
                                  QColor(0x26, 0x23, 0x3a), QColor(0x40, 0x3d, 0x52), QColor(0x26, 0x23, 0x3a),
                                  QColor(0xe0, 0xde, 0xf4), QColor(0xe0, 0xde, 0xf4), QColor(0x6e, 0x6a, 0x86),
                                  QColor(0x90, 0x8c, 0xaa), QColor(0x26, 0x23, 0x3a), QColor(0x40, 0x3d, 0x52),
                                  QColor(0x31, 0x74, 0x8f), QColor(0xeb, 0x6f, 0x92), QColor(0xf6, 0xc1, 0x77),
                                  QColor(0x9c, 0xcf, 0xd8), QColor(0xc4, 0xa7, 0xe7))});

    list.push_back({QStringLiteral("one-dark"), QStringLiteral("One Dark"),
                    themedPalette(QColor(0x21, 0x25, 0x2b), QColor(0x28, 0x2c, 0x34), QColor(0x28, 0x2c, 0x34),
                                  QColor(0x21, 0x25, 0x2b), QColor(0x1b, 0x1f, 0x23), QColor(0x2c, 0x31, 0x3a),
                                  QColor(0x2c, 0x31, 0x3a), QColor(0x3e, 0x44, 0x51), QColor(0x3e, 0x44, 0x51),
                                  QColor(0xab, 0xb2, 0xbf), QColor(0xab, 0xb2, 0xbf), QColor(0x5c, 0x63, 0x70),
                                  QColor(0x9d, 0xa5, 0xb4), QColor(0x3e, 0x44, 0x51), QColor(0x4b, 0x52, 0x63),
                                  QColor(0x98, 0xc3, 0x79), QColor(0xe0, 0x6c, 0x75), QColor(0xe5, 0xc0, 0x7b),
                                  QColor(0x56, 0xb6, 0xc2), QColor(0x61, 0xaf, 0xef))});

    list.push_back({QStringLiteral("light"), QStringLiteral("Light"), lightPalette()});

    return list;
}

const QVector<Theme::Preset>& presetCache()
{
    static const QVector<Theme::Preset> list = buildPresets();
    return list;
}

// Map removed accent-only presets to a custom accent so upgrades keep the color.
QColor accentForLegacyPreset(const QString& id)
{
    if (id == QLatin1String("rose"))
        return QColor(0xeb, 0x45, 0x9f);
    if (id == QLatin1String("emerald"))
        return QColor(0x23, 0xa5, 0x59);
    if (id == QLatin1String("sunset"))
        return QColor(0xf2, 0x65, 0x22);
    if (id == QLatin1String("ocean"))
        return QColor(0x00, 0xb0, 0xf4);
    return {};
}

} // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

Theme::Theme(QObject* parent)
    : QObject(parent)
{
    load();
    m_palette = resolvePalette();
}

QVector<Theme::Preset> Theme::presets()
{
    return presetCache();
}

const Theme::Preset* Theme::findPreset(const QString& id)
{
    const QVector<Preset>& list = presetCache();
    for (const Preset& preset : list) {
        if (preset.id == id)
            return &preset;
    }
    return &list.front();
}

QVector<QColor> Theme::accentSwatches()
{
    return {
        QColor(0x58, 0x65, 0xf2), // blurple
        QColor(0x9b, 0x59, 0xb6), // violet
        QColor(0xeb, 0x45, 0x9f), // rose
        QColor(0xed, 0x42, 0x45), // red
        QColor(0xf2, 0x65, 0x22), // sunset
        QColor(0xfa, 0xa6, 0x1a), // gold
        QColor(0x23, 0xa5, 0x59), // emerald
        QColor(0x00, 0xb0, 0xf4), // ocean
    };
}

QStringList Theme::fontFamilyChoices()
{
    // Empty string = default stack. Only list families that usually exist on desktop OSes.
    QStringList choices{QString()};
    const QStringList candidates{QStringLiteral("Noto Sans"), QStringLiteral("Inter"),
                                 QStringLiteral("Segoe UI"), QStringLiteral("Cantarell"),
                                 QStringLiteral("DejaVu Sans")};
    for (const QString& family : candidates) {
        if (QFontDatabase::hasFamily(family))
            choices.push_back(family);
    }
    return choices;
}

QStringList Theme::tokenIds()
{
    return {
        QStringLiteral("bg0"),        QStringLiteral("bg1"),         QStringLiteral("bg2"),
        QStringLiteral("bg3"),        QStringLiteral("bg4"),         QStringLiteral("surface"),
        QStringLiteral("hover"),      QStringLiteral("selected"),    QStringLiteral("border"),
        QStringLiteral("text"),       QStringLiteral("textBright"),  QStringLiteral("textMuted"),
        QStringLiteral("textDim"),    QStringLiteral("accent"),      QStringLiteral("accentHover"),
        QStringLiteral("accentMuted"),QStringLiteral("button"),      QStringLiteral("buttonHover"),
        QStringLiteral("success"),    QStringLiteral("successHover"),QStringLiteral("danger"),
        QStringLiteral("dangerHover"),QStringLiteral("warning"),     QStringLiteral("link"),
        QStringLiteral("onAccent"),
    };
}

QString Theme::tokenLabel(const QString& id)
{
    static const QMap<QString, QString> labels{
        {QStringLiteral("bg0"), QStringLiteral("Rail (bg0)")},
        {QStringLiteral("bg1"), QStringLiteral("Sidebar (bg1)")},
        {QStringLiteral("bg2"), QStringLiteral("Chat (bg2)")},
        {QStringLiteral("bg3"), QStringLiteral("Panels (bg3)")},
        {QStringLiteral("bg4"), QStringLiteral("Menus (bg4)")},
        {QStringLiteral("surface"), QStringLiteral("Inputs")},
        {QStringLiteral("hover"), QStringLiteral("Hover")},
        {QStringLiteral("selected"), QStringLiteral("Selected")},
        {QStringLiteral("border"), QStringLiteral("Border")},
        {QStringLiteral("text"), QStringLiteral("Text")},
        {QStringLiteral("textBright"), QStringLiteral("Text bright")},
        {QStringLiteral("textMuted"), QStringLiteral("Text muted")},
        {QStringLiteral("textDim"), QStringLiteral("Text dim")},
        {QStringLiteral("accent"), QStringLiteral("Accent")},
        {QStringLiteral("accentHover"), QStringLiteral("Accent hover")},
        {QStringLiteral("accentMuted"), QStringLiteral("Accent muted")},
        {QStringLiteral("button"), QStringLiteral("Button")},
        {QStringLiteral("buttonHover"), QStringLiteral("Button hover")},
        {QStringLiteral("success"), QStringLiteral("Success")},
        {QStringLiteral("successHover"), QStringLiteral("Success hover")},
        {QStringLiteral("danger"), QStringLiteral("Danger")},
        {QStringLiteral("dangerHover"), QStringLiteral("Danger hover")},
        {QStringLiteral("warning"), QStringLiteral("Warning")},
        {QStringLiteral("link"), QStringLiteral("Link")},
        {QStringLiteral("onAccent"), QStringLiteral("On accent")},
    };
    return labels.value(id, id);
}

QColor Theme::profilePrimary() const
{
    return m_settings.profilePrimary.isValid() ? m_settings.profilePrimary : m_palette.bg3;
}

QColor Theme::profileAccent() const
{
    return m_settings.profileAccent.isValid() ? m_settings.profileAccent : m_palette.accent;
}

int Theme::messageGroupGap() const
{
    switch (m_settings.chatDensity) {
    case ChatDensity::Compact:
        return 6;
    case ChatDensity::Comfortable:
        return 22;
    case ChatDensity::Normal:
    default:
        return 14;
    }
}

int Theme::messageTightGap() const
{
    switch (m_settings.chatDensity) {
    case ChatDensity::Compact:
        return 0;
    case ChatDensity::Comfortable:
        return 4;
    case ChatDensity::Normal:
    default:
        return 1;
    }
}

void Theme::normalize(Settings& settings) const
{
    settings.fontSize = qBound(12, settings.fontSize, 18);
    settings.radius = qBound(0, settings.radius, 16);
    settings.uiScale = qBound(85, settings.uiScale, 130);
    settings.brightness = qBound(-40, settings.brightness, 40);
    settings.saturation = qBound(-50, settings.saturation, 50);
    settings.panelOpacity = qBound(40, settings.panelOpacity, 100);
    settings.wallpaperOpacity = qBound(0, settings.wallpaperOpacity, 100);
    settings.wallpaperBlur = qBound(0, settings.wallpaperBlur, 12);
    settings.wallpaperFrost = qBound(0, settings.wallpaperFrost, 80);
    if (settings.chatDensity != ChatDensity::Compact
        && settings.chatDensity != ChatDensity::Comfortable)
        settings.chatDensity = ChatDensity::Normal;
    if (findPreset(settings.presetId)->id != settings.presetId)
        settings.presetId = QStringLiteral("discord");
    if (!settings.fontFamily.isEmpty() && !QFontDatabase::hasFamily(settings.fontFamily))
        settings.fontFamily.clear();
    if (!settings.gradientTop.isValid())
        settings.gradientTop = QColor(0x58, 0x65, 0xf2);
    if (!settings.gradientBottom.isValid())
        settings.gradientBottom = QColor(0x11, 0x12, 0x14);

    QMap<QString, QColor> cleaned;
    for (auto it = settings.tokenOverrides.begin(); it != settings.tokenOverrides.end(); ++it) {
        if (it.value().isValid() && tokenIds().contains(it.key()))
            cleaned.insert(it.key(), it.value());
    }
    settings.tokenOverrides = cleaned;
}

void Theme::load()
{
    QSettings settings;
    m_settings.presetId = settings.value(QStringLiteral("appearance/preset"), QStringLiteral("discord")).toString();
    m_settings.customAccent = readColor(settings, QStringLiteral("appearance/customAccent"));
    m_settings.customBg0 = readColor(settings, QStringLiteral("appearance/customBg0"));
    m_settings.customBg1 = readColor(settings, QStringLiteral("appearance/customBg1"));
    m_settings.customBg2 = readColor(settings, QStringLiteral("appearance/customBg2"));

    // Former Rose/Emerald/Sunset/Ocean presets are now accent chips on Discord.
    if (const QColor legacy = accentForLegacyPreset(m_settings.presetId); legacy.isValid()) {
        if (!m_settings.customAccent.isValid())
            m_settings.customAccent = legacy;
        m_settings.presetId = QStringLiteral("discord");
    }

    m_settings.profilePrimary = readColor(settings, QStringLiteral("appearance/profilePrimary"));
    m_settings.profileAccent = readColor(settings, QStringLiteral("appearance/profileAccent"));
    m_settings.fontSize = settings.value(QStringLiteral("appearance/fontSize"), 14).toInt();
    m_settings.radius = settings.value(QStringLiteral("appearance/radius"), 4).toInt();
    m_settings.uiScale = settings.value(QStringLiteral("appearance/uiScale"), 100).toInt();
    m_settings.brightness = settings.value(QStringLiteral("appearance/brightness"), 0).toInt();
    m_settings.saturation = settings.value(QStringLiteral("appearance/saturation"), 0).toInt();
    m_settings.panelOpacity = settings.value(QStringLiteral("appearance/panelOpacity"), 100).toInt();
    m_settings.chatDensity = static_cast<ChatDensity>(
        settings.value(QStringLiteral("appearance/chatDensity"), static_cast<int>(ChatDensity::Normal)).toInt());
    m_settings.fontFamily = settings.value(QStringLiteral("appearance/fontFamily")).toString();
    m_settings.wallpaperPath = settings.value(QStringLiteral("appearance/wallpaperPath")).toString();
    m_settings.wallpaperOpacity = settings.value(QStringLiteral("appearance/wallpaperOpacity"), 35).toInt();
    m_settings.wallpaperBlur = settings.value(QStringLiteral("appearance/wallpaperBlur"), 0).toInt();
    m_settings.wallpaperFrost = settings.value(QStringLiteral("appearance/wallpaperFrost"), 25).toInt();
    m_settings.wallpaperAppWide = settings.value(QStringLiteral("appearance/wallpaperAppWide"), true).toBool();
    m_settings.gradientEnabled = settings.value(QStringLiteral("appearance/gradientEnabled"), false).toBool();
    m_settings.gradientTop = readColor(settings, QStringLiteral("appearance/gradientTop"));
    m_settings.gradientBottom = readColor(settings, QStringLiteral("appearance/gradientBottom"));
    m_settings.syncDiscordAccent = settings.value(QStringLiteral("appearance/syncDiscordAccent"), false).toBool();

    const int tokenCount = settings.beginReadArray(QStringLiteral("appearance/tokens"));
    for (int i = 0; i < tokenCount; ++i) {
        settings.setArrayIndex(i);
        const QString id = settings.value(QStringLiteral("id")).toString();
        const QColor color(settings.value(QStringLiteral("color")).toString());
        if (!id.isEmpty() && color.isValid())
            m_settings.tokenOverrides.insert(id, color);
    }
    settings.endArray();

    normalize(m_settings);
    reloadWallpaper();
}

void Theme::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/preset"), m_settings.presetId);
    settings.setValue(QStringLiteral("appearance/fontSize"), m_settings.fontSize);
    settings.setValue(QStringLiteral("appearance/radius"), m_settings.radius);
    settings.setValue(QStringLiteral("appearance/uiScale"), m_settings.uiScale);
    settings.setValue(QStringLiteral("appearance/brightness"), m_settings.brightness);
    settings.setValue(QStringLiteral("appearance/saturation"), m_settings.saturation);
    settings.setValue(QStringLiteral("appearance/panelOpacity"), m_settings.panelOpacity);
    settings.setValue(QStringLiteral("appearance/chatDensity"), static_cast<int>(m_settings.chatDensity));
    settings.setValue(QStringLiteral("appearance/fontFamily"), m_settings.fontFamily);
    settings.setValue(QStringLiteral("appearance/wallpaperPath"), m_settings.wallpaperPath);
    settings.setValue(QStringLiteral("appearance/wallpaperOpacity"), m_settings.wallpaperOpacity);
    settings.setValue(QStringLiteral("appearance/wallpaperBlur"), m_settings.wallpaperBlur);
    settings.setValue(QStringLiteral("appearance/wallpaperFrost"), m_settings.wallpaperFrost);
    settings.setValue(QStringLiteral("appearance/wallpaperAppWide"), m_settings.wallpaperAppWide);
    settings.setValue(QStringLiteral("appearance/gradientEnabled"), m_settings.gradientEnabled);
    settings.setValue(QStringLiteral("appearance/syncDiscordAccent"), m_settings.syncDiscordAccent);
    writeColor(settings, QStringLiteral("appearance/customAccent"), m_settings.customAccent);
    writeColor(settings, QStringLiteral("appearance/customBg0"), m_settings.customBg0);
    writeColor(settings, QStringLiteral("appearance/customBg1"), m_settings.customBg1);
    writeColor(settings, QStringLiteral("appearance/customBg2"), m_settings.customBg2);
    writeColor(settings, QStringLiteral("appearance/profilePrimary"), m_settings.profilePrimary);
    writeColor(settings, QStringLiteral("appearance/profileAccent"), m_settings.profileAccent);
    writeColor(settings, QStringLiteral("appearance/gradientTop"), m_settings.gradientTop);
    writeColor(settings, QStringLiteral("appearance/gradientBottom"), m_settings.gradientBottom);

    settings.remove(QStringLiteral("appearance/tokens"));
    settings.beginWriteArray(QStringLiteral("appearance/tokens"), m_settings.tokenOverrides.size());
    int i = 0;
    for (auto it = m_settings.tokenOverrides.begin(); it != m_settings.tokenOverrides.end(); ++it, ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("id"), it.key());
        settings.setValue(QStringLiteral("color"), it.value().name(QColor::HexRgb));
    }
    settings.endArray();
    settings.sync();
}

void Theme::setSettings(Settings settings)
{
    normalize(settings);

    // Skip a full stylesheet rebuild when nothing actually changed (e.g. redundant slider events).
    if (settings.presetId == m_settings.presetId
        && sameColor(settings.customAccent, m_settings.customAccent)
        && sameColor(settings.customBg0, m_settings.customBg0)
        && sameColor(settings.customBg1, m_settings.customBg1)
        && sameColor(settings.customBg2, m_settings.customBg2)
        && sameColor(settings.profilePrimary, m_settings.profilePrimary)
        && sameColor(settings.profileAccent, m_settings.profileAccent)
        && sameTokenMap(settings.tokenOverrides, m_settings.tokenOverrides)
        && settings.fontSize == m_settings.fontSize
        && settings.radius == m_settings.radius
        && settings.uiScale == m_settings.uiScale
        && settings.brightness == m_settings.brightness
        && settings.saturation == m_settings.saturation
        && settings.panelOpacity == m_settings.panelOpacity
        && settings.chatDensity == m_settings.chatDensity
        && settings.fontFamily == m_settings.fontFamily
        && settings.wallpaperPath == m_settings.wallpaperPath
        && settings.wallpaperOpacity == m_settings.wallpaperOpacity
        && settings.wallpaperBlur == m_settings.wallpaperBlur
        && settings.wallpaperFrost == m_settings.wallpaperFrost
        && settings.wallpaperAppWide == m_settings.wallpaperAppWide
        && settings.gradientEnabled == m_settings.gradientEnabled
        && sameColor(settings.gradientTop, m_settings.gradientTop)
        && sameColor(settings.gradientBottom, m_settings.gradientBottom)
        && settings.syncDiscordAccent == m_settings.syncDiscordAccent) {
        return;
    }

    const bool wallpaperChanged = settings.wallpaperPath != m_settings.wallpaperPath
        || settings.wallpaperBlur != m_settings.wallpaperBlur;

    m_settings = std::move(settings);
    if (wallpaperChanged)
        reloadWallpaper();
    save();

    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        apply(*app);
    else {
        m_palette = resolvePalette();
        emit changed();
    }
}

Theme::Palette Theme::resolvePalette() const
{
    Palette palette = findPreset(m_settings.presetId)->palette;
    if (m_settings.customAccent.isValid())
        palette = withAccent(palette, m_settings.customAccent);
    else if (m_settings.syncDiscordAccent && m_discordAccent.isValid())
        palette = withAccent(palette, m_discordAccent);
    if (m_settings.customBg0.isValid())
        palette.bg0 = m_settings.customBg0;
    if (m_settings.customBg1.isValid())
        palette.bg1 = m_settings.customBg1;
    if (m_settings.customBg2.isValid())
        palette.bg2 = m_settings.customBg2;
    for (auto it = m_settings.tokenOverrides.begin(); it != m_settings.tokenOverrides.end(); ++it) {
        if (QColor* field = paletteField(palette, it.key()))
            *field = it.value();
    }

    // Global tone knobs — applied last so presets stay editable, not locked.
    if (m_settings.brightness != 0) {
        const int d = m_settings.brightness;
        palette.bg0 = shiftValue(palette.bg0, d);
        palette.bg1 = shiftValue(palette.bg1, d);
        palette.bg2 = shiftValue(palette.bg2, d);
        palette.bg3 = shiftValue(palette.bg3, d);
        palette.bg4 = shiftValue(palette.bg4, d);
        palette.surface = shiftValue(palette.surface, d);
        palette.hover = shiftValue(palette.hover, d);
        palette.selected = shiftValue(palette.selected, d);
        palette.border = shiftValue(palette.border, d);
        palette.button = shiftValue(palette.button, d);
        palette.buttonHover = shiftValue(palette.buttonHover, d);
    }
    if (m_settings.saturation != 0) {
        const int d = m_settings.saturation;
        palette.accent = shiftSaturation(palette.accent, d);
        palette.accentHover = shiftSaturation(palette.accentHover, d);
        palette.accentMuted = shiftSaturation(palette.accentMuted, d);
        palette.success = shiftSaturation(palette.success, d);
        palette.successHover = shiftSaturation(palette.successHover, d);
        palette.danger = shiftSaturation(palette.danger, d);
        palette.dangerHover = shiftSaturation(palette.dangerHover, d);
        palette.warning = shiftSaturation(palette.warning, d);
        palette.link = shiftSaturation(palette.link, d);
    }
    return palette;
}

void Theme::applyQtPalette(QApplication& app) const
{
    QPalette qt;
    qt.setColor(QPalette::Window, m_palette.bg2);
    qt.setColor(QPalette::WindowText, m_palette.text);
    qt.setColor(QPalette::Base, m_palette.surface);
    qt.setColor(QPalette::AlternateBase, m_palette.bg1);
    qt.setColor(QPalette::Text, m_palette.text);
    qt.setColor(QPalette::PlaceholderText, m_palette.textMuted);
    qt.setColor(QPalette::Button, m_palette.button);
    qt.setColor(QPalette::ButtonText, m_palette.textBright);
    qt.setColor(QPalette::Highlight, m_palette.accent);
    qt.setColor(QPalette::HighlightedText, m_palette.onAccent);
    qt.setColor(QPalette::ToolTipBase, m_palette.bg4);
    qt.setColor(QPalette::ToolTipText, m_palette.text);
    qt.setColor(QPalette::Link, m_palette.link);
    app.setPalette(qt);
}

QString Theme::buildStyleSheet() const
{
    QFile file(QStringLiteral(":/theme/app.qss"));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QString qss = QString::fromUtf8(file.readAll());
    const int font = m_settings.fontSize;
    const auto replace = [&](const char* token, const QString& value) {
        qss.replace(QLatin1String(token), value);
    };

    replace("@bg0Panel", rgba(m_palette.bg0, m_settings.panelOpacity));
    replace("@bg1Panel", rgba(m_palette.bg1, m_settings.panelOpacity));
    replace("@bg0", hex(m_palette.bg0));
    replace("@bg1", hex(m_palette.bg1));
    replace("@bg2", hex(m_palette.bg2));
    replace("@bg3", hex(m_palette.bg3));
    replace("@bg4", hex(m_palette.bg4));
    replace("@surface", hex(m_palette.surface));
    replace("@hover", hex(m_palette.hover));
    replace("@selected", hex(m_palette.selected));
    replace("@border", hex(m_palette.border));
    replace("@textBright", hex(m_palette.textBright));
    replace("@textMuted", hex(m_palette.textMuted));
    replace("@textDim", hex(m_palette.textDim));
    replace("@text", hex(m_palette.text));
    replace("@accentHover", hex(m_palette.accentHover));
    replace("@accentMuted", hex(m_palette.accentMuted));
    replace("@accent", hex(m_palette.accent));
    replace("@buttonHover", hex(m_palette.buttonHover));
    replace("@button", hex(m_palette.button));
    replace("@successHover", hex(m_palette.successHover));
    replace("@success", hex(m_palette.success));
    replace("@dangerHover", hex(m_palette.dangerHover));
    replace("@danger", hex(m_palette.danger));
    replace("@warning", hex(m_palette.warning));
    replace("@link", hex(m_palette.link));
    replace("@onAccent", hex(m_palette.onAccent));
    replace("@profilePrimary", hex(profilePrimary()));
    replace("@profileAccent", hex(profileAccent()));

    const int radius = m_settings.radius;
    const int radiusLg = radius == 0 ? 0 : qMin(24, radius * 2);
    replace("@radiusLg", QString::number(radiusLg) + QStringLiteral("px"));
    replace("@radius", QString::number(radius) + QStringLiteral("px"));

    const int scaled = qMax(10, qRound(font * m_settings.uiScale / 100.0));
    // Longer font tokens first so "@fontSize" does not eat "@fontSizeSm".
    replace("@fontSizeDisplay", QString::number(scaled + 14) + QStringLiteral("px"));
    replace("@fontSizeTitle", QString::number(scaled + 6) + QStringLiteral("px"));
    replace("@fontSizeHero", QString::number(scaled + 10) + QStringLiteral("px"));
    replace("@fontSizeHint", QString::number(scaled - 1) + QStringLiteral("px"));
    replace("@fontSizeSm", QString::number(scaled - 2) + QStringLiteral("px"));
    replace("@fontSizeMd", QString::number(scaled + 1) + QStringLiteral("px"));
    replace("@fontSizeLg", QString::number(scaled + 2) + QStringLiteral("px"));
    replace("@fontSizeXl", QString::number(scaled + 4) + QStringLiteral("px"));
    replace("@fontSize", QString::number(scaled) + QStringLiteral("px"));

    return qss;
}

void Theme::applyAppFont(QApplication& app) const
{
    QFont font = app.font();
    if (m_settings.fontFamily.isEmpty()) {
        font.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("Inter"), QStringLiteral("Segoe UI")});
    } else {
        font.setFamilies({m_settings.fontFamily, QStringLiteral("Noto Sans"), QStringLiteral("Segoe UI")});
    }
    font.setPixelSize(-1); // keep point size from system; QSS drives UI sizes
    app.setFont(font);
}

void Theme::apply(QApplication& app)
{
    m_palette = resolvePalette();
    applyAppFont(app);
    applyQtPalette(app);
    const QString qss = buildStyleSheet();
    if (app.styleSheet() != qss)
        app.setStyleSheet(qss);
    emit changed();
}

QColor Theme::tokenColor(const QString& id) const
{
    if (const QColor* field = paletteField(m_palette, id))
        return *field;
    return {};
}

void Theme::setTokenOverride(const QString& id, const QColor& color)
{
    Settings settings = m_settings;
    if (!color.isValid())
        settings.tokenOverrides.remove(id);
    else
        settings.tokenOverrides.insert(id, color);
    // Keep shortcut fields in sync when editing the common tokens.
    if (id == u"accent")
        settings.customAccent = color;
    else if (id == u"bg0")
        settings.customBg0 = color;
    else if (id == u"bg1")
        settings.customBg1 = color;
    else if (id == u"bg2")
        settings.customBg2 = color;
    setSettings(settings);
}

bool Theme::hasCustomization() const
{
    return m_settings.customAccent.isValid() || m_settings.customBg0.isValid()
        || m_settings.customBg1.isValid() || m_settings.customBg2.isValid()
        || m_settings.profilePrimary.isValid() || m_settings.profileAccent.isValid()
        || !m_settings.tokenOverrides.isEmpty() || !m_settings.wallpaperPath.isEmpty()
        || m_settings.gradientEnabled || m_settings.brightness != 0 || m_settings.saturation != 0
        || m_settings.panelOpacity != 100 || m_settings.radius != 4 || m_settings.uiScale != 100;
}

void Theme::clearCustomization()
{
    Settings settings = m_settings;
    settings.customAccent = {};
    settings.customBg0 = {};
    settings.customBg1 = {};
    settings.customBg2 = {};
    settings.profilePrimary = {};
    settings.profileAccent = {};
    settings.tokenOverrides.clear();
    settings.wallpaperPath.clear();
    settings.wallpaperOpacity = 35;
    settings.wallpaperBlur = 0;
    settings.wallpaperFrost = 25;
    settings.wallpaperAppWide = true;
    settings.gradientEnabled = false;
    settings.brightness = 0;
    settings.saturation = 0;
    settings.panelOpacity = 100;
    settings.radius = 4;
    settings.uiScale = 100;
    settings.syncDiscordAccent = false;
    setSettings(settings);
}

void Theme::reloadWallpaper()
{
    m_wallpaper = {};
    if (m_settings.wallpaperPath.isEmpty())
        return;
    QImage image(m_settings.wallpaperPath);
    if (image.isNull())
        return;
    image = softBlur(image, m_settings.wallpaperBlur);
    m_wallpaper = QPixmap::fromImage(image);
}

QJsonObject Theme::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("presetId"), m_settings.presetId);
    root.insert(QStringLiteral("customAccent"), colorToJson(m_settings.customAccent));
    root.insert(QStringLiteral("customBg0"), colorToJson(m_settings.customBg0));
    root.insert(QStringLiteral("customBg1"), colorToJson(m_settings.customBg1));
    root.insert(QStringLiteral("customBg2"), colorToJson(m_settings.customBg2));
    root.insert(QStringLiteral("profilePrimary"), colorToJson(m_settings.profilePrimary));
    root.insert(QStringLiteral("profileAccent"), colorToJson(m_settings.profileAccent));
    root.insert(QStringLiteral("fontSize"), m_settings.fontSize);
    root.insert(QStringLiteral("radius"), m_settings.radius);
    root.insert(QStringLiteral("uiScale"), m_settings.uiScale);
    root.insert(QStringLiteral("brightness"), m_settings.brightness);
    root.insert(QStringLiteral("saturation"), m_settings.saturation);
    root.insert(QStringLiteral("panelOpacity"), m_settings.panelOpacity);
    root.insert(QStringLiteral("chatDensity"), static_cast<int>(m_settings.chatDensity));
    root.insert(QStringLiteral("fontFamily"), m_settings.fontFamily);
    root.insert(QStringLiteral("wallpaperPath"), m_settings.wallpaperPath);
    root.insert(QStringLiteral("wallpaperOpacity"), m_settings.wallpaperOpacity);
    root.insert(QStringLiteral("wallpaperBlur"), m_settings.wallpaperBlur);
    root.insert(QStringLiteral("wallpaperFrost"), m_settings.wallpaperFrost);
    root.insert(QStringLiteral("wallpaperAppWide"), m_settings.wallpaperAppWide);
    root.insert(QStringLiteral("gradientEnabled"), m_settings.gradientEnabled);
    root.insert(QStringLiteral("gradientTop"), colorToJson(m_settings.gradientTop));
    root.insert(QStringLiteral("gradientBottom"), colorToJson(m_settings.gradientBottom));
    root.insert(QStringLiteral("syncDiscordAccent"), m_settings.syncDiscordAccent);
    QJsonObject tokens;
    for (auto it = m_settings.tokenOverrides.begin(); it != m_settings.tokenOverrides.end(); ++it)
        tokens.insert(it.key(), it.value().name(QColor::HexRgb));
    root.insert(QStringLiteral("tokens"), tokens);
    return root;
}

QString Theme::applyJson(const QJsonObject& json)
{
    if (json.value(QStringLiteral("version")).toInt(1) > 1)
        return QStringLiteral("Unsupported theme file version.");

    Settings settings = m_settings;
    if (json.contains(QStringLiteral("presetId")))
        settings.presetId = json.value(QStringLiteral("presetId")).toString();
    if (json.contains(QStringLiteral("customAccent")))
        settings.customAccent = colorFromJson(json.value(QStringLiteral("customAccent")));
    if (json.contains(QStringLiteral("customBg0")))
        settings.customBg0 = colorFromJson(json.value(QStringLiteral("customBg0")));
    if (json.contains(QStringLiteral("customBg1")))
        settings.customBg1 = colorFromJson(json.value(QStringLiteral("customBg1")));
    if (json.contains(QStringLiteral("customBg2")))
        settings.customBg2 = colorFromJson(json.value(QStringLiteral("customBg2")));
    if (json.contains(QStringLiteral("profilePrimary")))
        settings.profilePrimary = colorFromJson(json.value(QStringLiteral("profilePrimary")));
    if (json.contains(QStringLiteral("profileAccent")))
        settings.profileAccent = colorFromJson(json.value(QStringLiteral("profileAccent")));
    if (json.contains(QStringLiteral("fontSize")))
        settings.fontSize = json.value(QStringLiteral("fontSize")).toInt(settings.fontSize);
    if (json.contains(QStringLiteral("radius")))
        settings.radius = json.value(QStringLiteral("radius")).toInt(settings.radius);
    if (json.contains(QStringLiteral("uiScale")))
        settings.uiScale = json.value(QStringLiteral("uiScale")).toInt(settings.uiScale);
    if (json.contains(QStringLiteral("brightness")))
        settings.brightness = json.value(QStringLiteral("brightness")).toInt(settings.brightness);
    if (json.contains(QStringLiteral("saturation")))
        settings.saturation = json.value(QStringLiteral("saturation")).toInt(settings.saturation);
    if (json.contains(QStringLiteral("panelOpacity")))
        settings.panelOpacity = json.value(QStringLiteral("panelOpacity")).toInt(settings.panelOpacity);
    if (json.contains(QStringLiteral("chatDensity")))
        settings.chatDensity = static_cast<ChatDensity>(json.value(QStringLiteral("chatDensity")).toInt());
    if (json.contains(QStringLiteral("fontFamily")))
        settings.fontFamily = json.value(QStringLiteral("fontFamily")).toString();
    if (json.contains(QStringLiteral("wallpaperPath")))
        settings.wallpaperPath = json.value(QStringLiteral("wallpaperPath")).toString();
    if (json.contains(QStringLiteral("wallpaperOpacity")))
        settings.wallpaperOpacity = json.value(QStringLiteral("wallpaperOpacity")).toInt();
    if (json.contains(QStringLiteral("wallpaperBlur")))
        settings.wallpaperBlur = json.value(QStringLiteral("wallpaperBlur")).toInt();
    if (json.contains(QStringLiteral("wallpaperFrost")))
        settings.wallpaperFrost = json.value(QStringLiteral("wallpaperFrost")).toInt();
    if (json.contains(QStringLiteral("wallpaperAppWide")))
        settings.wallpaperAppWide = json.value(QStringLiteral("wallpaperAppWide")).toBool();
    if (json.contains(QStringLiteral("gradientEnabled")))
        settings.gradientEnabled = json.value(QStringLiteral("gradientEnabled")).toBool();
    if (json.contains(QStringLiteral("gradientTop")))
        settings.gradientTop = colorFromJson(json.value(QStringLiteral("gradientTop")));
    if (json.contains(QStringLiteral("gradientBottom")))
        settings.gradientBottom = colorFromJson(json.value(QStringLiteral("gradientBottom")));
    if (json.contains(QStringLiteral("syncDiscordAccent")))
        settings.syncDiscordAccent = json.value(QStringLiteral("syncDiscordAccent")).toBool();
    if (json.contains(QStringLiteral("tokens"))) {
        settings.tokenOverrides.clear();
        const QJsonObject tokens = json.value(QStringLiteral("tokens")).toObject();
        for (auto it = tokens.begin(); it != tokens.end(); ++it) {
            const QColor color(it.value().toString());
            if (color.isValid())
                settings.tokenOverrides.insert(it.key(), color);
        }
    }

    setSettings(settings);
    return {};
}

QString Theme::exportToFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Could not write theme file.");
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return {};
}

QString Theme::importFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not read theme file.");
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return QStringLiteral("Invalid theme JSON.");
    return applyJson(doc.object());
}

void Theme::setDiscordProfileColors(const QColor& accent, const QColor& banner)
{
    m_discordAccent = accent;
    m_discordBanner = banner;
    if (!m_settings.syncDiscordAccent)
        return;
    Settings settings = m_settings;
    if (accent.isValid())
        settings.customAccent = accent;
    if (banner.isValid() && !settings.profilePrimary.isValid())
        settings.profilePrimary = banner;
    setSettings(settings);
}
