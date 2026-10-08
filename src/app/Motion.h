#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

#include <functional>

class QVariantAnimation;
class QWidget;

// Short UI transitions (hover, popups, page changes). Everything here only does work while an
// animation is running, so an idle window still costs no CPU. "Reduce motion" turns it all off.
namespace Motion {

constexpr int Fast = 120;   // hover and selection
constexpr int Normal = 180; // popups, pages, panels

// False when the user asked for reduced motion (or it was suppressed, e.g. for screenshots).
bool enabled();
bool reduceMotion();
void setReduceMotion(bool reduce);
// Turns animations off for this run without touching the saved setting.
void suppress();

qreal ease(qreal progress); // ease-out cubic, 0..1
QColor mix(const QColor& from, const QColor& to, qreal amount);

// Fades popups, tooltips and dialogs in when they appear. Installed once on the application.
void installWindowFades();
// Fades a top-level window in (window opacity). Call right before show().
void fadeInWindow(QWidget* window, int duration = Normal);
// Fades a child widget in (opacity effect only while it runs). Call after show().
void fadeInWidget(QWidget* widget, int duration = Fast);
// Snapshots `widget` and fades the snapshot out over whatever it shows next. Call before the change.
void crossFade(QWidget* widget, int duration = Normal);
// Runs `change`, which shows or hides `panel` inside `container`, and slides the panel in or out from the
// right edge. The rest of the container is laid out once, not on every frame.
void slidePanel(QWidget* container, QWidget* panel, const std::function<void()>& change, int duration = Normal);

// A number that eases towards its target, repainting `owner` while it moves. Used by custom-painted widgets.
class Value : public QObject
{
public:
    explicit Value(QWidget* owner, qreal initial = 0.0, int duration = Fast);

    qreal value() const { return m_value; }
    qreal target() const { return m_target; }
    void animateTo(qreal target);
    void setValue(qreal value); // jumps without animating
    // Called with every new value, before the owner repaints (e.g. to resize something with it).
    void setOnChange(std::function<void(qreal)> callback) { m_onChange = std::move(callback); }

private:
    void apply(qreal value);

    QWidget* m_owner;
    std::function<void(qreal)> m_onChange;
    QVariantAnimation* m_animation = nullptr;
    qreal m_value;
    qreal m_target;
    int m_duration;
};

// Per-item transitions for item delegates, which have no widget per row. Delegates ask for the level
// (0..1) of a state while painting; the animator remembers where each item was and keeps repainting the
// rows that are still moving. Items seen for the first time fade in only when `on`.
class ItemAnimator : public QObject
{
public:
    explicit ItemAnimator(QWidget* viewport, int duration = Fast);

    qreal level(const QString& key, bool on, const QRect& rect);

private:
    struct Entry
    {
        qreal from = 0.0;
        bool on = false;
        qint64 start = 0;
        QRect rect;
    };

    qreal valueOf(const Entry& entry, qint64 now) const;
    void tick();

    QPointer<QWidget> m_viewport;
    int m_duration;
    QHash<QString, Entry> m_entries;
    QElapsedTimer m_clock;
    QTimer m_frame;
    qint64 m_until = 0;
};

} // namespace Motion
