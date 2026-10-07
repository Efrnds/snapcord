#include "Motion.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPainter>
#include <QSettings>
#include <QVariantAnimation>
#include <QWidget>

namespace Motion {

namespace {

constexpr auto ReduceMotionKey = "ui/reduceMotion";
constexpr int FrameInterval = 16;

int s_reduceMotion = -1; // -1 = not read from the settings yet
bool s_suppressed = false;

// Window opacity is unsupported on Wayland (Qt warns on every call) and pointless off screen.
bool windowOpacitySupported()
{
    static const bool supported = [] {
        const QString platform = QGuiApplication::platformName();
        return !platform.startsWith(u"wayland") && platform != u"offscreen" && platform != u"minimal";
    }();
    return supported;
}

// Qt's own menu, combo box and tooltip effects would run on top of ours (and grab the screen to do so).
void disableQtEffects()
{
    for (Qt::UIEffect effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                                Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
        QApplication::setEffectEnabled(effect, false);
}

// Runs `apply` with values from `from` to `to`, then `done`; the animation belongs to `owner`.
QVariantAnimation* animate(QObject* owner, qreal from, qreal to, int duration, const std::function<void(qreal)>& apply,
                           const std::function<void()>& done = {})
{
    auto* animation = new QVariantAnimation(owner);
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setDuration(duration);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QVariantAnimation::valueChanged, owner,
                     [apply](const QVariant& value) { apply(value.toReal()); });
    if (done)
        QObject::connect(animation, &QVariantAnimation::finished, owner, done);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    return animation;
}

class WindowFadeFilter : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Show && !event->spontaneous() && object->isWidgetType()) {
            auto* widget = static_cast<QWidget*>(object);
            if (widget->isWindow()) {
                const Qt::WindowType type = widget->windowType();
                if (type == Qt::Popup || type == Qt::ToolTip)
                    fadeInWindow(widget, Fast);
                else if (type == Qt::Dialog || type == Qt::Sheet)
                    fadeInWindow(widget, Normal);
            }
        }
        return false;
    }
};

// Paints a snapshot with decreasing opacity (cross-fade), or a background plus a sliding panel.
class Overlay : public QWidget
{
public:
    Overlay(QWidget* parent, QWidget* target)
        : QWidget(parent)
        , m_target(target)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setObjectName(QStringLiteral("motionOverlay"));
    }

    QWidget* target() const { return m_target; }

    QPixmap snapshot;   // cross-fade: what was there before
    QPixmap background; // slide: what is behind the panel
    QPixmap panel;      // slide: the panel itself
    qreal opacity = 1.0;
    qreal offset = 0.0; // slide: how far the panel is pushed out to the right

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        if (!snapshot.isNull()) {
            painter.setOpacity(opacity);
            painter.drawPixmap(0, 0, snapshot);
            return;
        }
        painter.drawPixmap(0, 0, background);
        painter.drawPixmap(QPointF(offset, 0), panel);
    }

private:
    QPointer<QWidget> m_target;
};

// One overlay per target: starting a new transition removes the previous one.
Overlay* makeOverlay(QWidget* target, const QRect& rectInTarget)
{
    QWidget* window = target->window();
    for (QWidget* child : window->findChildren<QWidget*>(QStringLiteral("motionOverlay"), Qt::FindDirectChildrenOnly)) {
        if (auto* old = dynamic_cast<Overlay*>(child); old && old->target() == target)
            delete old;
    }
    auto* overlay = new Overlay(window, target);
    overlay->setGeometry(QRect(target->mapTo(window, rectInTarget.topLeft()), rectInTarget.size()));
    return overlay;
}

QPixmap crop(const QPixmap& pixmap, const QRect& rect)
{
    const qreal ratio = pixmap.devicePixelRatio();
    QPixmap result = pixmap.copy(QRect((QPointF(rect.topLeft()) * ratio).toPoint(), (QSizeF(rect.size()) * ratio).toSize()));
    result.setDevicePixelRatio(ratio);
    return result;
}

} // namespace

