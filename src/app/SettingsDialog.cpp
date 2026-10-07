#include "SettingsDialog.h"

#include "Language.h"
#include "Notifier.h"
#include "Theme.h"
#include "VoiceController.h"
#include "platform/KeyState.h"
#include "voice/AudioEngine.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
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
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/Session.h"

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
    painter.setBrush(Theme::instance().palette().button);
    painter.drawRoundedRect(bar, 4, 4);

    const qreal levelX = bar.width() * (qBound(MinDb, m_level, 0.0f) - MinDb) / -MinDb;
    const bool active = !m_thresholdVisible || m_level >= m_threshold;
    const auto& palette = Theme::instance().palette();
    painter.setBrush(active ? palette.success : palette.warning);
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

// --- ColorSwatch ------------------------------------------------------------------------------------

ColorSwatch::ColorSwatch(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("colorSwatch"));
    setCursor(Qt::PointingHandCursor);
    setFixedSize(40, 40);
    setFocusPolicy(Qt::NoFocus);
    setCheckable(false);
}

void ColorSwatch::setSwatchColor(const QColor& color)
{
    if (m_color == color)
        return;
    m_color = color.isValid() ? color : QColor(Qt::black);
    update();
}

void ColorSwatch::setSelectedSwatch(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void ColorSwatch::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    const QColor ring = m_selected || underMouse() ? Theme::instance().accent()
                                                   : Theme::instance().palette().border;
    painter.setPen(QPen(ring, m_selected ? 3 : 2));
    painter.setBrush(m_color);
    painter.drawRoundedRect(box, 6, 6);
}

// --- PresetCard -------------------------------------------------------------------------------------

PresetCard::PresetCard(const Theme::Preset& preset, const QString& title, QWidget* parent)
    : QPushButton(parent)
    , m_palette(preset.palette)
{
    setObjectName(QStringLiteral("presetCard"));
    setCursor(Qt::PointingHandCursor);
    setCheckable(true);
    setProperty("presetId", preset.id);
    setText(title);
    setMinimumHeight(64);
    setFocusPolicy(Qt::NoFocus);
}

void PresetCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF outer = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    const QColor border = isChecked() || underMouse() ? m_palette.accent : m_palette.border;
    painter.setPen(QPen(border, isChecked() ? 2.5 : 1.5));
    painter.setBrush(m_palette.bg2);
    painter.drawRoundedRect(outer, 8, 8);

    // Mini layout: rail | sidebar | chat
    const QRectF preview(outer.left() + 10, outer.top() + 10, 52, outer.height() - 20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_palette.bg0);
    painter.drawRoundedRect(QRectF(preview.left(), preview.top(), 10, preview.height()), 2, 2);
    painter.setBrush(m_palette.bg1);
    painter.drawRect(QRectF(preview.left() + 12, preview.top(), 16, preview.height()));
    painter.setBrush(m_palette.bg2);
    painter.drawRect(QRectF(preview.left() + 28, preview.top(), preview.width() - 28, preview.height()));
    painter.setBrush(m_palette.accent);
    painter.drawRoundedRect(QRectF(preview.left() + 32, preview.bottom() - 8, 12, 4), 1, 1);

    painter.setPen(m_palette.textBright);
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(QRectF(preview.right() + 12, outer.top(), outer.right() - preview.right() - 16, outer.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text());
}

// --- LayoutStudio -----------------------------------------------------------------------------------

LayoutStudio::LayoutStudio(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click a region to change its color"));
    connect(&Theme::instance(), &Theme::changed, this, QOverload<>::of(&QWidget::update));
}

QString LayoutStudio::hitTest(const QPoint& pos) const
{
    const QRect area = rect().adjusted(8, 8, -8, -8);
    if (!area.contains(pos))
        return {};
    const int railW = qMax(18, area.width() / 10);
    const int sideW = qMax(48, area.width() / 4);
    const QRect rail(area.left(), area.top(), railW, area.height());
    const QRect side(rail.right() + 1, area.top(), sideW, area.height());
    const QRect chat(side.right() + 1, area.top(), area.right() - side.right(), area.height());
    const QRect accent(chat.left() + 12, chat.bottom() - 18, 40, 8);
    if (accent.contains(pos))
        return QStringLiteral("accent");
    if (rail.contains(pos))
        return QStringLiteral("bg0");
    if (side.contains(pos))
        return QStringLiteral("bg1");
    if (chat.contains(pos))
        return QStringLiteral("bg2");
    return {};
}

