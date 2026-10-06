#include "SettingsDialog.h"

#include "Language.h"
#include "VoiceController.h"
#include "platform/KeyState.h"
#include "voice/AudioEngine.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr float MinDb = -100.0f;

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QStringLiteral("settingsSection"));
    return label;
}

// A checkbox with a muted explanation underneath, like Discord's settings toggles.
QWidget* option(QCheckBox* checkBox, const QString& hint)
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(2);
    layout->addWidget(checkBox);
    auto* label = new QLabel(hint);
    label->setObjectName(QStringLiteral("settingsHint"));
    label->setWordWrap(true);
    label->setContentsMargins(26, 0, 0, 0);
    layout->addWidget(label);
    return widget;
}

QSlider* volumeSlider(float value)
{
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 200);
    slider->setValue(qRound(value * 100));
    slider->setToolTip(QStringLiteral("%1%").arg(slider->value()));
    QObject::connect(slider, &QSlider::valueChanged, slider,
                     [slider](int v) { slider->setToolTip(QStringLiteral("%1%").arg(v)); });
    return slider;
}

} // namespace

// --- LevelMeter -------------------------------------------------------------------------------------

LevelMeter::LevelMeter(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(24);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Drag to set how loud you need to be for your microphone to activate."));
}

void LevelMeter::setLevel(float db)
{
    if (std::abs(db - m_level) < 0.5f)
        return;
    m_level = db;
    update();
}

void LevelMeter::setThreshold(float db)
{
    m_threshold = qBound(MinDb, db, 0.0f);
    update();
}

void LevelMeter::setThresholdVisible(bool visible)
{
    m_thresholdVisible = visible;
    update();
}

float LevelMeter::dbAt(int x) const
{
    return MinDb + (qBound(0, x, width()) / float(width())) * -MinDb;
}

void LevelMeter::mousePressEvent(QMouseEvent* event)
{
    if (!m_thresholdVisible)
        return;
    setThreshold(dbAt(event->position().toPoint().x()));
    emit thresholdChanged(m_threshold);
}

void LevelMeter::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_thresholdVisible || !(event->buttons() & Qt::LeftButton))
        return;
    setThreshold(dbAt(event->position().toPoint().x()));
    emit thresholdChanged(m_threshold);
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bar = QRectF(rect()).adjusted(0, 6, 0, -6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x4e, 0x50, 0x58));
    painter.drawRoundedRect(bar, 4, 4);

    const qreal levelX = bar.width() * (qBound(MinDb, m_level, 0.0f) - MinDb) / -MinDb;
    const bool active = !m_thresholdVisible || m_level >= m_threshold;
    painter.setBrush(active ? QColor(0x23, 0xa5, 0x59) : QColor(0xf0, 0xb2, 0x32));
    painter.drawRoundedRect(QRectF(bar.left(), bar.top(), levelX, bar.height()), 4, 4);

    if (m_thresholdVisible) {
        const qreal thresholdX = bar.width() * (m_threshold - MinDb) / -MinDb;
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(thresholdX - 3, 0, 6, height()), 3, 3);
    }
}

// --- KeybindButton ----------------------------------------------------------------------------------

KeybindButton::KeybindButton(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("secondaryButton"));
    setCursor(Qt::PointingHandCursor);
    setMinimumWidth(200);
    connect(this, &QPushButton::clicked, this, [this] {
        m_recording = true;
        updateText();
        setFocus();
    });
    updateText();
}

void KeybindButton::setKey(int nativeKey)
{
    m_key = nativeKey;
    updateText();
}

void KeybindButton::keyPressEvent(QKeyEvent* event)
{
    if (!m_recording) {
        QPushButton::keyPressEvent(event);
        return;
    }
    if (event->key() != Qt::Key_Escape) {
        const int key = static_cast<int>(event->nativeVirtualKey());
        if (key != 0) {
            m_key = key;
            emit keyChanged(m_key);
        }
    }
    stopRecording();
}

void KeybindButton::mousePressEvent(QMouseEvent* event)
{
    if (m_recording) {
        const int key = KeyState::mouseButtonKey(event->button());
        if (key != 0) {
            m_key = key;
            emit keyChanged(m_key);
            stopRecording();
            return;
        }
    }
    QPushButton::mousePressEvent(event);
}

void KeybindButton::focusOutEvent(QFocusEvent* event)
{
    stopRecording();
    QPushButton::focusOutEvent(event);
}

void KeybindButton::stopRecording()
{
    m_recording = false;
    updateText();
}

