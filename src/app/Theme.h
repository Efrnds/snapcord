#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QApplication;

// Lightweight appearance system: solid-color palettes + one accent, applied through QSS
// token substitution and a shared Palette for hand-painted widgets. No images, no blur.
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
        int fontSize = 14;        // 12–18
        int radius = 4;           // 0 / 4 / 8
        ChatDensity chatDensity = ChatDensity::Normal;
        QString fontFamily;       // empty => default stack (Noto/Inter/Segoe)
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
    // Quick accent chips shown in Appearance (includes former accent-only presets).
    static QVector<QColor> accentSwatches();
    // Short list of open fonts shipped or commonly available; empty id = default stack.
    static QStringList fontFamilyChoices();

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

    Settings m_settings;
    Palette m_palette;
};