void LayoutStudio::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Theme::Palette& c = Theme::instance().palette();
    const QRect area = rect().adjusted(8, 8, -8, -8);
    const int railW = qMax(18, area.width() / 10);
    const int sideW = qMax(48, area.width() / 4);
    const QRect rail(area.left(), area.top(), railW, area.height());
    const QRect side(rail.right() + 1, area.top(), sideW, area.height());
    const QRect chat(side.right() + 1, area.top(), area.right() - side.right(), area.height());

    painter.setPen(QPen(c.border, 1.5));
    painter.setBrush(c.bg2);
    painter.drawRoundedRect(area, 10, 10);

    auto drawRegion = [&](const QRect& r, const QColor& fill, const QString& id, const QString& label) {
        const bool hot = m_hover == id;
        painter.setPen(hot ? QPen(c.accent, 2) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRect(r);
        painter.setPen(c.textBright);
        QFont font = painter.font();
        font.setPixelSize(11);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignCenter, label);
    };

    drawRegion(rail, c.bg0, QStringLiteral("bg0"), tr("Rail"));
    drawRegion(side, c.bg1, QStringLiteral("bg1"), tr("Sidebar"));
    drawRegion(chat, c.bg2, QStringLiteral("bg2"), tr("Chat"));

    const QRect accent(chat.left() + 12, chat.bottom() - 18, 48, 10);
    painter.setPen(m_hover == u"accent" ? QPen(c.textBright, 1) : Qt::NoPen);
    painter.setBrush(c.accent);
    painter.drawRoundedRect(accent, 3, 3);
    painter.setPen(c.onAccent);
    painter.drawText(accent, Qt::AlignCenter, tr("Accent"));
}

void LayoutStudio::mousePressEvent(QMouseEvent* event)
{
    const QString id = hitTest(event->position().toPoint());
    if (!id.isEmpty())
        emit regionClicked(id);
}

void LayoutStudio::mouseMoveEvent(QMouseEvent* event)
{
    const QString id = hitTest(event->position().toPoint());
    if (id != m_hover) {
        m_hover = id;
        update();
    }
}