bool reduceMotion()
{
    if (s_reduceMotion < 0)
        s_reduceMotion = QSettings().value(QLatin1String(ReduceMotionKey), false).toBool() ? 1 : 0;
    return s_reduceMotion == 1;
}

void setReduceMotion(bool reduce)
{
    s_reduceMotion = reduce ? 1 : 0;
    QSettings().setValue(QLatin1String(ReduceMotionKey), reduce);
}

void suppress()
{
    s_suppressed = true;
}

bool enabled()
{
    return !s_suppressed && !reduceMotion();
}

qreal ease(qreal progress)
{
    const qreal t = 1.0 - qBound(0.0, progress, 1.0);
    return 1.0 - t * t * t;
}

QColor mix(const QColor& from, const QColor& to, qreal amount)
{
    if (amount <= 0.0)
        return from;
    if (amount >= 1.0)
        return to;
    const QColor a = from.toRgb();
    const QColor b = to.toRgb();
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * amount, a.greenF() + (b.greenF() - a.greenF()) * amount,
                            a.blueF() + (b.blueF() - a.blueF()) * amount,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * amount);
}

void installWindowFades()
{
    disableQtEffects();
    qApp->installEventFilter(new WindowFadeFilter(qApp));
}

void fadeInWindow(QWidget* window, int duration)
{
    if (!enabled() || !windowOpacitySupported())
        return;
    // Showing again while a fade still runs restarts it.
    if (auto* running = window->findChild<QVariantAnimation*>(QStringLiteral("motionWindowFade"), Qt::FindDirectChildrenOnly))
        delete running;
    else if (window->windowOpacity() < 1.0)
        return; // a window that is translucent on purpose
    window->setWindowOpacity(0.0);
    QPointer<QWidget> guard(window);
    QVariantAnimation* animation = animate(
        window, 0.0, 1.0, duration, [guard](qreal value) { if (guard) guard->setWindowOpacity(value); },
        [guard] { if (guard) guard->setWindowOpacity(1.0); });
    animation->setObjectName(QStringLiteral("motionWindowFade"));
}

void fadeInWidget(QWidget* widget, int duration)
{
    if (!enabled() || !widget->isVisible() || widget->graphicsEffect())
        return;
    // The opacity effect makes the widget render off screen, so it only stays for the fade.
    auto* effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(0.0);
    widget->setGraphicsEffect(effect);
    // The animation belongs to the widget, not the effect: removing the effect deletes it, and that happens
    // while the animation is still reporting that it finished.
    QPointer<QGraphicsOpacityEffect> guard(effect);
    animate(widget, 0.0, 1.0, duration, [guard](qreal value) { if (guard) guard->setOpacity(value); },
            [widget, guard] {
                if (guard && widget->graphicsEffect() == guard)
                    widget->setGraphicsEffect(nullptr);
            });
}

void crossFade(QWidget* widget, int duration)
{
    if (!enabled() || !widget->isVisible() || widget->size().isEmpty())
        return;
    const QPixmap snapshot = widget->grab();
    Overlay* overlay = makeOverlay(widget, widget->rect());
    overlay->snapshot = snapshot;
    overlay->show();
    overlay->raise();
    QPointer<Overlay> guard(overlay);
    animate(overlay, 1.0, 0.0, duration, [guard](qreal value) {
        if (!guard)
            return;
        guard->opacity = value;
        guard->update();
    }, [guard] { if (guard) guard->deleteLater(); });
}

