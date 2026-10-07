#include "Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QPalette>
#include <QSettings>
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

QColor Theme::profilePrimary() const
{
    return m_settings.profilePrimary.isValid() ? m_settings.profilePrimary : m_palette.bg3;
}

QColor Theme::profileAccent() const
{
    return m_settings.profileAccent.isValid() ? m_settings.profileAccent : m_palette.accent;
}

void Theme::load()
{
    const QSettings settings;
    m_settings.presetId = settings.value(QStringLiteral("appearance/preset"), QStringLiteral("discord")).toString();
    m_settings.customAccent = readColor(settings, QStringLiteral("appearance/customAccent"));

    // Former Rose/Emerald/Sunset/Ocean presets are now accent chips on Discord.
    if (const QColor legacy = accentForLegacyPreset(m_settings.presetId); legacy.isValid()) {
        if (!m_settings.customAccent.isValid())
            m_settings.customAccent = legacy;
        m_settings.presetId = QStringLiteral("discord");
    }

    if (findPreset(m_settings.presetId)->id != m_settings.presetId)
        m_settings.presetId = QStringLiteral("discord");
    m_settings.profilePrimary = readColor(settings, QStringLiteral("appearance/profilePrimary"));
    m_settings.profileAccent = readColor(settings, QStringLiteral("appearance/profileAccent"));
    m_settings.fontSize = qBound(12, settings.value(QStringLiteral("appearance/fontSize"), 14).toInt(), 18);
}

void Theme::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/preset"), m_settings.presetId);
    settings.setValue(QStringLiteral("appearance/fontSize"), m_settings.fontSize);
    writeColor(settings, QStringLiteral("appearance/customAccent"), m_settings.customAccent);
    writeColor(settings, QStringLiteral("appearance/profilePrimary"), m_settings.profilePrimary);
    writeColor(settings, QStringLiteral("appearance/profileAccent"), m_settings.profileAccent);
    settings.sync();
}

void Theme::setSettings(Settings settings)
{
    settings.fontSize = qBound(12, settings.fontSize, 18);
    if (findPreset(settings.presetId)->id != settings.presetId)
        settings.presetId = QStringLiteral("discord");

    const auto sameColor = [](const QColor& a, const QColor& b) {
        if (!a.isValid() && !b.isValid())
            return true;
        if (!a.isValid() || !b.isValid())
            return false;
        return a.rgb() == b.rgb();
    };

    // Skip a full stylesheet rebuild when nothing actually changed (e.g. redundant slider events).
    if (settings.presetId == m_settings.presetId
        && sameColor(settings.customAccent, m_settings.customAccent)
        && sameColor(settings.profilePrimary, m_settings.profilePrimary)
        && sameColor(settings.profileAccent, m_settings.profileAccent)
        && settings.fontSize == m_settings.fontSize) {
        return;
    }

    m_settings = std::move(settings);
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

void Theme::apply(QApplication& app)
{
    m_palette = resolvePalette();
    applyQtPalette(app);
    const QString qss = buildStyleSheet();
    if (app.styleSheet() != qss)
        app.setStyleSheet(qss);
    emit changed();
}