void LayoutStudio::leaveEvent(QEvent*)
{
    if (!m_hover.isEmpty()) {
        m_hover.clear();
        update();
    }
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
    navigation->addItem(tr("Appearance"));
    navigation->addItem(tr("Notifications"));
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
    pages->addWidget(buildAppearancePage());
    pages->addWidget(buildNotificationsPage());
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

    // Device names can be very long; without this the page grows wider than the window.
    for (QComboBox* combo : {m_inputDevice, m_outputDevice}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
    }

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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget* SettingsDialog::buildNotificationsPage()
{
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("settingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(12);
    auto* title = new QLabel(tr("Notifications"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    layout->addWidget(title);
    layout->addSpacing(8);

    auto* desktop = new QCheckBox(tr("Enable desktop notifications"));
    desktop->setChecked(Notifier::desktopNotificationsEnabled());
    connect(desktop, &QCheckBox::toggled, this, &Notifier::setDesktopNotificationsEnabled);
    layout->addWidget(option(desktop, tr("Direct messages and mentions show a notification while Snapcord is in the background.")));

    auto* sound = new QCheckBox(tr("Play a sound for new messages"));
    sound->setChecked(Notifier::soundEnabled());
    connect(sound, &QCheckBox::toggled, this, &Notifier::setSoundEnabled);
    layout->addWidget(option(sound, tr("Only for direct messages and mentions, like the notifications.")));
    layout->addStretch();
    return content;
}

QWidget* SettingsDialog::buildAppearancePage()
{
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("settingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Appearance"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    layout->addWidget(title);
    layout->addSpacing(4);

    auto* hint = new QLabel(tr("Themes and colors stay on this device. They do not change your Discord profile."));
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addSpacing(8);

    layout->addWidget(sectionLabel(tr("Theme")));
    m_presetGroup = new QButtonGroup(content);
    m_presetGroup->setExclusive(true);
    auto* presetGrid = new QGridLayout;
    presetGrid->setHorizontalSpacing(10);
    presetGrid->setVerticalSpacing(10);
    const Theme::Settings current = Theme::instance().settings();
    int index = 0;
    for (const Theme::Preset& preset : Theme::presets()) {
        auto* button = new PresetCard(preset, presetDisplayName(preset.id, preset.name));
        button->setChecked(preset.id == current.presetId);
        m_presetGroup->addButton(button, index);
        presetGrid->addWidget(button, index / 2, index % 2);
        ++index;
    }
    layout->addLayout(presetGrid);
    connect(m_presetGroup, &QButtonGroup::idClicked, this, &SettingsDialog::applyAppearance);

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Click to recolor")));
    auto* studioHint = new QLabel(tr("Click rail, sidebar, chat, or accent on the map. Changes apply instantly."));
    studioHint->setObjectName(QStringLiteral("settingsHint"));
    studioHint->setWordWrap(true);
    layout->addWidget(studioHint);
    m_layoutStudio = new LayoutStudio;
    layout->addWidget(m_layoutStudio);
    connect(m_layoutStudio, &LayoutStudio::regionClicked, this, [this](const QString& id) {
        pickColor(Theme::instance().tokenColor(id), Theme::tokenLabel(id), [this, id](const QColor& c) {
            Theme::instance().setTokenOverride(id, c);
            refreshColorSwatches();
        });
    });

    m_customizedLabel = new QLabel;
    m_customizedLabel->setObjectName(QStringLiteral("settingsHint"));
    layout->addWidget(m_customizedLabel);
    auto* resetAll = new QPushButton(tr("Reset all customization"));
    resetAll->setObjectName(QStringLiteral("secondaryButton"));
    resetAll->setCursor(Qt::PointingHandCursor);
    layout->addWidget(resetAll, 0, Qt::AlignLeft);
    connect(resetAll, &QPushButton::clicked, this, [this] {
        Theme::instance().clearCustomization();
        refreshAppearanceControls();
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Tone & scale")));
    auto addToneSlider = [&](const QString& title, QSlider*& slider, QLabel*& valueLabel, int min, int max,
                             int value, const std::function<QString(int)>& format) {
        layout->addWidget(new QLabel(title));
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(value);
        valueLabel = new QLabel(format(value));
        valueLabel->setObjectName(QStringLiteral("settingsHint"));
        auto* row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
        layout->addLayout(row);
        QLabel* label = valueLabel;
        connect(slider, &QSlider::valueChanged, this, [this, label, format](int v) {
            label->setText(format(v));
            applyAppearance();
        });
    };
    addToneSlider(tr("Brightness"), m_brightness, m_brightnessLabel, -40, 40, current.brightness,
                  [this](int v) { return tr("%1").arg(v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v)); });
    addToneSlider(tr("Saturation"), m_saturation, m_saturationLabel, -50, 50, current.saturation,
                  [this](int v) { return tr("%1").arg(v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v)); });
    addToneSlider(tr("UI scale"), m_uiScale, m_uiScaleLabel, 85, 130, current.uiScale,
                  [this](int v) { return tr("%1%").arg(v); });
    addToneSlider(tr("Corner radius"), m_radius, m_radiusLabel, 0, 16, current.radius,
                  [this](int v) { return tr("%1 px").arg(v); });
    addToneSlider(tr("Panel opacity (rail & sidebar)"), m_panelOpacity, m_panelOpacityLabel, 40, 100,
                  current.panelOpacity, [this](int v) { return tr("%1%").arg(v); });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Accent color")));
    auto* accentHint = new QLabel(tr("Pick a color or open the custom picker."));
    accentHint->setObjectName(QStringLiteral("settingsHint"));
    accentHint->setWordWrap(true);
    layout->addWidget(accentHint);

    auto* accentRow = new QHBoxLayout;
    accentRow->setSpacing(8);
    m_accentChips.clear();
    for (const QColor& color : Theme::accentSwatches()) {
        auto* chip = new ColorSwatch;
        chip->setFixedSize(32, 32);
        chip->setSwatchColor(color);
        chip->setToolTip(color.name(QColor::HexRgb).toUpper());
        accentRow->addWidget(chip);
        m_accentChips.push_back(chip);
        connect(chip, &QPushButton::clicked, this, [this, color] { setCustomAccent(color); });
    }
    m_accentSwatch = new ColorSwatch;
    m_accentSwatch->setToolTip(tr("Custom…"));
    accentRow->addWidget(m_accentSwatch);
    auto* resetAccent = new QPushButton(tr("Use theme default"));
    resetAccent->setObjectName(QStringLiteral("secondaryButton"));
    resetAccent->setCursor(Qt::PointingHandCursor);
    accentRow->addWidget(resetAccent);
    accentRow->addStretch();
    layout->addLayout(accentRow);
    connect(m_accentSwatch, &QPushButton::clicked, this, [this] {
        const Theme::Settings settings = Theme::instance().settings();
        const QColor initial = settings.customAccent.isValid() ? settings.customAccent
                                                               : Theme::instance().accent();
        pickColor(initial, tr("Accent color"), [this](const QColor& chosen) { setCustomAccent(chosen); });
    });
    connect(resetAccent, &QPushButton::clicked, this, [this] {
        setCustomAccent(QColor()); // invalid = follow the selected theme
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Backgrounds")));
    auto* bgHint = new QLabel(tr("Server rail, channel list, and chat area. Leave unset to follow the theme."));
    bgHint->setObjectName(QStringLiteral("settingsHint"));
    bgHint->setWordWrap(true);
    layout->addWidget(bgHint);
    auto* bgRow = new QHBoxLayout;
    bgRow->setSpacing(8);
    m_bg0Swatch = new ColorSwatch;
    m_bg0Swatch->setToolTip(tr("Server rail"));
    m_bg1Swatch = new ColorSwatch;
    m_bg1Swatch->setToolTip(tr("Channel list"));
    m_bg2Swatch = new ColorSwatch;
    m_bg2Swatch->setToolTip(tr("Chat area"));
    auto* resetBg = new QPushButton(tr("Reset backgrounds"));
    resetBg->setObjectName(QStringLiteral("secondaryButton"));
    resetBg->setCursor(Qt::PointingHandCursor);
    bgRow->addWidget(m_bg0Swatch);
    bgRow->addWidget(m_bg1Swatch);
    bgRow->addWidget(m_bg2Swatch);
    bgRow->addWidget(resetBg);
    bgRow->addStretch();
    layout->addLayout(bgRow);
    connect(m_bg0Swatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().palette().bg0, tr("Server rail"), [this](const QColor& c) {
            Theme::Settings s = Theme::instance().settings();
            s.customBg0 = c;
            Theme::instance().setSettings(s);
            refreshColorSwatches();
        });
    });
    connect(m_bg1Swatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().palette().bg1, tr("Channel list"), [this](const QColor& c) {
            Theme::Settings s = Theme::instance().settings();
            s.customBg1 = c;
            Theme::instance().setSettings(s);
            refreshColorSwatches();
        });
    });
    connect(m_bg2Swatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().palette().bg2, tr("Chat area"), [this](const QColor& c) {
            Theme::Settings s = Theme::instance().settings();
            s.customBg2 = c;
            Theme::instance().setSettings(s);
            refreshColorSwatches();
        });
    });
    connect(resetBg, &QPushButton::clicked, this, [this] {
        Theme::Settings s = Theme::instance().settings();
        s.customBg0 = QColor();
        s.customBg1 = QColor();
        s.customBg2 = QColor();
        Theme::instance().setSettings(s);
        refreshColorSwatches();
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("My profile (this app only)")));
    auto* profileHint = new QLabel(tr("Colors for your user panel at the bottom left."));
    profileHint->setObjectName(QStringLiteral("settingsHint"));
    profileHint->setWordWrap(true);
    layout->addWidget(profileHint);

    auto* profileRow = new QHBoxLayout;
    m_profilePrimarySwatch = new ColorSwatch;
    m_profilePrimarySwatch->setToolTip(tr("Panel background"));
    m_profileAccentSwatch = new ColorSwatch;
    m_profileAccentSwatch->setToolTip(tr("Panel accent stripe"));
    auto* resetProfile = new QPushButton(tr("Reset profile colors"));
    resetProfile->setObjectName(QStringLiteral("secondaryButton"));
    resetProfile->setCursor(Qt::PointingHandCursor);
    profileRow->addWidget(m_profilePrimarySwatch);
    profileRow->addWidget(m_profileAccentSwatch);
    profileRow->addWidget(resetProfile);
    profileRow->addStretch();
    layout->addLayout(profileRow);

    connect(m_profilePrimarySwatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().profilePrimary(), tr("Profile background"), [this](const QColor& chosen) {
            Theme::Settings settings = Theme::instance().settings();
            settings.profilePrimary = chosen;
            Theme::instance().setSettings(settings);
            refreshColorSwatches();
        });
    });
    connect(m_profileAccentSwatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().profileAccent(), tr("Profile accent"), [this](const QColor& chosen) {
            Theme::Settings settings = Theme::instance().settings();
            settings.profileAccent = chosen;
            Theme::instance().setSettings(settings);
            refreshColorSwatches();
        });
    });
    connect(resetProfile, &QPushButton::clicked, this, [this] {
        Theme::Settings settings = Theme::instance().settings();
        settings.profilePrimary = QColor();
        settings.profileAccent = QColor();
        Theme::instance().setSettings(settings);
        refreshColorSwatches();
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Chat density")));
    m_chatDensity = new QComboBox;
    m_chatDensity->addItem(tr("Compact"), static_cast<int>(Theme::ChatDensity::Compact));
    m_chatDensity->addItem(tr("Normal"), static_cast<int>(Theme::ChatDensity::Normal));
    m_chatDensity->addItem(tr("Comfortable"), static_cast<int>(Theme::ChatDensity::Comfortable));
    m_chatDensity->setCurrentIndex(m_chatDensity->findData(static_cast<int>(current.chatDensity)));
    layout->addWidget(m_chatDensity);
    connect(m_chatDensity, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::applyAppearance);

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Font")));
    m_fontFamily = new QComboBox;
    for (const QString& family : Theme::fontFamilyChoices()) {
        if (family.isEmpty())
            m_fontFamily->addItem(tr("Default (Noto / Inter)"), family);
        else
            m_fontFamily->addItem(family, family);
    }
    {
        const int idx = m_fontFamily->findData(current.fontFamily);
        m_fontFamily->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    layout->addWidget(m_fontFamily);
    connect(m_fontFamily, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::applyAppearance);

    layout->addWidget(sectionLabel(tr("Chat font size")));
    m_fontSize = new QSlider(Qt::Horizontal);
    m_fontSize->setRange(12, 18);
    m_fontSize->setValue(current.fontSize);
    m_fontSizeLabel = new QLabel;
    m_fontSizeLabel->setObjectName(QStringLiteral("settingsHint"));
    auto* fontRow = new QHBoxLayout;
    fontRow->addWidget(m_fontSize, 1);
    fontRow->addWidget(m_fontSizeLabel);
    layout->addLayout(fontRow);
    connect(m_fontSize, &QSlider::valueChanged, this, [this](int value) {
        m_fontSizeLabel->setText(tr("%1 px").arg(value));
        applyAppearance();
    });
    m_fontSizeLabel->setText(tr("%1 px").arg(m_fontSize->value()));

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Discord Nitro colors")));
    auto* nitroHint = new QLabel(
        tr("Uses your Discord accent_color / banner when the account has Nitro profile colors."));
    nitroHint->setObjectName(QStringLiteral("settingsHint"));
    nitroHint->setWordWrap(true);
    layout->addWidget(nitroHint);
    m_syncDiscordAccent = new QCheckBox(tr("Keep accent in sync with Discord"));
    m_syncDiscordAccent->setChecked(current.syncDiscordAccent);
    layout->addWidget(m_syncDiscordAccent);
    connect(m_syncDiscordAccent, &QCheckBox::toggled, this, &SettingsDialog::applyAppearance);
    auto* applyDiscord = new QPushButton(tr("Apply Discord colors now"));
    applyDiscord->setObjectName(QStringLiteral("secondaryButton"));
    applyDiscord->setCursor(Qt::PointingHandCursor);
    layout->addWidget(applyDiscord, 0, Qt::AlignLeft);
    connect(applyDiscord, &QPushButton::clicked, this, [this] {
        if (!m_voice || !m_voice->session())
            return;
        const User& self = m_voice->session()->self();
        const QColor accent = self.hasAccentColor ? QColor::fromRgb(self.accentColorRgb) : QColor();
        const QColor banner(self.bannerColorHex);
        Theme::instance().setDiscordProfileColors(accent, banner);
        Theme::Settings settings = Theme::instance().settings();
        if (accent.isValid())
            settings.customAccent = accent;
        if (banner.isValid())
            settings.profilePrimary = banner;
        Theme::instance().setSettings(settings);
        refreshAppearanceControls();
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Chat wallpaper")));
    auto* wallHint = new QLabel(
        tr("Optional image behind the UI. Kept light: no blur, large images are capped in memory."));
    wallHint->setObjectName(QStringLiteral("settingsHint"));
    wallHint->setWordWrap(true);
    layout->addWidget(wallHint);
    m_wallpaperPathLabel = new QLabel(current.wallpaperPath.isEmpty() ? tr("No wallpaper")
                                                                      : current.wallpaperPath);
    m_wallpaperPathLabel->setObjectName(QStringLiteral("settingsHint"));
    m_wallpaperPathLabel->setWordWrap(true);
    layout->addWidget(m_wallpaperPathLabel);
    auto* wallButtons = new QHBoxLayout;
    auto* chooseWall = new QPushButton(tr("Choose image…"));
    chooseWall->setObjectName(QStringLiteral("secondaryButton"));
    chooseWall->setCursor(Qt::PointingHandCursor);
    auto* clearWall = new QPushButton(tr("Clear wallpaper"));
    clearWall->setObjectName(QStringLiteral("secondaryButton"));
    clearWall->setCursor(Qt::PointingHandCursor);
    wallButtons->addWidget(chooseWall);
    wallButtons->addWidget(clearWall);
    wallButtons->addStretch();
    layout->addLayout(wallButtons);
    connect(chooseWall, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Chat wallpaper"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (path.isEmpty())
            return;
        Theme::Settings settings = Theme::instance().settings();
        settings.wallpaperPath = path;
        Theme::instance().setSettings(settings);
        refreshAppearanceControls();
    });
    connect(clearWall, &QPushButton::clicked, this, [this] {
        Theme::Settings settings = Theme::instance().settings();
        settings.wallpaperPath.clear();
        Theme::instance().setSettings(settings);
        refreshAppearanceControls();
    });
    m_wallpaperAppWide = new QCheckBox(tr("Wallpaper behind the whole window (not only chat)"));
    m_wallpaperAppWide->setChecked(current.wallpaperAppWide);
    layout->addWidget(m_wallpaperAppWide);
    connect(m_wallpaperAppWide, &QCheckBox::toggled, this, &SettingsDialog::applyAppearance);

    auto addPercentSlider = [&](const QString& title, QSlider*& slider, QLabel*& valueLabel, int min, int max,
                                int value) {
        layout->addWidget(new QLabel(title));
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(value);
        valueLabel = new QLabel(tr("%1%").arg(value));
        valueLabel->setObjectName(QStringLiteral("settingsHint"));
        auto* row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
        layout->addLayout(row);
        QLabel* label = valueLabel;
        connect(slider, &QSlider::valueChanged, this, [this, label](int v) {
            label->setText(tr("%1%").arg(v));
            applyAppearance();
        });
    };
    addPercentSlider(tr("Wallpaper opacity"), m_wallpaperOpacity, m_wallpaperOpacityLabel, 0, 100,
                     current.wallpaperOpacity);
    addPercentSlider(tr("Dim overlay"), m_wallpaperFrost, m_wallpaperFrostLabel, 0, 80,
                     current.wallpaperFrost);

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Gradient")));
    m_gradientEnabled = new QCheckBox(tr("Enable chat gradient"));
    m_gradientEnabled->setChecked(current.gradientEnabled);
    layout->addWidget(m_gradientEnabled);
    connect(m_gradientEnabled, &QCheckBox::toggled, this, &SettingsDialog::applyAppearance);
    auto* gradRow = new QHBoxLayout;
    m_gradientTopSwatch = new ColorSwatch;
    m_gradientTopSwatch->setToolTip(tr("Gradient top"));
    m_gradientBottomSwatch = new ColorSwatch;
    m_gradientBottomSwatch->setToolTip(tr("Gradient bottom"));
    gradRow->addWidget(m_gradientTopSwatch);
    gradRow->addWidget(m_gradientBottomSwatch);
    gradRow->addStretch();
    layout->addLayout(gradRow);
    connect(m_gradientTopSwatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().settings().gradientTop, tr("Gradient top"), [this](const QColor& c) {
            Theme::Settings s = Theme::instance().settings();
            s.gradientTop = c;
            Theme::instance().setSettings(s);
            refreshColorSwatches();
        });
    });
    connect(m_gradientBottomSwatch, &QPushButton::clicked, this, [this] {
        pickColor(Theme::instance().settings().gradientBottom, tr("Gradient bottom"), [this](const QColor& c) {
            Theme::Settings s = Theme::instance().settings();
            s.gradientBottom = c;
            Theme::instance().setSettings(s);
            refreshColorSwatches();
        });
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Advanced tokens")));
    auto* tokenHint = new QLabel(tr("Override any palette token. Cleared entries follow the theme again."));
    tokenHint->setObjectName(QStringLiteral("settingsHint"));
    tokenHint->setWordWrap(true);
    layout->addWidget(tokenHint);
    auto* tokenGrid = new QGridLayout;
    tokenGrid->setHorizontalSpacing(8);
    tokenGrid->setVerticalSpacing(6);
    m_tokenSwatches.clear();
    int tokenIndex = 0;
    for (const QString& id : Theme::tokenIds()) {
        auto* name = new QLabel(Theme::tokenLabel(id));
        name->setObjectName(QStringLiteral("settingsHint"));
        auto* chip = new ColorSwatch;
        chip->setFixedSize(28, 28);
        chip->setToolTip(id);
        tokenGrid->addWidget(name, tokenIndex, 0);
        tokenGrid->addWidget(chip, tokenIndex, 1);
        m_tokenSwatches.insert(id, chip);
        connect(chip, &QPushButton::clicked, this, [this, id] {
            pickColor(Theme::instance().tokenColor(id), Theme::tokenLabel(id), [this, id](const QColor& c) {
                Theme::instance().setTokenOverride(id, c);
                refreshColorSwatches();
            });
        });
        ++tokenIndex;
    }
    layout->addLayout(tokenGrid);
    auto* resetTokens = new QPushButton(tr("Reset all token overrides"));
    resetTokens->setObjectName(QStringLiteral("secondaryButton"));
    resetTokens->setCursor(Qt::PointingHandCursor);
    layout->addWidget(resetTokens, 0, Qt::AlignLeft);
    connect(resetTokens, &QPushButton::clicked, this, [this] {
        Theme::Settings settings = Theme::instance().settings();
        settings.tokenOverrides.clear();
        Theme::instance().setSettings(settings);
        refreshColorSwatches();
    });

    layout->addSpacing(12);
    layout->addWidget(sectionLabel(tr("Import / export")));
    auto* ioRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton(tr("Export theme…"));
    exportBtn->setObjectName(QStringLiteral("secondaryButton"));
    exportBtn->setCursor(Qt::PointingHandCursor);
    auto* importBtn = new QPushButton(tr("Import theme…"));
    importBtn->setObjectName(QStringLiteral("secondaryButton"));
    importBtn->setCursor(Qt::PointingHandCursor);
    ioRow->addWidget(exportBtn);
    ioRow->addWidget(importBtn);
    ioRow->addStretch();
    layout->addLayout(ioRow);
    connect(exportBtn, &QPushButton::clicked, this, &SettingsDialog::exportTheme);
    connect(importBtn, &QPushButton::clicked, this, &SettingsDialog::importTheme);

    layout->addStretch();
    refreshColorSwatches();
    connect(&Theme::instance(), &Theme::changed, this, &SettingsDialog::refreshColorSwatches);

    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void SettingsDialog::refreshColorSwatches()
{
    const Theme::Palette& palette = Theme::instance().palette();
    const Theme::Settings& appearance = Theme::instance().settings();
    if (m_accentSwatch)
        m_accentSwatch->setSwatchColor(Theme::instance().accent());
    if (m_bg0Swatch)
        m_bg0Swatch->setSwatchColor(palette.bg0);
    if (m_bg1Swatch)
        m_bg1Swatch->setSwatchColor(palette.bg1);
    if (m_bg2Swatch)
        m_bg2Swatch->setSwatchColor(palette.bg2);
    if (m_profilePrimarySwatch)
        m_profilePrimarySwatch->setSwatchColor(Theme::instance().profilePrimary());
    if (m_profileAccentSwatch)
        m_profileAccentSwatch->setSwatchColor(Theme::instance().profileAccent());
    if (m_gradientTopSwatch)
        m_gradientTopSwatch->setSwatchColor(appearance.gradientTop);
    if (m_gradientBottomSwatch)
        m_gradientBottomSwatch->setSwatchColor(appearance.gradientBottom);

    const QColor custom = appearance.customAccent;
    bool matchedChip = false;
    for (ColorSwatch* chip : m_accentChips) {
        if (!chip)
            continue;
        const bool match = custom.isValid() && chip->swatchColor().rgb() == custom.rgb();
        chip->setSelectedSwatch(match);
        matchedChip = matchedChip || match;
    }
    if (m_accentSwatch)
        m_accentSwatch->setSelectedSwatch(custom.isValid() && !matchedChip);

    for (auto it = m_tokenSwatches.begin(); it != m_tokenSwatches.end(); ++it) {
        if (!it.value())
            continue;
        it.value()->setSwatchColor(Theme::instance().tokenColor(it.key()));
        it.value()->setSelectedSwatch(appearance.tokenOverrides.contains(it.key()));
    }

    if (m_customizedLabel) {
        m_customizedLabel->setText(Theme::instance().hasCustomization()
                                       ? tr("This theme has local customizations.")
                                       : tr("Using the preset as-is."));
    }
}

void SettingsDialog::refreshAppearanceControls()
{
    const Theme::Settings& appearance = Theme::instance().settings();
    const QSignalBlocker blockFont(m_fontSize);
    const QSignalBlocker blockFamily(m_fontFamily);
    const QSignalBlocker blockRadius(m_radius);
    const QSignalBlocker blockScale(m_uiScale);
    const QSignalBlocker blockBright(m_brightness);
    const QSignalBlocker blockSat(m_saturation);
    const QSignalBlocker blockPanel(m_panelOpacity);
    const QSignalBlocker blockDensity(m_chatDensity);
    const QSignalBlocker blockSync(m_syncDiscordAccent);
    const QSignalBlocker blockGrad(m_gradientEnabled);
    const QSignalBlocker blockAppWide(m_wallpaperAppWide);
    const QSignalBlocker blockOpacity(m_wallpaperOpacity);
    const QSignalBlocker blockFrost(m_wallpaperFrost);

    if (m_presetGroup) {
        const auto buttons = m_presetGroup->buttons();
        for (QAbstractButton* button : buttons)
            button->setChecked(button->property("presetId").toString() == appearance.presetId);
    }
    if (m_fontSize) {
        m_fontSize->setValue(appearance.fontSize);
        if (m_fontSizeLabel)
            m_fontSizeLabel->setText(tr("%1 px").arg(appearance.fontSize));
    }
    if (m_fontFamily) {
        const int idx = m_fontFamily->findData(appearance.fontFamily);
        m_fontFamily->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_radius) {
        m_radius->setValue(appearance.radius);
        if (m_radiusLabel)
            m_radiusLabel->setText(tr("%1 px").arg(appearance.radius));
    }
    if (m_uiScale) {
        m_uiScale->setValue(appearance.uiScale);
        if (m_uiScaleLabel)
            m_uiScaleLabel->setText(tr("%1%").arg(appearance.uiScale));
    }
    if (m_brightness) {
        m_brightness->setValue(appearance.brightness);
        if (m_brightnessLabel) {
            const int v = appearance.brightness;
            m_brightnessLabel->setText(v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v));
        }
    }
    if (m_saturation) {
        m_saturation->setValue(appearance.saturation);
        if (m_saturationLabel) {
            const int v = appearance.saturation;
            m_saturationLabel->setText(v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v));
        }
    }
    if (m_panelOpacity) {
        m_panelOpacity->setValue(appearance.panelOpacity);
        if (m_panelOpacityLabel)
            m_panelOpacityLabel->setText(tr("%1%").arg(appearance.panelOpacity));
    }
    if (m_chatDensity)
        m_chatDensity->setCurrentIndex(m_chatDensity->findData(static_cast<int>(appearance.chatDensity)));
    if (m_syncDiscordAccent)
        m_syncDiscordAccent->setChecked(appearance.syncDiscordAccent);
    if (m_wallpaperAppWide)
        m_wallpaperAppWide->setChecked(appearance.wallpaperAppWide);
    if (m_gradientEnabled)
        m_gradientEnabled->setChecked(appearance.gradientEnabled);
    if (m_wallpaperPathLabel) {
        m_wallpaperPathLabel->setText(appearance.wallpaperPath.isEmpty() ? tr("No wallpaper")
                                                                         : appearance.wallpaperPath);
    }
    if (m_wallpaperOpacity) {
        m_wallpaperOpacity->setValue(appearance.wallpaperOpacity);
        if (m_wallpaperOpacityLabel)
            m_wallpaperOpacityLabel->setText(tr("%1%").arg(appearance.wallpaperOpacity));
    }
    if (m_wallpaperFrost) {
        m_wallpaperFrost->setValue(appearance.wallpaperFrost);
        if (m_wallpaperFrostLabel)
            m_wallpaperFrostLabel->setText(tr("%1%").arg(appearance.wallpaperFrost));
    }
    refreshColorSwatches();
}

void SettingsDialog::exportTheme()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export theme"), QStringLiteral("snapcord-theme.json"),
                                                      tr("Theme JSON (*.json)"));
    if (path.isEmpty())
        return;
    const QString error = Theme::instance().exportToFile(path);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Export theme"), error);
}