void KeybindButton::updateText()
{
    if (m_recording)
        setText(tr("Press a key…"));
    else
        setText(m_key == 0 ? tr("Record Keybind") : KeyState::name(m_key));
}

// --- SettingsDialog ---------------------------------------------------------------------------------

SettingsDialog::SettingsDialog(VoiceController* voice, QWidget* parent)
    : QDialog(parent)
    , m_voice(voice)
    , m_settings(VoiceSettings::load())
    , m_meterTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("User Settings"));
    resize(860, 640);

    auto* navigation = new QListWidget;
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setFixedWidth(200);
    navigation->setFocusPolicy(Qt::NoFocus);
    navigation->addItem(tr("Voice & Audio"));
    navigation->addItem(tr("Language"));

    auto* logout = new QPushButton(tr("Log Out"));
    logout->setObjectName(QStringLiteral("dangerButton"));
    logout->setCursor(Qt::PointingHandCursor);
    connect(logout, &QPushButton::clicked, this, [this] {
        accept();
        emit logoutRequested();
    });

    auto* side = new QWidget;
    side->setObjectName(QStringLiteral("settingsSide"));
    side->setAttribute(Qt::WA_StyledBackground);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(12, 24, 12, 16);
    sideLayout->addWidget(navigation, 1);
    sideLayout->addWidget(logout);

    auto* pages = new QStackedWidget;
    pages->addWidget(buildVoicePage());
    pages->addWidget(buildLanguagePage());
    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(side);
    layout->addWidget(pages, 1);

    m_meterTimer->setInterval(50);
    connect(m_meterTimer, &QTimer::timeout, this, [this] {
        const bool inCall = m_voice->state() == VoiceConnection::State::Connected;
        m_meter->setLevel(inCall ? m_voice->connection()->inputLevelDb() : m_testLevelDb.load());
    });
}

SettingsDialog::~SettingsDialog()
{
    stopMicTest();
}

