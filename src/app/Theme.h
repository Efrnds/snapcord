#pragma once

#include <QColor>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QApplication;
class QJsonObject;

// Appearance system: palettes + optional chat gradient, applied through QSS tokens
// and a shared Palette for hand-painted widgets.
class Theme : public QObject
{
    Q_OBJECT

public:
    enum class ChatDensity {
        Compact = 0,
        Normal = 1,
        Comfortable = 2,
    };

    struct Palette {
        QColor bg0;       // server rail / deepest
        QColor bg1;       // channel sidebar
        QColor bg2;       // main / chat
        QColor bg3;       // user & voice panels
        QColor bg4;       // menus / popups
        QColor surface;   // inputs, combo boxes
        QColor hover;
        QColor selected;
        QColor border;
        QColor text;
        QColor textBright;
        QColor textMuted;
        QColor textDim;
        QColor accent;
        QColor accentHover;
        QColor accentMuted;
        QColor button;
        QColor buttonHover;
        QColor success;
        QColor successHover;
        QColor danger;
        QColor dangerHover;
        QColor warning;
        QColor link;
        QColor onAccent; // text/icons on accent, success, and danger fills
    };

    struct Preset {
        QString id;
        QString name; // English source string; callers wrap with tr() when showing UI
        Palette palette;
    };

    struct Settings {
        QString presetId = QStringLiteral("discord");
        QColor customAccent;      // invalid => use preset accent
        QColor customBg0;         // invalid => use preset
        QColor customBg1;
        QColor customBg2;
        QColor profilePrimary;    // invalid => use palette.bg3
        QColor profileAccent;     // invalid => use effective accent
        QMap<QString, QColor> tokenOverrides; // advanced per-token colors (bg3, text, …)
        int fontSize = 14;        // 12–18
        int radius = 4;           // 0–12 (Fusion squares corners when radius ≥ half widget height)
        int uiScale = 100;        // 85–130; each step = ±1px on the base font size
        int brightness = 0;       // -40..40, shifts background values
        int saturation = 0;       // -50..50, shifts accent/status saturation
        ChatDensity chatDensity = ChatDensity::Normal;
        QString fontFamily;       // empty => default stack (Noto/Inter/Segoe)
        bool gradientEnabled = false;
        QColor gradientTop;
        QColor gradientBottom;
        bool syncDiscordAccent = false;
    };

    static Theme& instance();

    const Settings& settings() const { return m_settings; }
    const Palette& palette() const { return m_palette; }
    QColor accent() const { return m_palette.accent; }
    QColor profilePrimary() const;
    QColor profileAccent() const;

    // Extra vertical gap before a message that starts a group (avatar + name).
    int messageGroupGap() const;
    // Extra vertical gap between consecutive messages in the same group.
    int messageTightGap() const;

    static QVector<Preset> presets();
    static const Preset* findPreset(const QString& id);
    static QVector<QColor> accentSwatches();
    static QStringList fontFamilyChoices();
    // Ordered token ids matching Palette fields (for the advanced editor + JSON).
    static QStringList tokenIds();
    static QString tokenLabel(const QString& id);

    QColor tokenColor(const QString& id) const;
    void setTokenOverride(const QString& id, const QColor& color); // invalid clears
    bool hasCustomization() const;
    void clearCustomization(); // keeps preset + font prefs, drops color/gradient overrides

    QJsonObject toJson() const;
    // Returns an empty string on success, or a short error message.
    QString applyJson(const QJsonObject& json);
    QString exportToFile(const QString& path) const;
    QString importFromFile(const QString& path);

    // Called when Discord user profile colors are known (accent_color / banner_color).
    void setDiscordProfileColors(const QColor& accent, const QColor& banner);

    void load();
    void save() const;
    void setSettings(Settings settings);
    void apply(QApplication& app);

signals:
    void changed();

private:
    explicit Theme(QObject* parent = nullptr);

    Palette resolvePalette() const;
    QString buildStyleSheet() const;
    void applyQtPalette(QApplication& app) const;
    void applyAppFont(QApplication& app) const;
    void normalize(Settings& settings) const;

    Settings m_settings;
    Palette m_palette;
    QColor m_discordAccent;
    QColor m_discordBanner;
};
