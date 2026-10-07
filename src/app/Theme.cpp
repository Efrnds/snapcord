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
    settings.radius = (settings.radius == 0 || settings.radius == 8) ? settings.radius : 4;
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
    m_settings.chatDensity = static_cast<ChatDensity>(
        settings.value(QStringLiteral("appearance/chatDensity"), static_cast<int>(ChatDensity::Normal)).toInt());
    m_settings.fontFamily = settings.value(QStringLiteral("appearance/fontFamily")).toString();
    m_settings.wallpaperPath = settings.value(QStringLiteral("appearance/wallpaperPath")).toString();
    m_settings.wallpaperOpacity = settings.value(QStringLiteral("appearance/wallpaperOpacity"), 35).toInt();
    m_settings.wallpaperBlur = settings.value(QStringLiteral("appearance/wallpaperBlur"), 0).toInt();
    m_settings.wallpaperFrost = settings.value(QStringLiteral("appearance/wallpaperFrost"), 25).toInt();
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
    settings.setValue(QStringLiteral("appearance/chatDensity"), static_cast<int>(m_settings.chatDensity));
    settings.setValue(QStringLiteral("appearance/fontFamily"), m_settings.fontFamily);
    settings.setValue(QStringLiteral("appearance/wallpaperPath"), m_settings.wallpaperPath);
    settings.setValue(QStringLiteral("appearance/wallpaperOpacity"), m_settings.wallpaperOpacity);
    settings.setValue(QStringLiteral("appearance/wallpaperBlur"), m_settings.wallpaperBlur);
    settings.setValue(QStringLiteral("appearance/wallpaperFrost"), m_settings.wallpaperFrost);
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
        && settings.chatDensity == m_settings.chatDensity
        && settings.fontFamily == m_settings.fontFamily
        && settings.wallpaperPath == m_settings.wallpaperPath
        && settings.wallpaperOpacity == m_settings.wallpaperOpacity
        && settings.wallpaperBlur == m_settings.wallpaperBlur
        && settings.wallpaperFrost == m_settings.wallpaperFrost
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
    const int radiusLg = radius == 0 ? 0 : radius * 2;
    replace("@radiusLg", QString::number(radiusLg) + QStringLiteral("px"));
    replace("@radius", QString::number(radius) + QStringLiteral("px"));

    // Longer font tokens first so "@fontSize" does not eat "@fontSizeSm".
    replace("@fontSizeDisplay", QString::number(font + 14) + QStringLiteral("px"));
    replace("@fontSizeTitle", QString::number(font + 6) + QStringLiteral("px"));
    replace("@fontSizeHero", QString::number(font + 10) + QStringLiteral("px"));
    replace("@fontSizeHint", QString::number(font - 1) + QStringLiteral("px"));
    replace("@fontSizeSm", QString::number(font - 2) + QStringLiteral("px"));
    replace("@fontSizeMd", QString::number(font + 1) + QStringLiteral("px"));
    replace("@fontSizeLg", QString::number(font + 2) + QStringLiteral("px"));
    replace("@fontSizeXl", QString::number(font + 4) + QStringLiteral("px"));
    replace("@fontSize", QString::number(font) + QStringLiteral("px"));

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
    root.insert(QStringLiteral("chatDensity"), static_cast<int>(m_settings.chatDensity));
    root.insert(QStringLiteral("fontFamily"), m_settings.fontFamily);
    root.insert(QStringLiteral("wallpaperPath"), m_settings.wallpaperPath);
    root.insert(QStringLiteral("wallpaperOpacity"), m_settings.wallpaperOpacity);
    root.insert(QStringLiteral("wallpaperBlur"), m_settings.wallpaperBlur);
    root.insert(QStringLiteral("wallpaperFrost"), m_settings.wallpaperFrost);
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