QWidget* SettingsDialog::buildVoicePage()
{
    AudioEngine& audio = m_voice->connection()->audio();

    m_inputDevice = new QComboBox;
    m_inputDevice->addItem(tr("Default"), QString());
    for (const QString& name : audio.inputDevices())
        m_inputDevice->addItem(name, name);
    m_inputDevice->setCurrentIndex(qMax(0, m_inputDevice->findData(m_settings.inputDevice)));

    m_outputDevice = new QComboBox;
    m_outputDevice->addItem(tr("Default"), QString());
    for (const QString& name : audio.outputDevices())
        m_outputDevice->addItem(name, name);
    m_outputDevice->setCurrentIndex(qMax(0, m_outputDevice->findData(m_settings.outputDevice)));

    m_inputVolume = volumeSlider(m_settings.inputVolume);
    m_outputVolume = volumeSlider(m_settings.outputVolume);

    m_voiceActivity = new QRadioButton(tr("Voice Activity"));
    m_pushToTalk = new QRadioButton(tr("Push to Talk"));
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_voiceActivity);
    modeGroup->addButton(m_pushToTalk);
    (m_settings.inputMode == VoiceSettings::InputMode::PushToTalk ? m_pushToTalk : m_voiceActivity)->setChecked(true);

    m_meter = new LevelMeter;
    m_meter->setThreshold(m_settings.activationThresholdDb);
    m_automaticSensitivity = new QCheckBox(tr("Automatically determine input sensitivity"));
    m_automaticSensitivity->setChecked(m_settings.automaticSensitivity);

    m_noiseSuppression = new QCheckBox(tr("Noise Suppression"));
    m_noiseSuppression->setChecked(m_settings.noiseSuppression);
    m_echoCancellation = new QCheckBox(tr("Echo Cancellation"));
    m_echoCancellation->setChecked(m_settings.echoCancellation);
    m_automaticGainControl = new QCheckBox(tr("Automatic Gain Control"));
    m_automaticGainControl->setChecked(m_settings.automaticGainControl);
    m_soundEffects = new QCheckBox(tr("Play sound effects"));
    m_soundEffects->setChecked(m_settings.soundEffects);

    m_keybind = new KeybindButton;
    m_keybind->setKey(m_settings.pushToTalkKey);
    m_releaseDelay = new QSlider(Qt::Horizontal);
    m_releaseDelay->setRange(0, 2000);
    m_releaseDelay->setSingleStep(20);
    m_releaseDelay->setValue(m_settings.pushToTalkReleaseMs);
    m_releaseDelayLabel = new QLabel;
    m_releaseDelayLabel->setObjectName(QStringLiteral("settingsHint"));

    // Layout.
    auto* devices = new QHBoxLayout;
    devices->setSpacing(16);
    auto* inputColumn = new QVBoxLayout;
    inputColumn->addWidget(sectionLabel(tr("Input Device")));
    inputColumn->addWidget(m_inputDevice);
    inputColumn->addSpacing(8);
    inputColumn->addWidget(sectionLabel(tr("Input Volume")));
    inputColumn->addWidget(m_inputVolume);
    auto* outputColumn = new QVBoxLayout;
    outputColumn->addWidget(sectionLabel(tr("Output Device")));
    outputColumn->addWidget(m_outputDevice);
    outputColumn->addSpacing(8);
    outputColumn->addWidget(sectionLabel(tr("Output Volume")));
    outputColumn->addWidget(m_outputVolume);
    devices->addLayout(inputColumn, 1);
    devices->addLayout(outputColumn, 1);

    m_sensitivityGroup = new QWidget;
    auto* sensitivityLayout = new QVBoxLayout(m_sensitivityGroup);
    sensitivityLayout->setContentsMargins(0, 0, 0, 0);
    sensitivityLayout->addWidget(sectionLabel(tr("Input Sensitivity")));
    sensitivityLayout->addWidget(m_automaticSensitivity);
    auto* sensitivityHint = new QLabel(tr("Talk to test your microphone. The bar turns green when you are loud "
                                          "enough to be heard; drag the white marker to adjust."));
    sensitivityHint->setObjectName(QStringLiteral("settingsHint"));
    sensitivityHint->setWordWrap(true);
    sensitivityLayout->addWidget(sensitivityHint);
    sensitivityLayout->addWidget(m_meter);

    m_pushToTalkGroup = new QWidget;
    auto* pttLayout = new QVBoxLayout(m_pushToTalkGroup);
    pttLayout->setContentsMargins(0, 0, 0, 0);
    auto* pttRow = new QHBoxLayout;
    auto* shortcutColumn = new QVBoxLayout;
    shortcutColumn->addWidget(sectionLabel(tr("Shortcut")));
    shortcutColumn->addWidget(m_keybind);
    auto* delayColumn = new QVBoxLayout;
    delayColumn->addWidget(sectionLabel(tr("Push to Talk Release Delay")));
    delayColumn->addWidget(m_releaseDelay);
    delayColumn->addWidget(m_releaseDelayLabel);
    pttRow->addLayout(shortcutColumn);
    pttRow->addSpacing(16);
    pttRow->addLayout(delayColumn, 1);
    pttLayout->addLayout(pttRow);
    if (!KeyState::isSupported()) {
        auto* unsupported = new QLabel(tr("Push to Talk is not available on this system yet."));
        unsupported->setObjectName(QStringLiteral("settingsHint"));
        pttLayout->addWidget(unsupported);
    }

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("settingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(12);
    auto* title = new QLabel(tr("Voice & Audio"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addLayout(devices);
    layout->addSpacing(16);
    layout->addWidget(sectionLabel(tr("Input Mode")));
    layout->addWidget(m_voiceActivity);
    layout->addWidget(m_pushToTalk);
    layout->addSpacing(16);
    layout->addWidget(m_sensitivityGroup);
    layout->addWidget(m_pushToTalkGroup);
    layout->addSpacing(16);
    layout->addWidget(sectionLabel(tr("Voice Processing")));
    layout->addWidget(option(m_noiseSuppression, tr("Removes background noise like keyboards, fans and traffic.")));
    layout->addWidget(option(m_echoCancellation,
                             tr("Stops others from hearing themselves when you use speakers instead of headphones.")));
    layout->addWidget(option(m_automaticGainControl, tr("Keeps your voice at a steady volume.")));
    layout->addSpacing(16);
    layout->addWidget(sectionLabel(tr("Sounds")));
    layout->addWidget(option(m_soundEffects, tr("Joining, leaving, muting and incoming calls.")));
    layout->addStretch();

    connect(m_inputDevice, &QComboBox::currentIndexChanged, this, [this] {
        apply();
        if (m_testAudio)
            startMicTest();
    });
    connect(m_outputDevice, &QComboBox::currentIndexChanged, this, &SettingsDialog::apply);
    connect(m_inputVolume, &QSlider::valueChanged, this, &SettingsDialog::apply);
    connect(m_outputVolume, &QSlider::valueChanged, this, &SettingsDialog::apply);
    connect(m_voiceActivity, &QRadioButton::toggled, this, [this] {
        updateModeWidgets();
        apply();
    });
    connect(m_meter, &LevelMeter::thresholdChanged, this, &SettingsDialog::apply);
    connect(m_automaticSensitivity, &QCheckBox::toggled, this, [this] {
        updateModeWidgets();
        apply();
    });
    for (QCheckBox* checkBox : {m_noiseSuppression, m_echoCancellation, m_automaticGainControl, m_soundEffects})
        connect(checkBox, &QCheckBox::toggled, this, &SettingsDialog::apply);
    connect(m_keybind, &KeybindButton::keyChanged, this, &SettingsDialog::apply);
    connect(m_releaseDelay, &QSlider::valueChanged, this, [this] {
        m_releaseDelayLabel->setText(tr("%1 ms").arg(m_releaseDelay->value()));
        apply();
    });
    m_releaseDelayLabel->setText(tr("%1 ms").arg(m_releaseDelay->value()));
    updateModeWidgets();

    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget* SettingsDialog::buildLanguagePage()
{
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("settingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(12);
    auto* title = new QLabel(tr("Language"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(sectionLabel(tr("Select a language")));

    auto* group = new QButtonGroup(content);
    const QString current = Language::current();
    for (const auto& [code, name] : Language::available()) {
        auto* option = new QRadioButton(name);
        option->setChecked(code == current);
        group->addButton(option);
        layout->addWidget(option);
        connect(option, &QRadioButton::toggled, this, [this, code](bool checked) {
            if (checked)
                changeLanguage(code);
        });
    }
    layout->addStretch();
    return content;
}

void SettingsDialog::apply()
{
    m_settings.inputDevice = m_inputDevice->currentData().toString();
    m_settings.outputDevice = m_outputDevice->currentData().toString();
    m_settings.inputVolume = m_inputVolume->value() / 100.0f;
    m_settings.outputVolume = m_outputVolume->value() / 100.0f;
    m_settings.inputMode = m_pushToTalk->isChecked() ? VoiceSettings::InputMode::PushToTalk
                                                     : VoiceSettings::InputMode::VoiceActivity;
    m_settings.activationThresholdDb = m_meter->threshold();
    m_settings.pushToTalkKey = m_keybind->key();
    m_settings.pushToTalkReleaseMs = m_releaseDelay->value();
    m_settings.automaticSensitivity = m_automaticSensitivity->isChecked();
    m_settings.noiseSuppression = m_noiseSuppression->isChecked();
    m_settings.echoCancellation = m_echoCancellation->isChecked();
    m_settings.automaticGainControl = m_automaticGainControl->isChecked();
    m_settings.soundEffects = m_soundEffects->isChecked();
    m_voice->applySettings(m_settings);
}

void SettingsDialog::updateModeWidgets()
{
    const bool pushToTalk = m_pushToTalk->isChecked();
    m_pushToTalkGroup->setVisible(pushToTalk);
    m_automaticSensitivity->setVisible(!pushToTalk);
    // With automatic sensitivity, voice detection decides; the meter only shows the level.
    m_meter->setThresholdVisible(!pushToTalk && !m_automaticSensitivity->isChecked());
}

void SettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // Outside of a call, open the microphone just for the level meter while the dialog is visible.
    if (m_voice->state() != VoiceConnection::State::Connected)
        startMicTest();
    m_meterTimer->start();
}

void SettingsDialog::hideEvent(QHideEvent* event)
{
    m_meterTimer->stop();
    stopMicTest();
    QDialog::hideEvent(event);
}

void SettingsDialog::startMicTest()
{
    if (!m_testAudio)
        m_testAudio = std::make_unique<AudioEngine>();
    m_testAudio->startCapture(m_settings.inputDevice, [this](const float* samples, int count) {
        const float gain = m_settings.inputVolume;
        double energy = 0;
        for (int i = 0; i < count; ++i)
            energy += double(samples[i] * gain) * (samples[i] * gain);
        const double rms = count > 0 ? std::sqrt(energy / count) : 0.0;
        m_testLevelDb.store(rms > 1e-9 ? static_cast<float>(20.0 * std::log10(rms)) : -100.0f);
    });
}

void SettingsDialog::stopMicTest()
{
    m_testAudio.reset();
    m_testLevelDb.store(-100.0f);
}

void SettingsDialog::changeLanguage(const QString& code)
{
    if (code == Language::current())
        return;
    Language::setCurrent(code);

    QMessageBox box(this);
    box.setWindowTitle(tr("Language"));
    box.setText(tr("Restart Snapcord to apply the new language."));
    QPushButton* restartNow = box.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == restartNow) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    }
}