void slidePanel(QWidget* container, QWidget* panel, const std::function<void()>& change, int duration)
{
    const bool wasVisible = panel->isVisible();
    if (!enabled() || !container->isVisible()) {
        change();
        return;
    }
    const QPixmap before = container->grab();
    const QRect rectBefore(panel->mapTo(container, QPoint(0, 0)), panel->size());
    const QPixmap panelBefore = wasVisible ? panel->grab() : QPixmap();

    change();
    if (panel->isVisible() == wasVisible)
        return;
    if (QLayout* layout = container->layout())
        layout->activate(); // the new geometry, now rather than on the next event loop pass

    const bool showing = panel->isVisible();
    const QRect area = showing ? QRect(panel->mapTo(container, QPoint(0, 0)), panel->size()) : rectBefore;
    if (area.isEmpty())
        return;
    Overlay* overlay = makeOverlay(container, area);
    // Behind the panel: the old content while it slides in, the new content once it slides out.
    overlay->background = showing ? crop(before, area) : container->grab(area);
    overlay->panel = showing ? panel->grab() : panelBefore;
    const qreal width = area.width();
    overlay->offset = showing ? width : 0.0;
    overlay->show();
    overlay->raise();
    QPointer<Overlay> guard(overlay);
    animate(overlay, showing ? width : 0.0, showing ? 0.0 : width, duration, [guard](qreal value) {
        if (!guard)
            return;
        guard->offset = value;
        guard->update();
    }, [guard] { if (guard) guard->deleteLater(); });
}

Value::Value(QWidget* owner, qreal initial, int duration)
    : QObject(owner)
    , m_owner(owner)
    , m_value(initial)
    , m_target(initial)
    , m_duration(duration)
{
}

void Value::setValue(qreal value)
{
    if (m_animation)
        m_animation->stop();
    m_target = value;
    if (m_value != value) {
        m_value = value;
        m_owner->update();
    }
}

void Value::animateTo(qreal target)
{
    if (target == m_target)
        return;
    if (!enabled() || !m_owner->isVisible()) {
        setValue(target);
        return;
    }
    m_target = target;
    if (!m_animation) {
        m_animation = new QVariantAnimation(this);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_value = value.toReal();
            m_owner->update();
        });
    }
    m_animation->stop();
    m_animation->setDuration(m_duration);
    m_animation->setStartValue(m_value);
    m_animation->setEndValue(target);
    m_animation->start();
}

ItemAnimator::ItemAnimator(QWidget* viewport, int duration)
    : QObject(viewport)
    , m_viewport(viewport)
    , m_duration(duration)
{
    m_clock.start();
    m_frame.setInterval(FrameInterval);
    m_frame.setTimerType(Qt::PreciseTimer);
    connect(&m_frame, &QTimer::timeout, this, &ItemAnimator::tick);
}

qreal ItemAnimator::valueOf(const Entry& entry, qint64 now) const
{
    const qreal progress = m_duration > 0 ? qreal(now - entry.start) / m_duration : 1.0;
    const qreal to = entry.on ? 1.0 : 0.0;
    return entry.from + (to - entry.from) * ease(progress);
}

qreal ItemAnimator::level(const QString& key, bool on, const QRect& rect)
{
    if (!enabled()) {
        if (!m_entries.isEmpty())
            m_entries.clear();
        return on ? 1.0 : 0.0;
    }
    const qint64 now = m_clock.elapsed();
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        if (!on)
            return 0.0;
        // Safety net: rows that vanished while "on" are never painted "off" again.
        if (m_entries.size() > 2000)
            m_entries.clear();
        it = m_entries.insert(key, Entry{0.0, true, now, rect});
    } else {
        it->rect = rect;
        if (it->on != on) {
            it->from = valueOf(*it, now);
            it->on = on;
            it->start = now;
        }
    }
    const qreal value = valueOf(*it, now);
    if (now - it->start < m_duration) {
        m_until = qMax(m_until, it->start + m_duration);
        if (!m_frame.isActive())
            m_frame.start();
    } else if (!on) {
        m_entries.erase(it);
    }
    return value;
}

void ItemAnimator::tick()
{
    if (!m_viewport) {
        m_frame.stop();
        return;
    }
    const qint64 now = m_clock.elapsed();
    // Repaint the rows still moving, plus the ones that just arrived, so they settle exactly on 0 or 1.
    QRegion region;
    for (const Entry& entry : std::as_const(m_entries)) {
        if (now - entry.start < m_duration + 2 * FrameInterval)
            region += entry.rect;
    }
    if (!region.isEmpty())
        m_viewport->update(region);
    if (now >= m_until + FrameInterval)
        m_frame.stop();
}

} // namespace Motion
