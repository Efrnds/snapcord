#pragma once

#include "voice/VoiceSettings.h"

#include <QDialog>
#include <QPushButton>
#include <QWidget>

#include <atomic>
#include <memory>

class AudioEngine;
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
    QWidget* buildNotificationsPage();
    QWidget* buildLanguagePage();
    void apply();
    void updateModeWidgets();
    void startMicTest();
    void stopMicTest();
    void changeLanguage(const QString& code);

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

    QTimer* m_meterTimer;
    std::unique_ptr<AudioEngine> m_testAudio;
    std::atomic<float> m_testLevelDb{-100.0f};
};