void SettingsDialog::importTheme()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import theme"), QString(),
                                                      tr("Theme JSON (*.json)"));
    if (path.isEmpty())
        return;
    const QString error = Theme::instance().importFromFile(path);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import theme"), error);
        return;
    }
    refreshAppearanceControls();
}

void SettingsDialog::setCustomAccent(const QColor& color)
{
    Theme::Settings settings = Theme::instance().settings();
    settings.customAccent = color;
    Theme::instance().setSettings(settings);
    refreshColorSwatches();
}

void SettingsDialog::pickColor(const QColor& initial, const QString& title,
                               const std::function<void(QColor)>& onPicked)
{
    // Native portal pickers on Linux often hang the whole Qt app; use Qt's own dialog.
    auto* dialog = new QColorDialog(initial.isValid() ? initial : Qt::white, this);
    dialog->setWindowTitle(title);
    dialog->setOption(QColorDialog::DontUseNativeDialog, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    connect(dialog, &QColorDialog::colorSelected, this, [onPicked](const QColor& color) {
        if (color.isValid())
            onPicked(color);
    });
    dialog->open();
}

QString SettingsDialog::presetDisplayName(const QString& id, const QString& fallback)
{
    if (id == QLatin1String("discord"))
        return tr("Discord");
    if (id == QLatin1String("midnight"))
        return tr("Midnight");
    if (id == QLatin1String("amoled"))
        return tr("AMOLED");
    if (id == QLatin1String("ash"))
        return tr("Ash");
    if (id == QLatin1String("catppuccin-mocha"))
        return tr("Catppuccin Mocha");
    if (id == QLatin1String("catppuccin-latte"))
        return tr("Catppuccin Latte");
    if (id == QLatin1String("nord"))
        return tr("Nord");
    if (id == QLatin1String("dracula"))
        return tr("Dracula");
    if (id == QLatin1String("gruvbox"))
        return tr("Gruvbox");
    if (id == QLatin1String("tokyo-night"))
        return tr("Tokyo Night");
    if (id == QLatin1String("rose-pine"))
        return tr("Rosé Pine");
    if (id == QLatin1String("one-dark"))
        return tr("One Dark");
    if (id == QLatin1String("light"))
        return tr("Light");
    return fallback;
}

void SettingsDialog::applyAppearance()
{
    Theme::Settings settings = Theme::instance().settings();
    if (m_presetGroup) {
        if (QAbstractButton* button = m_presetGroup->checkedButton())
            settings.presetId = button->property("presetId").toString();
    }
    if (m_fontSize)
        settings.fontSize = m_fontSize->value();
    if (m_fontFamily)
        settings.fontFamily = m_fontFamily->currentData().toString();
    if (m_radius)
        settings.radius = m_radius->value();
    if (m_uiScale)
        settings.uiScale = m_uiScale->value();
    if (m_brightness)
        settings.brightness = m_brightness->value();
    if (m_saturation)
        settings.saturation = m_saturation->value();
    if (m_panelOpacity)
        settings.panelOpacity = m_panelOpacity->value();
    if (m_chatDensity)
        settings.chatDensity = static_cast<Theme::ChatDensity>(m_chatDensity->currentData().toInt());
    if (m_syncDiscordAccent)
        settings.syncDiscordAccent = m_syncDiscordAccent->isChecked();
    if (m_wallpaperAppWide)
        settings.wallpaperAppWide = m_wallpaperAppWide->isChecked();
    if (m_gradientEnabled)
        settings.gradientEnabled = m_gradientEnabled->isChecked();
    if (m_wallpaperOpacity)
        settings.wallpaperOpacity = m_wallpaperOpacity->value();
    if (m_wallpaperFrost)
        settings.wallpaperFrost = m_wallpaperFrost->value();
    Theme::instance().setSettings(settings);
    refreshColorSwatches();
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
    // Stop capture before the dialog finishes hiding; otherwise PipeWire can block the UI thread
    // while the modal event loop is still unwinding.
    stopMicTest();
    QDialog::hideEvent(event);
}

void SettingsDialog::startMicTest()
{
    if (!m_testAudio)
        m_testAudio = std::make_unique<AudioEngine>();
    // Capture only the atomic level — never touch widgets from the audio thread.
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
    if (!m_testAudio)
        return;
    m_testAudio->stopCapture();
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
