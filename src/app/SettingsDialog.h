#pragma once

#include "Theme.h"
#include "voice/VoiceSettings.h"

#include <QDialog>
#include <QPushButton>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class AudioEngine;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QTimer;
class VoiceController;

// Microphone level bar that doubles as the voice activity sensitivity control (drag to set the threshold).
class LevelMeter : public QWidget
{
    Q_OBJECT

public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void setLevel(float db);
    void setThreshold(float db);
    float threshold() const { return m_threshold; }
    void setThresholdVisible(bool visible);

    QSize sizeHint() const override { return {300, 24}; }

signals:
    void thresholdChanged(float db);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    float dbAt(int x) const;

    float m_level = -100.0f;
    float m_threshold = -50.0f;
    bool m_thresholdVisible = true;
};

// Button that records the next key or mouse button press as the push-to-talk key.
class KeybindButton : public QPushButton
{
    Q_OBJECT

public:
    explicit KeybindButton(QWidget* parent = nullptr);

    void setKey(int nativeKey);
    int key() const { return m_key; }

signals:
    void keyChanged(int nativeKey);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void stopRecording();
    void updateText();

    int m_key = 0;
    bool m_recording = false;
};

// Solid color chip for Appearance. Painted directly so global QSS cannot wipe the fill.
class ColorSwatch : public QPushButton
{
    Q_OBJECT

public:
    explicit ColorSwatch(QWidget* parent = nullptr);

    void setSwatchColor(const QColor& color);
    QColor swatchColor() const { return m_color; }
    void setSelectedSwatch(bool selected);
    bool isSelectedSwatch() const { return m_selected; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor m_color{Qt::black};
    bool m_selected = false;
};

// Theme preset card with a tiny rail / sidebar / chat preview.
class PresetCard : public QPushButton
{
    Q_OBJECT

public:
    PresetCard(const Theme::Preset& preset, const QString& title, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Theme::Palette m_palette;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(VoiceController* voice, QWidget* parent = nullptr);
    ~SettingsDialog() override;

signals:
    void logoutRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QWidget* buildVoicePage();
    QWidget* buildAppearancePage();
    QWidget* buildNotificationsPage();
    QWidget* buildLanguagePage();
    void apply();
    void applyAppearance();
    void updateModeWidgets();
    void startMicTest();
    void stopMicTest();
    void changeLanguage(const QString& code);
    void refreshColorSwatches();
    void setCustomAccent(const QColor& color);
    void pickColor(const QColor& initial, const QString& title, const std::function<void(QColor)>& onPicked);
    static QString presetDisplayName(const QString& id, const QString& fallback);

    VoiceController* m_voice;
    VoiceSettings m_settings;

    QComboBox* m_inputDevice = nullptr;
    QComboBox* m_outputDevice = nullptr;
    QSlider* m_inputVolume = nullptr;
    QSlider* m_outputVolume = nullptr;
    QRadioButton* m_voiceActivity = nullptr;
    QRadioButton* m_pushToTalk = nullptr;
    LevelMeter* m_meter = nullptr;
    QWidget* m_sensitivityGroup = nullptr;
    QWidget* m_pushToTalkGroup = nullptr;
    KeybindButton* m_keybind = nullptr;
    QSlider* m_releaseDelay = nullptr;
    QLabel* m_releaseDelayLabel = nullptr;
    QCheckBox* m_automaticSensitivity = nullptr;
    QCheckBox* m_noiseSuppression = nullptr;
    QCheckBox* m_echoCancellation = nullptr;
    QCheckBox* m_automaticGainControl = nullptr;
    QCheckBox* m_soundEffects = nullptr;

    QSlider* m_fontSize = nullptr;
    QLabel* m_fontSizeLabel = nullptr;
    QComboBox* m_fontFamily = nullptr;
    QComboBox* m_radius = nullptr;
    QComboBox* m_chatDensity = nullptr;
    ColorSwatch* m_accentSwatch = nullptr;
    ColorSwatch* m_bg0Swatch = nullptr;
    ColorSwatch* m_bg1Swatch = nullptr;
    ColorSwatch* m_bg2Swatch = nullptr;
    ColorSwatch* m_profilePrimarySwatch = nullptr;
    ColorSwatch* m_profileAccentSwatch = nullptr;
    QButtonGroup* m_presetGroup = nullptr;
    QVector<ColorSwatch*> m_accentChips;

    QTimer* m_meterTimer;
    std::unique_ptr<AudioEngine> m_testAudio;
    std::atomic<float> m_testLevelDb{-100.0f};
};
