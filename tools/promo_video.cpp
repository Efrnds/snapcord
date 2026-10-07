// Renders the Snapcord promo video: motion graphics drawn with QPainter and an original soundtrack
// synthesized in code, encoded to H.264/AAC MP4 with Windows Media Foundation. Windows only.
//   snapcord_promo <output.mp4>                      render the video
//   snapcord_promo --frames <dir> <seconds...>       save single frames as PNG (design preview)
//   snapcord_promo --check <file.mp4> <dir> <seconds...>  decode frames of an encoded video as PNG
// The interface shown is an illustration with made-up names; the numbers were measured on a release build.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kFps = 60;
constexpr double kDuration = 44.0;
constexpr int kSampleRate = 48000;
constexpr double kPi = 3.14159265358979323846;

// Music timing: 120 BPM, so a beat is 0.5 s and a bar 2 s. Scene cuts land on bar lines.
constexpr double kBeat = 0.5;
constexpr double kGrooveStart = 6.0;
constexpr double kGrooveEnd = 38.0;

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

// --- Soundtrack ------------------------------------------------------------------------------------

struct Bus {
    std::vector<float> l, r;
    explicit Bus(size_t frames) : l(frames), r(frames) {}
    void add(size_t i, double left, double right)
    {
        if (i < l.size()) {
            l[i] += float(left);
            r[i] += float(right);
        }
    }
};

uint32_t g_noiseState = 0x9e3779b9u;
double noise()
{
    g_noiseState = g_noiseState * 1664525u + 1013904223u;
    return double(g_noiseState >> 8) / 8388608.0 - 1.0;
}

double mtof(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

double polyBlep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double onePole(double cutoff) { return 1.0 - std::exp(-2.0 * kPi * cutoff / kSampleRate); }

struct Chord {
    int bass;
    std::array<int, 4> notes;
};
// Fmaj7 - G - Em7 - Am7, one chord per bar.
constexpr std::array<Chord, 4> kChords = {{
    {41, {53, 57, 60, 64}},
    {43, {55, 59, 62, 67}},
    {40, {52, 55, 59, 62}},
    {45, {57, 60, 64, 67}},
}};
constexpr std::array<int, 6> kFinalChord = {53, 57, 60, 64, 67, 72}; // Fmaj9

void panGains(double pan, double& left, double& right)
{
    const double angle = (pan + 1.0) * kPi / 4.0;
    left = std::cos(angle) * std::sqrt(2.0);
    right = std::sin(angle) * std::sqrt(2.0);
}

void padNote(Bus& bus, int note, double start, double length, double amp)
{
    constexpr double detune[3] = {-0.09, 0.0, 0.09};
    constexpr double pans[3] = {-0.6, 0.0, 0.6};
    constexpr double attack = 0.35, release = 0.7;
    const size_t first = size_t(start * kSampleRate);
    const size_t count = size_t((length + release) * kSampleRate);
    for (int v = 0; v < 3; ++v) {
        const double inc = mtof(note + detune[v]) / kSampleRate;
        double phase = std::fmod(note * 0.37 + v * 0.21, 1.0);
        double lp1 = 0, lp2 = 0, gl, gr;
        panGains(pans[v], gl, gr);
        for (size_t i = 0; i < count; ++i) {
            const double t = double(i) / kSampleRate;
            double env = t < attack ? t / attack : 1.0;
            if (t > length)
                env *= std::max(0.0, 1.0 - (t - length) / release);
            env = env * env * (3.0 - 2.0 * env);
            const double now = start + t;
            const double cutoff = now < kGrooveStart ? 450.0 + now / kGrooveStart * 1000.0 : 1500.0;
            const double a = onePole(cutoff);
            const double saw = 2.0 * phase - 1.0 - polyBlep(phase, inc);
            phase += inc;
            if (phase >= 1.0)
                phase -= 1.0;
            lp1 += a * (saw - lp1);
            lp2 += a * (lp1 - lp2);
            const double s = lp2 * env * amp;
            bus.add(first + i, s * gl, s * gr);
        }
    }
}

void pluck(Bus& bus, int note, double start, double amp, double pan)
{
    const double f = mtof(note);
    double gl, gr;
    panGains(pan, gl, gr);
    const size_t first = size_t(start * kSampleRate);
    const size_t count = size_t(0.7 * kSampleRate);
    for (size_t i = 0; i < count; ++i) {
        const double t = double(i) / kSampleRate;
        const double env = (t < 0.003 ? t / 0.003 : 1.0) * std::exp(-t / 0.14);
        const double w = 2.0 * kPi * f * t;
        const double s = (std::sin(w) + 0.35 * std::sin(2 * w) * std::exp(-t / 0.05)
                          + 0.12 * std::sin(3 * w) * std::exp(-t / 0.03)) * env * amp;
        bus.add(first + i, s * gl, s * gr);
    }
}

void pingPongDelay(Bus& bus, double seconds, double feedback)
{
    const size_t d = size_t(seconds * kSampleRate);
    for (size_t i = d; i < bus.l.size(); ++i) {
        bus.l[i] += float(feedback * bus.r[i - d]);
        bus.r[i] += float(feedback * bus.l[i - d]);
    }
}

void kick(Bus& bus, double start)
{
    const size_t first = size_t(start * kSampleRate);
    double phase = 0;
    for (size_t i = 0; i < size_t(0.45 * kSampleRate); ++i) {
        const double t = double(i) / kSampleRate;
        phase += (48.0 + 110.0 * std::exp(-t * 30.0)) / kSampleRate;
        const double s = std::sin(2 * kPi * phase) * std::exp(-t * 6.5) * 0.62 + noise() * std::exp(-t * 400.0) * 0.12;
        bus.add(first + i, s, s);
    }
}

void clap(Bus& bus, double start)
{
    const size_t first = size_t(start * kSampleRate);
    double lo = 0, hi = 0;
    const double aLo = onePole(700), aHi = onePole(2600);
    for (size_t i = 0; i < size_t(0.3 * kSampleRate); ++i) {
        const double t = double(i) / kSampleRate;
        const double n = noise();
        lo += aLo * (n - lo);
        hi += aHi * (n - hi);
        double env = std::exp(-t * 16.0);
        for (int k = 1; k <= 2; ++k) // the short "flutter" of a hand clap
            if (t < k * 0.011)
                env = std::max(env * 0.6, std::exp(-(t - (k - 1) * 0.011) * 180.0));
        const double s = (hi - lo) * env * 0.55;
        bus.add(first + i, s, s);
    }
}

void hat(Bus& bus, double start, double amp, double pan)
{
    const size_t first = size_t(start * kSampleRate);
    double lp = 0, gl, gr;
    panGains(pan, gl, gr);
    const double a = onePole(7000);
    for (size_t i = 0; i < size_t(0.12 * kSampleRate); ++i) {
        const double t = double(i) / kSampleRate;
        const double n = noise();
        lp += a * (n - lp);
        const double s = (n - lp) * std::exp(-t * 55.0) * amp;
        bus.add(first + i, s * gl, s * gr);
    }
}

void bassNote(Bus& bus, int note, double start)
{
    const double f = mtof(note), sub = mtof(note - 12);
    const size_t first = size_t(start * kSampleRate);
    for (size_t i = 0; i < size_t(0.24 * kSampleRate); ++i) {
        const double t = double(i) / kSampleRate;
        const double env = std::min(1.0, t / 0.005) * (t < 0.17 ? 1.0 : std::max(0.0, 1.0 - (t - 0.17) / 0.06));
        const double s = (0.8 * std::sin(2 * kPi * sub * t) + 0.3 * std::tanh(2.0 * std::sin(2 * kPi * f * t))) * env * 0.24;
        bus.add(first + i, s, s);
    }
}

void impact(Bus& bus, double start)
{
    const size_t first = size_t(start * kSampleRate);
    double lpL = 0, lpR = 0;
    for (size_t i = 0; i < size_t(2.2 * kSampleRate); ++i) {
        const double t = double(i) / kSampleRate;
        const double a = onePole(300.0 + 2500.0 * std::exp(-t * 3.0));
        lpL += a * (noise() - lpL);
        lpR += a * (noise() - lpR);
        const double env = std::exp(-t * 2.2) * 0.16;
        const double boom = std::sin(2 * kPi * 52.0 * t) * std::exp(-t * 4.0) * 0.3;
        bus.add(first + i, lpL * env + boom, lpR * env + boom);
    }
}

void riser(Bus& bus, double start, double end)
{
    const size_t first = size_t(start * kSampleRate);
    double lpL = 0, lpR = 0;
    for (size_t i = 0; i < size_t((end - start) * kSampleRate); ++i) {
        const double x = double(i) / ((end - start) * kSampleRate);
        const double a = onePole(300.0 * std::pow(20.0, x));
        lpL += a * (noise() - lpL);
        lpR += a * (noise() - lpR);
        const double env = 0.09 * x * x;
        bus.add(first + i, lpL * env, lpR * env);
    }
}

bool inGroove(double t) { return t >= kGrooveStart && t < kGrooveEnd; }

// Envelope of the kick drum, also used to make the visuals pulse with the beat.
double pump(double t)
{
    return inGroove(t) ? std::exp(-std::fmod(t - kGrooveStart, kBeat) * 8.0) : 0.0;
}

std::vector<int16_t> renderSoundtrack()
{
    const size_t frames = size_t(kDuration * kSampleRate);
    Bus pad(frames), arp(frames), bass(frames), drums(frames), fx(frames);

    for (int bar = 0; bar < 19; ++bar)
        for (int note : kChords[bar % 4].notes)
            padNote(pad, note, bar * 2.0, 2.0, 0.03);
    for (int note : kFinalChord)
        padNote(pad, note, kGrooveEnd, 3.6, 0.03);

    static constexpr int pattern[16] = {0, 1, 2, 3, 1, 2, 3, 2, 0, 2, 1, 3, 2, 3, 1, 2};
    for (int step = 0; step * 0.125 < kGrooveEnd; ++step) {
        const double t = step * 0.125;
        const bool intro = t < kGrooveStart;
        if (intro && step % 2)
            continue;
        const Chord& chord = kChords[(step / 16) % 4];
        double amp = intro ? 0.03 + 0.02 * t / kGrooveStart : 0.07;
        if (step % 4 == 0 && !intro)
            amp *= 1.2;
        pluck(arp, chord.notes[size_t(pattern[step % 16])] + 12, t, amp, step % 2 ? 0.35 : -0.35);
    }
    for (int step = 0; step < 14; ++step) // the arpeggio slows down and fades over the final chord
        pluck(arp, kFinalChord[size_t(step % 6)] + 12, kGrooveEnd + step * 0.25, 0.07 * std::pow(0.85, step), step % 2 ? 0.4 : -0.4);
    pingPongDelay(arp, 0.375, 0.38);

    for (double t = kGrooveStart; t < kGrooveEnd - 0.01; t += kBeat) {
        kick(drums, t);
        const double inBar = std::fmod(t, 2.0);
        if (std::abs(inBar - 0.5) < 0.01 || std::abs(inBar - 1.5) < 0.01)
            clap(drums, t);
        hat(drums, t + 0.25, 0.13, 0.2);
        if (t >= 12.0) {
            hat(drums, t + 0.125, 0.04, -0.25);
            hat(drums, t + 0.375, 0.04, -0.25);
        }
        bassNote(bass, kChords[size_t(int(t / 2.0) % 4)].bass, t + 0.25);
    }

    riser(fx, 3.0, kGrooveStart);
    riser(fx, 18.5, 20.0);
    riser(fx, 28.5, 30.0);
    riser(fx, 36.5, kGrooveEnd);
    for (double t : {kGrooveStart, 12.0, 20.0, 30.0, kGrooveEnd})
        impact(fx, t);

    std::vector<double> mix(frames * 2);
    double peak = 0;
    for (size_t i = 0; i < frames; ++i) {
        const double t = double(i) / kSampleRate;
        const double duck = pump(t);
        const double padGain = 1.0 - 0.55 * duck, arpGain = 1.0 - 0.25 * duck;
        double fade = std::min(1.0, t / 0.05);
        if (t > 41.0) {
            const double x = std::min(1.0, (t - 41.0) / (kDuration - 41.0));
            fade *= 1.0 - x * x * (3.0 - 2.0 * x);
        }
        for (int ch = 0; ch < 2; ++ch) {
            auto pick = [&](const Bus& b) { return double(ch ? b.r[i] : b.l[i]); };
            double s = pick(pad) * padGain + pick(arp) * arpGain + pick(bass) + pick(drums) + pick(fx);
            s = std::tanh(s * 1.1) * fade;
            mix[i * 2 + size_t(ch)] = s;
            peak = std::max(peak, std::abs(s));
        }
    }
    const double gain = peak > 0 ? 0.89 / peak : 1.0;
    std::vector<int16_t> pcm(mix.size());
    for (size_t i = 0; i < mix.size(); ++i)
        pcm[i] = int16_t(std::lround(std::clamp(mix[i] * gain, -1.0, 1.0) * 32767.0));
    return pcm;
}

// --- Drawing helpers -------------------------------------------------------------------------------

QColor hex(uint32_t rgb, double alpha = 1.0)
{
    QColor c(int((rgb >> 16) & 0xff), int((rgb >> 8) & 0xff), int(rgb & 0xff));
    c.setAlphaF(float(alpha));
    return c;
}

constexpr uint32_t kBlurple = 0x5865f2, kGreen = 0x23a55a, kRed = 0xf23f43, kCyan = 0x00a8fc,
                   kText = 0xf2f3f5, kBody = 0xdbdee1, kMuted = 0xb5bac1, kDim = 0x949ba4,
                   kRail = 0x1e1f22, kPanel = 0x2b2d31, kMain = 0x313338, kInput = 0x383a40, kUserBar = 0x232428;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
double ramp(double t, double a, double b) { return clamp01((t - a) / (b - a)); }
double easeOut(double x) { return 1.0 - std::pow(1.0 - clamp01(x), 3.0); }
double easeInOut(double x)
{
    x = clamp01(x);
    return x < 0.5 ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3) / 2;
}
double easeOutBack(double x)
{
    x = clamp01(x);
    constexpr double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}
double reveal(double t, double start, double dur = 0.6) { return easeOut(ramp(t, start, start + dur)); }

QFont font(double px, QFont::Weight weight = QFont::Normal, const char* family = "Segoe UI")
{
    QFont f(QString::fromLatin1(family));
    f.setPixelSize(std::max(1, int(std::lround(px))));
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

double textWidth(const QString& s, const QFont& f) { return QFontMetricsF(f).horizontalAdvance(s); }

// Draws text with y as its vertical centre; returns the width.
double drawText(QPainter& p, double x, double y, const QString& s, const QFont& f, const QColor& color,
                Qt::Alignment align = Qt::AlignLeft)
{
    const QFontMetricsF fm(f);
    const double w = fm.horizontalAdvance(s);
    if (align & Qt::AlignHCenter)
        x -= w / 2;
    else if (align & Qt::AlignRight)
        x -= w;
    p.setFont(f);
    p.setPen(color);
    p.drawText(QPointF(x, y + (fm.ascent() - fm.descent()) / 2), s);
    return w;
}

// Fades and slides everything painted while it is alive.
class Layer {
public:
    Layer(QPainter& p, double opacity, double dy = 0, double dx = 0, double scale = 1, QPointF origin = {})
        : m_p(p)
    {
        m_p.save();
        m_p.setOpacity(m_p.opacity() * clamp01(opacity));
        m_p.translate(dx, dy);
        if (scale != 1) {
            m_p.translate(origin);
            m_p.scale(scale, scale);
            m_p.translate(-origin);
        }
    }
    ~Layer() { m_p.restore(); }
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

private:
    QPainter& m_p;
};

// Typical "fade up" entrance.
struct Rise : Layer {
    Rise(QPainter& p, double t, double start, double distance = 40, double dur = 0.6)
        : Layer(p, reveal(t, start, dur), (1 - reveal(t, start, dur)) * distance)
    {
    }
};

void drawLogo(QPainter& p, QPointF c, double size, double t, double energy)
{
    const double s = size / 64.0;
    const QRectF r(c.x() - size / 2, c.y() - size / 2, size, size);
    p.setPen(Qt::NoPen);
    p.setBrush(hex(kBlurple));
    p.drawRoundedRect(r, 16 * s, 16 * s);
    static constexpr double xs[4] = {18, 27, 36, 45}, lengths[4] = {12, 28, 20, 8};
    p.setPen(QPen(Qt::white, 5 * s, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 4; ++i) {
        const double wobble = 0.5 + 0.5 * std::sin(t * (6.0 + i * 2.3) + i * 1.7);
        const double len = std::clamp(lengths[i] * (1.0 - 0.3 * energy + 0.6 * energy * wobble), 3.0, 40.0);
        const double x = r.left() + xs[i] * s;
        p.drawLine(QPointF(x, c.y() - len / 2 * s), QPointF(x, c.y() + len / 2 * s));
    }
}

void glow(QPainter& p, QPointF c, double radius, const QColor& color)
{
    QRadialGradient g(c, radius);
    g.setColorAt(0, color);
    QColor clear = color;
    clear.setAlpha(0);
    g.setColorAt(1, clear);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, radius, radius);
}

void avatar(QPainter& p, QPointF c, double r, uint32_t color, const QString& initial)
{
    p.setPen(Qt::NoPen);
    p.setBrush(hex(color));
    p.drawEllipse(c, r, r);
    drawText(p, c.x(), c.y(), initial, font(r * 0.95, QFont::DemiBold), Qt::white, Qt::AlignHCenter);
}

void statusDot(QPainter& p, QPointF c, double r, uint32_t color, uint32_t ring)
{
    p.setPen(Qt::NoPen);
    p.setBrush(hex(ring));
    p.drawEllipse(c, r + 2.5, r + 2.5);
    p.setBrush(hex(color));
    p.drawEllipse(c, r, r);
}

void micIcon(QPainter& p, QPointF c, double s, const QColor& color)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(c.x() - 3 * s, c.y() - 9 * s, 6 * s, 12 * s), 3 * s, 3 * s);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 2 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(c.x() - 6.5 * s, c.y() - 7.5 * s, 13 * s, 13 * s), 180 * 16, 180 * 16);
    p.drawLine(QPointF(c.x(), c.y() + 5.5 * s), QPointF(c.x(), c.y() + 9 * s));
}

void headphonesIcon(QPainter& p, QPointF c, double s, const QColor& color)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 2 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(c.x() - 8 * s, c.y() - 8 * s, 16 * s, 16 * s), 0, 180 * 16);
    p.drawLine(QPointF(c.x() - 8 * s, c.y()), QPointF(c.x() - 8 * s, c.y() + 3 * s));
    p.drawLine(QPointF(c.x() + 8 * s, c.y()), QPointF(c.x() + 8 * s, c.y() + 3 * s));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(c.x() - 9 * s, c.y() + 1.5 * s, 5 * s, 7.5 * s), 2 * s, 2 * s);
    p.drawRoundedRect(QRectF(c.x() + 4 * s, c.y() + 1.5 * s, 5 * s, 7.5 * s), 2 * s, 2 * s);
}

void speakerIcon(QPainter& p, QPointF c, double s, const QColor& color)
{
    QPainterPath body;
    body.moveTo(c.x() - 7 * s, c.y() - 3 * s);
    body.lineTo(c.x() - 3 * s, c.y() - 3 * s);
    body.lineTo(c.x() + 2 * s, c.y() - 7.5 * s);
    body.lineTo(c.x() + 2 * s, c.y() + 7.5 * s);
    body.lineTo(c.x() - 3 * s, c.y() + 3 * s);
    body.lineTo(c.x() - 7 * s, c.y() + 3 * s);
    body.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(body);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 1.8 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(c.x() - 2 * s, c.y() - 5 * s, 10 * s, 10 * s), -50 * 16, 100 * 16);
}

// --- Illustrated client window (1280 x 720 design units) -------------------------------------------

struct Run {
    QString text;
    enum Style { Normal, Bold, Code, Mention } style = Normal;
};

void drawRuns(QPainter& p, double x, double y, const std::vector<Run>& runs, double px)
{
    for (const Run& run : runs) {
        QFont f = run.style == Run::Code ? font(px * 0.9, QFont::Normal, "Consolas")
                                         : font(px, run.style == Run::Bold ? QFont::Bold
                                                    : run.style == Run::Mention ? QFont::DemiBold
                                                                                : QFont::Normal);
        const double w = textWidth(run.text, f);
        QColor color = hex(kBody);
        if (run.style == Run::Code) {
            p.setPen(QPen(hex(0x1e1f22), 1));
            p.setBrush(hex(kPanel));
            p.drawRoundedRect(QRectF(x - 1, y - px * 0.72, w + 6, px * 1.44), 4, 4);
            x += 3;
        } else if (run.style == Run::Mention) {
            p.setPen(Qt::NoPen);
            p.setBrush(hex(kBlurple, 0.3));
            p.drawRoundedRect(QRectF(x - 2, y - px * 0.7, w + 4, px * 1.4), 3, 3);
            color = hex(0xc9cdfb);
        } else if (run.style == Run::Bold) {
            color = hex(kText);
        }
        drawText(p, x, y, run.text, f, color);
        x += w + (run.style == Run::Code ? 3 : 0);
    }
}

void message(QPainter& p, double y, uint32_t color, const QString& name, const QString& time,
             const std::vector<Run>& text)
{
    avatar(p, {352, y + 20}, 20, color, name.left(1));
    const QFont nameFont = font(16, QFont::DemiBold);
    const double w = drawText(p, 384, y + 8, name, nameFont, hex(color == kBlurple ? kText : color));
    drawText(p, 392 + w, y + 9, time, font(12), hex(kDim));
    drawRuns(p, 384, y + 32, text, 16);
}

// speakingSeed animates who is talking; newMessage (0..1) slides in a fresh message.
void drawClient(QPainter& p, double t, double newMessage)
{
    p.save();
    QPainterPath frame;
    frame.addRoundedRect(QRectF(0, 0, 1280, 720), 12, 12);
    p.setClipPath(frame);
    p.fillRect(QRectF(0, 0, 1280, 720), hex(kMain));

    // Title bar and server rail.
    p.fillRect(QRectF(0, 0, 1280, 30), hex(kRail));
    drawText(p, 14, 15, QStringLiteral("Snapcord"), font(12, QFont::DemiBold), hex(kDim));
    p.setPen(QPen(hex(kDim), 1.4));
    p.drawLine(QPointF(1150, 15), QPointF(1160, 15));
    p.drawRect(QRectF(1196, 10, 10, 10));
    p.drawLine(QPointF(1242, 10), QPointF(1252, 20));
    p.drawLine(QPointF(1252, 10), QPointF(1242, 20));
    p.fillRect(QRectF(0, 30, 72, 690), hex(kRail));
    drawLogo(p, {36, 66}, 48, t, 0);
    p.fillRect(QRectF(24, 99, 24, 2), hex(0x35363c));
    struct Server { uint32_t color; const char* initials; };
    static constexpr Server servers[] = {{0xeb459e, "NO"}, {0x23a55a, "DV"}, {0xf0b232, "MU"}, {0x00a8fc, "PX"}, {0xed4245, "LO"}};
    for (int i = 0; i < 5; ++i) {
        const QPointF c(36, 136 + i * 58);
        p.setPen(Qt::NoPen);
        p.setBrush(hex(servers[i].color));
        if (i == 0) {
            p.drawRoundedRect(QRectF(c.x() - 24, c.y() - 24, 48, 48), 16, 16);
            p.setBrush(Qt::white);
            p.drawRoundedRect(QRectF(-4, c.y() - 20, 8, 40), 4, 4);
        } else {
            p.drawEllipse(c, 24, 24);
        }
        drawText(p, c.x(), c.y(), QString::fromLatin1(servers[i].initials), font(15, QFont::DemiBold), Qt::white, Qt::AlignHCenter);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(hex(0x313338));
    p.drawEllipse(QPointF(36, 136 + 5 * 58), 24, 24);
    drawText(p, 36, 136 + 5 * 58, QStringLiteral("+"), font(26), hex(kGreen), Qt::AlignHCenter);

    // Channel list.
    p.fillRect(QRectF(72, 30, 240, 690), hex(kPanel));
    drawText(p, 88, 54, QStringLiteral("Night Owls"), font(16, QFont::Bold), hex(kText));
    p.fillRect(QRectF(72, 77, 240, 1.5), hex(0x1f2023));
    const QFont category = font(11.5, QFont::DemiBold);
    drawText(p, 84, 102, QStringLiteral("TEXT CHANNELS"), category, hex(kDim));
    const char* textChannels[] = {"general", "memes", "music-share"};
    for (int i = 0; i < 3; ++i) {
        const double y = 128 + i * 32;
        if (i == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(hex(0x404249));
            p.drawRoundedRect(QRectF(80, y - 15, 224, 30), 5, 5);
        }
        drawText(p, 92, y, QStringLiteral("#"), font(20), hex(kDim));
        drawText(p, 114, y, QString::fromLatin1(textChannels[i]), font(15.5, i == 0 ? QFont::DemiBold : QFont::Normal),
                 hex(i == 0 ? kText : kDim));
    }
    drawText(p, 84, 234, QStringLiteral("VOICE CHANNELS"), category, hex(kDim));
    speakerIcon(p, {100, 262}, 1.0, hex(kDim));
    drawText(p, 114, 262, QStringLiteral("Lounge"), font(15.5, QFont::DemiBold), hex(kText));
    struct Member { uint32_t color; const char* name; };
    static constexpr Member voice[] = {{0xf0b232, "Alex"}, {0xeb459e, "Bia"}, {0x00a8fc, "Kenji"}, {kBlurple, "you"}};
    for (int i = 0; i < 4; ++i) {
        const QPointF c(124, 292 + i * 30);
        const double phase = std::fmod(t * 0.55 + i * 0.29, 1.0);
        const bool speaking = phase < 0.4 && !(i == 3 && t < 2);
        if (speaking) {
            p.setPen(QPen(hex(kGreen), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 13, 13);
        }
        avatar(p, c, 11, voice[i].color, QString::fromLatin1(voice[i].name).left(1).toUpper());
        drawText(p, 144, c.y(), QString::fromLatin1(voice[i].name), font(14.5), hex(speaking ? kText : kMuted));
    }
    speakerIcon(p, {100, 418}, 1.0, hex(kDim));
    drawText(p, 114, 418, QStringLiteral("Gaming"), font(15.5), hex(kDim));

    // Voice and user panels.
    p.fillRect(QRectF(72, 604, 240, 116), hex(kUserBar));
    drawText(p, 88, 622, QStringLiteral("Voice Connected"), font(14, QFont::DemiBold), hex(kGreen));
    drawText(p, 88, 642, QStringLiteral("Lounge / Night Owls"), font(12.5), hex(kDim));
    p.setPen(Qt::NoPen);
    p.setBrush(hex(0x3a3c42));
    p.drawRoundedRect(QRectF(268, 616, 32, 32), 6, 6);
    p.setPen(QPen(hex(kRed), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(276, 627, 16, 12), 20 * 16, 140 * 16);
    p.fillRect(QRectF(80, 659, 224, 1), hex(0x2e3035));
    avatar(p, {102, 690}, 16, kBlurple, QStringLiteral("Y"));
    statusDot(p, {114, 702}, 5, kGreen, kUserBar);
    drawText(p, 128, 682, QStringLiteral("you"), font(14.5, QFont::DemiBold), hex(kText));
    drawText(p, 128, 700, QStringLiteral("Online"), font(12.5), hex(kDim));
    micIcon(p, {250, 690}, 1.0, hex(kMuted));
    headphonesIcon(p, {282, 688}, 1.0, hex(kMuted));

    // Chat.
    drawText(p, 330, 54, QStringLiteral("#"), font(24), hex(kDim));
    drawText(p, 354, 54, QStringLiteral("general"), font(16.5, QFont::Bold), hex(kText));
    p.fillRect(QRectF(312, 77, 736, 1.5), hex(0x2a2b30));
    message(p, 96, 0xf0b232, QStringLiteral("Alex"), QStringLiteral("Today at 21:04"),
            {{QStringLiteral("anyone up for a match tonight?")}});
    message(p, 170, 0xeb459e, QStringLiteral("Bia"), QStringLiteral("Today at 21:05"),
            {{QStringLiteral("yes!"), Run::Bold}, {QStringLiteral(" ")}, {QStringLiteral("@Alex"), Run::Mention}, {QStringLiteral(" see you in Lounge")}});
    // A reply with an image and reactions.
    p.setPen(QPen(hex(0x4e5058), 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(352, 252, 26, 20), 90 * 16, 90 * 16);
    avatar(p, {394, 252}, 8, 0xeb459e, QStringLiteral("B"));
    drawText(p, 408, 252, QStringLiteral("@Bia"), font(13, QFont::DemiBold), hex(0xeb459e));
    drawText(p, 446, 252, QStringLiteral("yes! @Alex see you in Lounge"), font(13), hex(kDim));
    message(p, 262, 0x00a8fc, QStringLiteral("Kenji"), QStringLiteral("Today at 21:06"),
            {{QStringLiteral("that ")}, {QStringLiteral("gg"), Run::Code}, {QStringLiteral(" from last night was ")}, {QStringLiteral("wild"), Run::Bold}});
    {
        const QRectF image(384, 312, 260, 146);
        QPainterPath clip;
        clip.addRoundedRect(image, 8, 8);
        p.save();
        p.setClipPath(clip, Qt::IntersectClip);
        QLinearGradient sky(image.topLeft(), image.bottomLeft());
        sky.setColorAt(0, hex(0x5865f2));
        sky.setColorAt(1, hex(0xeb459e));
        p.fillRect(image, sky);
        p.setPen(Qt::NoPen);
        p.setBrush(hex(0xffe08a));
        p.drawEllipse(QPointF(image.left() + 190, image.top() + 52), 22, 22);
        QPainterPath hills;
        hills.moveTo(image.bottomLeft());
        hills.lineTo(image.left(), image.top() + 110);
        hills.quadTo(image.left() + 70, image.top() + 60, image.left() + 140, image.top() + 104);
        hills.quadTo(image.left() + 200, image.top() + 80, image.right(), image.top() + 96);
        hills.lineTo(image.bottomRight());
        hills.closeSubpath();
        p.setBrush(hex(0x1e1f22, 0.85));
        p.drawPath(hills);
        p.restore();
    }
    auto reaction = [&](double x, const QString& glyph, const QString& count, bool mine) {
        const QRectF r(x, 468, 56, 28);
        p.setPen(QPen(hex(mine ? kBlurple : 0x3f4147), 1.2));
        p.setBrush(hex(mine ? 0x373a5c : kPanel));
        p.drawRoundedRect(r, 8, 8);
        drawText(p, x + 12, r.center().y(), glyph, font(15, QFont::Normal, "Segoe UI Symbol"), hex(mine ? 0xff6b8a : 0xf0b232));
        drawText(p, x + 32, r.center().y(), count, font(14, QFont::DemiBold), hex(mine ? 0xc9cdfb : kMuted));
    };
    reaction(384, QStringLiteral("♥"), QStringLiteral("3"), true);
    reaction(446, QStringLiteral("★"), QStringLiteral("1"), false);
    if (newMessage > 0) {
        Layer layer(p, newMessage, (1 - newMessage) * 24);
        message(p, 520, kBlurple, QStringLiteral("you"), QStringLiteral("Today at 21:07"),
                {{QStringLiteral("joining now, noise suppression is ")}, {QStringLiteral("on"), Run::Bold}});
    }
    p.setPen(Qt::NoPen);
    p.setBrush(hex(kInput));
    p.drawRoundedRect(QRectF(328, 650, 704, 50), 8, 8);
    p.setBrush(hex(kMuted));
    p.drawEllipse(QPointF(356, 675), 11, 11);
    drawText(p, 356, 674, QStringLiteral("+"), font(20, QFont::Bold), hex(kInput), Qt::AlignHCenter);
    drawText(p, 380, 675, QStringLiteral("Message #general"), font(15.5), hex(0x6d6f78));

    // Member list.
    p.fillRect(QRectF(1048, 30, 232, 690), hex(kPanel));
    drawText(p, 1064, 60, QStringLiteral("ONLINE — 4"), category, hex(kDim));
    static constexpr Member members[] = {{0xf0b232, "Alex"}, {0xeb459e, "Bia"}, {0x00a8fc, "Kenji"}, {kBlurple, "you"}};
    for (int i = 0; i < 4; ++i) {
        const QPointF c(1080, 96 + i * 46);
        avatar(p, c, 16, members[i].color, QString::fromLatin1(members[i].name).left(1).toUpper());
        statusDot(p, {c.x() + 12, c.y() + 12}, 5, kGreen, kPanel);
        drawText(p, 1106, c.y(), QString::fromLatin1(members[i].name), font(15.5, QFont::DemiBold), hex(members[i].color == kBlurple ? kText : members[i].color));
    }
    drawText(p, 1064, 290, QStringLiteral("OFFLINE — 2"), category, hex(kDim));
    static constexpr Member offline[] = {{0x80848e, "Marina"}, {0x80848e, "Theo"}};
    for (int i = 0; i < 2; ++i) {
        Layer dim(p, 0.45);
        const QPointF c(1080, 326 + i * 46);
        avatar(p, c, 16, offline[i].color, QString::fromLatin1(offline[i].name).left(1));
        drawText(p, 1106, c.y(), QString::fromLatin1(offline[i].name), font(15.5, QFont::DemiBold), hex(kMuted));
    }
    p.restore();
}

void windowShadow(QPainter& p, const QRectF& r, double radius)
{
    p.setPen(Qt::NoPen);
    for (int i = 8; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawRoundedRect(r.adjusted(-i * 3, -i * 2, i * 3, i * 4), radius + i * 3, radius + i * 3);
    }
}

void drawClientAt(QPainter& p, const QRectF& r, double t, double newMessage)
{
    windowShadow(p, r, 12 * r.width() / 1280);
    p.save();
    p.translate(r.topLeft());
    p.scale(r.width() / 1280.0, r.height() / 720.0);
    drawClient(p, t, newMessage);
    p.restore();
}

void card(QPainter& p, const QRectF& r, double radius = 24)
{
    p.setPen(QPen(hex(0x34363c), 1.5));
    p.setBrush(hex(0x232428, 0.92));
    p.drawRoundedRect(r, radius, radius);
}

void pill(QPainter& p, QPointF center, const QString& label, double px, const QColor& accent)
{
    const QFont f = font(px, QFont::DemiBold);
    const double w = textWidth(label, f) + px * 2.2, h = px * 2.1;
    const QRectF r(center.x() - w / 2, center.y() - h / 2, w, h);
    p.setPen(QPen(accent, 1.6));
    QColor fill = accent;
    fill.setAlphaF(0.12f);
    p.setBrush(fill);
    p.drawRoundedRect(r, h / 2, h / 2);
    drawText(p, center.x(), center.y(), label, f, hex(kText), Qt::AlignHCenter);
}

void heading(QPainter& p, double t, double start, double y, const QString& title, const QString& subtitle)
{
    {
        Rise rise(p, t, start, 36);
        drawText(p, kWidth / 2.0, y, title, font(84, QFont::Black), hex(kText), Qt::AlignHCenter);
    }
    if (!subtitle.isEmpty()) {
        Rise rise(p, t, start + 0.25, 36);
        drawText(p, kWidth / 2.0, y + 78, subtitle, font(34), hex(kMuted), Qt::AlignHCenter);
    }
}

// --- Scenes ----------------------------------------------------------------------------------------

void background(QPainter& p, double t)
{
    QLinearGradient g(0, 0, 0, kHeight);
    g.setColorAt(0, hex(0x1a1b1f));
    g.setColorAt(1, hex(0x111214));
    p.fillRect(QRectF(0, 0, kWidth, kHeight), g);
    const double beat = pump(t);
    const double intro = easeOut(ramp(t, 0, 1.5));
    glow(p, {kWidth * (0.25 + 0.06 * std::sin(t * 0.21)), kHeight * (0.3 + 0.08 * std::cos(t * 0.17))}, 900,
         hex(kBlurple, (0.20 + 0.06 * beat) * intro));
    glow(p, {kWidth * (0.8 + 0.05 * std::cos(t * 0.19)), kHeight * (0.78 + 0.06 * std::sin(t * 0.23))}, 800,
         hex(kCyan, (0.10 + 0.04 * beat) * intro));
    // A faint dot grid gives depth without distracting.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 10));
    const double drift = std::fmod(t * 6.0, 48.0);
    for (double y = -48 + drift; y < kHeight; y += 48)
        for (double x = 24; x < kWidth; x += 48)
            p.drawEllipse(QPointF(x, y), 1.4, 1.4);
}

void sceneIntro(QPainter& p, double t)
{
    const QPointF center(kWidth / 2.0, 430);
    const double pop = easeOutBack(ramp(t, 0.4, 1.3));
    glow(p, center, 380 * easeOut(ramp(t, 0.3, 1.6)), hex(kBlurple, 0.45));
    {
        Layer layer(p, ramp(t, 0.4, 0.7), 0, 0, std::max(0.01, pop), center);
        p.save();
        p.translate(center);
        p.rotate(-10 * (1 - pop));
        p.translate(-center);
        const double energy = 0.35 + 0.35 * std::exp(-std::fmod(t, 0.25) * 9.0) * ramp(t, 1.0, 2.0);
        drawLogo(p, center, 230, t, energy);
        p.restore();
    }
    {
        Rise rise(p, t, 1.5, 50);
        drawText(p, kWidth / 2.0, 655, QStringLiteral("Snapcord"), font(132, QFont::Black), hex(kText), Qt::AlignHCenter);
    }
    {
        Rise rise(p, t, 2.1, 30);
        drawText(p, kWidth / 2.0, 778, QStringLiteral("The lightweight Discord client, built for voice."), font(40), hex(kMuted), Qt::AlignHCenter);
    }
    const QString tags[] = {QStringLiteral("Native"), QStringLiteral("Open source"), QStringLiteral("Voice first")};
    const uint32_t colors[] = {kBlurple, kGreen, kCyan};
    for (int i = 0; i < 3; ++i) {
        Rise rise(p, t, 3.0 + i * 0.22, 24, 0.5);
        pill(p, {kWidth / 2.0 + (i - 1) * 270, 870}, tags[i], 26, hex(colors[i]));
    }
}

void cursor(QPainter& p, QPointF tip, double scale)
{
    QPainterPath arrow;
    arrow.moveTo(0, 0);
    arrow.lineTo(0, 30);
    arrow.lineTo(8, 23);
    arrow.lineTo(13.5, 35);
    arrow.lineTo(18.5, 33);
    arrow.lineTo(13, 21);
    arrow.lineTo(23, 21);
    arrow.closeSubpath();
    p.save();
    p.translate(tip);
    p.scale(scale, scale);
    p.setPen(QPen(QColor(20, 20, 24), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    p.drawPath(arrow);
    p.restore();
}

void sceneFast(QPainter& p, double t)
{
    heading(p, t, 0.15, 170, QStringLiteral("Opens in a blink."), QStringLiteral("From click to window in about a quarter of a second."));

    constexpr double clickAt = 1.6, measured = 0.27;
    const QPointF icon(kWidth / 2.0, 690);
    const QRectF window(520, 440, 880, 495);
    const double open = ramp(t, clickAt, clickAt + measured);

    // Timer.
    const double elapsed = std::clamp(t - clickAt, 0.0, measured);
    {
        Rise rise(p, t, 0.6, 20);
        const bool done = t >= clickAt + measured;
        const QString value = QString::number(elapsed, 'f', 2) + QStringLiteral(" s");
        const QFont f = font(64, QFont::Black);
        const double w = textWidth(QStringLiteral("0.00 s"), f);
        p.setPen(Qt::NoPen);
        p.setBrush(hex(done ? kGreen : 0x2b2d31, done ? 0.18 : 0.9));
        p.drawRoundedRect(QRectF(kWidth / 2.0 - w / 2 - 70, 350 - 46, w + 140, 92), 46, 46);
        if (done) {
            p.setPen(QPen(hex(kGreen), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const double cx = kWidth / 2.0 - w / 2 - 30;
            p.drawPolyline(QPolygonF({QPointF(cx - 12, 350), QPointF(cx - 3, 360), QPointF(cx + 13, 338)}));
        }
        drawText(p, kWidth / 2.0 + (done ? 14 : 0), 350, value, f, hex(done ? 0x57f287 : kText), Qt::AlignHCenter);
    }

    if (t < clickAt + 0.05) {
        // The app icon waiting to be clicked, and the cursor travelling to it.
        Rise rise(p, t, 0.8, 30);
        const double press = t > clickAt - 0.08 ? 0.9 : 1.0;
        {
            Layer layer(p, 1, 0, 0, press, icon);
            drawLogo(p, icon, 150, t, 0.2);
        }
        drawText(p, icon.x(), icon.y() + 115, QStringLiteral("Snapcord"), font(26, QFont::DemiBold), hex(kBody), Qt::AlignHCenter);
        const double move = easeInOut(ramp(t, 0.9, clickAt - 0.1));
        const QPointF from(1500, 980), to(icon.x() + 20, icon.y() + 18);
        cursor(p, from + (to - from) * move, press);
    }
    if (t >= clickAt) {
        // Ripple of the click.
        const double r = ramp(t, clickAt, clickAt + 0.6);
        p.setPen(QPen(hex(kBlurple, 0.6 * (1 - r)), 4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(icon, 80 + 260 * r, 80 + 260 * r);
        // The window springs open from the icon.
        const double s = easeOutBack(open);
        const QPointF c = icon + (window.center() - icon) * easeOut(open);
        const QRectF r2(c.x() - window.width() / 2 * s, c.y() - window.height() / 2 * s, window.width() * s, window.height() * s);
        Layer layer(p, ramp(t, clickAt, clickAt + 0.08));
        if (r2.width() > 4)
            drawClientAt(p, r2, t, 0);
    }
    {
        Layer note(p, ramp(t, 2.6, 3.2));
        drawText(p, kWidth / 2.0, 1010, QStringLiteral("Measured on a 4-core PC with Windows 10."), font(22), hex(kDim), Qt::AlignHCenter);
    }
}

void sceneLight(QPainter& p, double t)
{
    heading(p, t, 0.15, 170, QStringLiteral("Light on your PC."), QStringLiteral("No embedded browser. Native C++ and Qt."));

    struct Stat {
        double value;
        int decimals;
        QString unit, label, caption;
        uint32_t accent;
    };
    const Stat stats[] = {
        {36, 0, QStringLiteral("MB"), QStringLiteral("of RAM"), QStringLiteral("logged in, every server loaded"), kBlurple},
        {0, 0, QStringLiteral("%"), QStringLiteral("CPU while idle"), QStringLiteral("event-driven, nothing polling"), kGreen},
        {15, 0, QStringLiteral("MB"), QStringLiteral("download"), QStringLiteral("portable Windows build"), kCyan},
    };
    for (int i = 0; i < 3; ++i) {
        const double start = 0.8 + i * 0.35;
        const double e = reveal(t, start, 0.7);
        const QRectF r(150 + i * 560, 360, 500, 470);
        Layer layer(p, e, (1 - e) * 60);
        card(p, r);
        const Stat& s = stats[size_t(i)];
        // Icon badge.
        const QPointF badge(r.left() + 70, r.top() + 72);
        p.setPen(Qt::NoPen);
        p.setBrush(hex(s.accent, 0.18));
        p.drawEllipse(badge, 34, 34);
        p.setPen(QPen(hex(s.accent), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        if (i == 0) {
            p.drawRoundedRect(QRectF(badge.x() - 16, badge.y() - 12, 32, 24), 4, 4);
            for (int k = -1; k <= 1; ++k) {
                p.drawLine(QPointF(badge.x() + k * 9, badge.y() - 12), QPointF(badge.x() + k * 9, badge.y() - 18));
                p.drawLine(QPointF(badge.x() + k * 9, badge.y() + 12), QPointF(badge.x() + k * 9, badge.y() + 18));
            }
        } else if (i == 1) {
            p.drawPolyline(QPolygonF({QPointF(badge.x() - 18, badge.y()), QPointF(badge.x() - 8, badge.y()),
                                      QPointF(badge.x() - 3, badge.y() - 12), QPointF(badge.x() + 4, badge.y() + 12),
                                      QPointF(badge.x() + 9, badge.y()), QPointF(badge.x() + 18, badge.y())}));
        } else {
            p.drawLine(QPointF(badge.x(), badge.y() - 16), QPointF(badge.x(), badge.y() + 6));
            p.drawPolyline(QPolygonF({QPointF(badge.x() - 9, badge.y() - 3), QPointF(badge.x(), badge.y() + 6), QPointF(badge.x() + 9, badge.y() - 3)}));
            p.drawLine(QPointF(badge.x() - 14, badge.y() + 15), QPointF(badge.x() + 14, badge.y() + 15));
        }
        // Counting number.
        const double count = s.value * easeOut(ramp(t, start + 0.2, start + 1.8));
        const QFont big = font(150, QFont::Black);
        const QString number = QString::number(std::round(count), 'f', s.decimals);
        const double w = drawText(p, r.left() + 56, r.top() + 225, number, big, hex(kText));
        drawText(p, r.left() + 66 + w, r.top() + 252, s.unit, font(56, QFont::Bold), hex(s.accent));
        drawText(p, r.left() + 60, r.top() + 336, s.label, font(36, QFont::DemiBold), hex(kText));
        drawText(p, r.left() + 60, r.top() + 384, s.caption, font(25), hex(kDim));
        // Activity line along the bottom: flat for CPU, a short level bar for the others.
        const double line = easeOut(ramp(t, start + 0.3, start + 2.4));
        const QRectF track(r.left() + 60, r.bottom() - 46, r.width() - 120, 6);
        if (i == 1) {
            QPainterPath path;
            path.moveTo(track.left(), track.center().y());
            for (double x = 0; x <= line; x += 0.01) {
                const double blip = std::fmod(x * 7.3 + t * 0.4, 1.0) < 0.03 ? -5 : 0;
                path.lineTo(track.left() + track.width() * x, track.center().y() + blip);
            }
            p.setPen(QPen(hex(kGreen), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(hex(0x3a3c42));
            p.drawRoundedRect(track, 3, 3);
            p.setBrush(hex(s.accent));
            p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * 0.12 * line, track.height()), 3, 3);
        }
    }
    Layer note(p, ramp(t, 3.0, 3.6));
    drawText(p, kWidth / 2.0, 920, QStringLiteral("Measured on a release build: Windows 10, 4-core PC."), font(22), hex(kDim), Qt::AlignHCenter);
}

void sceneComplete(QPainter& p, double t)
{
    {
        Rise rise(p, t, 0.15, 36);
        drawText(p, 110, 150, QStringLiteral("Everything you need."), font(80, QFont::Black), hex(kText));
    }
    {
        Rise rise(p, t, 0.4, 36);
        drawText(p, 112, 222, QStringLiteral("Voice, chat and servers, in a layout you already know."), font(32), hex(kMuted));
    }
    const double e = reveal(t, 0.5, 0.9);
    const QRectF window(110, 300, 1088, 612);
    {
        Layer layer(p, e, (1 - e) * 70, 0, 0.94 + 0.06 * e, window.center());
        drawClientAt(p, window, t + 20, easeOut(ramp(t, 4.5, 5.0)));
    }
    const QString features[] = {
        QStringLiteral("Voice channels"),       QStringLiteral("DM and group calls"),
        QStringLiteral("End-to-end encryption"), QStringLiteral("Noise suppression"),
        QStringLiteral("Echo cancellation"),    QStringLiteral("Markdown, emojis and images"),
        QStringLiteral("Replies and reactions"), QStringLiteral("Notifications and unreads"),
        QStringLiteral("Log in with a QR code"),
    };
    const uint32_t accents[] = {kGreen, kGreen, kGreen, kGreen, kGreen, kBlurple, kBlurple, kBlurple, kCyan};
    for (int i = 0; i < 9; ++i) {
        const double start = 1.0 + i * 0.38;
        const double a = reveal(t, start, 0.5);
        const QRectF r(1262, 300 + i * 70, 560, 58);
        Layer layer(p, a, 0, (1 - a) * 80);
        card(p, r, 14);
        const QPointF dot(r.left() + 32, r.center().y());
        p.setPen(Qt::NoPen);
        p.setBrush(hex(accents[i], 0.2));
        p.drawEllipse(dot, 15, 15);
        p.setPen(QPen(hex(accents[i]), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({QPointF(dot.x() - 6, dot.y()), QPointF(dot.x() - 1.5, dot.y() + 5), QPointF(dot.x() + 7, dot.y() - 5)}));
        drawText(p, r.left() + 62, r.center().y(), features[i], font(26, QFont::DemiBold), hex(kText));
    }
}

void pseudoQr(QPainter& p, const QRectF& r)
{
    constexpr int n = 21;
    const double cell = r.width() / n;
    p.setPen(Qt::NoPen);
    p.setBrush(hex(0x111214));
    uint32_t seed = 0xC0FFEEu;
    auto finder = [&](int fx, int fy) { return (fx < 8 && fy < 8) || (fx >= n - 8 && fy < 8) || (fx < 8 && fy >= n - 8); };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            seed = seed * 1103515245u + 12345u;
            if (!finder(x, y) && ((seed >> 16) & 1))
                p.drawRect(QRectF(r.left() + x * cell, r.top() + y * cell, cell + 0.3, cell + 0.3));
        }
    for (QPointF origin : {r.topLeft(), QPointF(r.right() - 7 * cell, r.top()), QPointF(r.left(), r.bottom() - 7 * cell)}) {
        p.setBrush(hex(0x111214));
        p.drawRect(QRectF(origin, QSizeF(7 * cell, 7 * cell)));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(origin + QPointF(cell, cell), QSizeF(5 * cell, 5 * cell)));
        p.setBrush(hex(0x111214));
        p.drawRect(QRectF(origin + QPointF(2 * cell, 2 * cell), QSizeF(3 * cell, 3 * cell)));
    }
}

void sceneSimple(QPainter& p, double t)
{
    heading(p, t, 0.15, 160, QStringLiteral("Simple from the first click."), QStringLiteral("No setup. Three steps and you are talking."));

    const QString titles[] = {QStringLiteral("Download"), QStringLiteral("Scan the QR code"), QStringLiteral("Talk")};
    const QString captions[] = {QStringLiteral("Windows, macOS or Linux"), QStringLiteral("with the Discord app on your phone"),
                                QStringLiteral("join a voice channel and you're in")};
    const double starts[] = {0.8, 2.0, 3.2};

    // Connector drawn as the steps appear.
    const double y = 330;
    const double progress = easeInOut(ramp(t, 1.0, 4.0));
    p.setPen(QPen(hex(0x3a3c42), 4, Qt::DashLine, Qt::RoundCap));
    p.drawLine(QPointF(420, y), QPointF(1500, y));
    p.setPen(QPen(hex(kBlurple), 4, Qt::SolidLine, Qt::RoundCap));
    if (progress > 0)
        p.drawLine(QPointF(420, y), QPointF(420 + 1080 * progress, y));

    for (int i = 0; i < 3; ++i) {
        const double e = reveal(t, starts[i], 0.7);
        const double local = t - starts[i];
        const QRectF r(180 + i * 540, y, 480, 560);
        Layer layer(p, e, (1 - e) * 60);
        card(p, r);
        p.setPen(Qt::NoPen);
        p.setBrush(hex(kBlurple));
        p.drawEllipse(QPointF(r.center().x(), y), 36, 36);
        drawText(p, r.center().x(), y, QString::number(i + 1), font(36, QFont::Black), Qt::white, Qt::AlignHCenter);

        const QPointF art(r.center().x(), r.top() + 205);
        if (i == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(hex(kPanel));
            p.drawEllipse(art, 100, 100);
            const double bounce = std::abs(std::sin(local * kPi * 2.0)) * 14 * (local > 0);
            p.setPen(QPen(hex(kBlurple), 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(art.x(), art.y() - 50 + bounce), QPointF(art.x(), art.y() + 14 + bounce));
            p.drawPolyline(QPolygonF({QPointF(art.x() - 28, art.y() - 12 + bounce), QPointF(art.x(), art.y() + 16 + bounce),
                                      QPointF(art.x() + 28, art.y() - 12 + bounce)}));
            p.setPen(QPen(hex(kText), 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF({QPointF(art.x() - 46, art.y() + 30), QPointF(art.x() - 46, art.y() + 50),
                                      QPointF(art.x() + 46, art.y() + 50), QPointF(art.x() + 46, art.y() + 30)}));
        } else if (i == 1) {
            const QRectF phone(art.x() - 70, art.y() - 125, 140, 250);
            p.setPen(QPen(hex(kMuted), 5));
            p.setBrush(hex(kRail));
            p.drawRoundedRect(phone, 22, 22);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            const QRectF qrArea(art.x() - 52, art.y() - 58, 104, 104);
            p.drawRoundedRect(qrArea.adjusted(-6, -6, 6, 6), 6, 6);
            pseudoQr(p, qrArea);
            const double scan = 0.5 - 0.5 * std::cos(local * 2.6);
            const double sy = qrArea.top() + qrArea.height() * scan;
            p.setPen(QPen(hex(kGreen), 3));
            p.drawLine(QPointF(qrArea.left() - 10, sy), QPointF(qrArea.right() + 10, sy));
            QLinearGradient trail(0, sy - 30, 0, sy);
            trail.setColorAt(0, hex(kGreen, 0));
            trail.setColorAt(1, hex(kGreen, 0.25));
            p.fillRect(QRectF(qrArea.left() - 10, sy - 30, qrArea.width() + 20, 30), trail);
            p.setPen(Qt::NoPen);
            p.setBrush(hex(kMuted));
            p.drawRoundedRect(QRectF(art.x() - 22, phone.bottom() - 22, 44, 6), 3, 3);
        } else {
            static constexpr uint32_t colors[] = {0xf0b232, 0xeb459e, kBlurple};
            static const QString initials[] = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Y")};
            for (int k = 0; k < 3; ++k) {
                const QPointF c(art.x() + (k - 1) * 120, art.y());
                const double speak = std::fmod(t * 0.9 + k * 0.33, 1.0) < 0.5 ? 1.0 : 0.0;
                if (speak > 0) {
                    const double pulse = pump(t + 30);
                    p.setPen(QPen(hex(kGreen), 6));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(c, 50 + 4 * pulse, 50 + 4 * pulse);
                }
                avatar(p, c, 44, colors[k], initials[k]);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(hex(kUserBar));
            p.drawRoundedRect(QRectF(art.x() - 120, art.y() + 80, 240, 56), 28, 28);
            micIcon(p, {art.x() - 60, art.y() + 108}, 1.4, hex(kText));
            headphonesIcon(p, {art.x(), art.y() + 106}, 1.4, hex(kText));
            p.setBrush(hex(kRed));
            p.drawEllipse(QPointF(art.x() + 60, art.y() + 108), 18, 18);
            p.setPen(QPen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(art.x() + 51, art.y() + 104, 18, 12), 20 * 16, 140 * 16);
        }
        drawText(p, r.center().x(), r.bottom() - 140, titles[i], font(44, QFont::Bold), hex(kText), Qt::AlignHCenter);
        drawText(p, r.center().x(), r.bottom() - 88, captions[i], font(25), hex(kDim), Qt::AlignHCenter);
    }
}

void sceneOutro(QPainter& p, double t)
{
    const QPointF center(kWidth / 2.0, 360);
    const double pop = easeOutBack(ramp(t, 0.1, 0.9));
    glow(p, center, 420, hex(kBlurple, 0.4 * ramp(t, 0, 0.8)));
    {
        Layer layer(p, ramp(t, 0.1, 0.4), 0, 0, std::max(0.01, pop), center);
        drawLogo(p, center, 200, t, 0.5 * std::exp(-std::fmod(t, 0.25) * 6.0) * (1 - ramp(t, 2.0, 4.0)) + 0.1);
    }
    {
        Rise rise(p, t, 0.5, 40);
        drawText(p, kWidth / 2.0, 570, QStringLiteral("Snapcord"), font(120, QFont::Black), hex(kText), Qt::AlignHCenter);
    }
    {
        Rise rise(p, t, 0.9, 30);
        drawText(p, kWidth / 2.0, 668, QStringLiteral("Free and open source  ·  GPLv3"), font(38, QFont::DemiBold), hex(kBody), Qt::AlignHCenter);
    }
    {
        Rise rise(p, t, 1.2, 30);
        drawText(p, kWidth / 2.0, 728, QStringLiteral("Windows  ·  macOS  ·  Linux"), font(32), hex(kMuted), Qt::AlignHCenter);
    }
    {
        Rise rise(p, t, 1.6, 30);
        pill(p, {kWidth / 2.0, 838}, QStringLiteral("github.com/pedrordgsr/snapcord"), 32, hex(kBlurple));
    }
    Layer note(p, ramp(t, 2.0, 2.6));
    drawText(p, kWidth / 2.0, 1010, QStringLiteral("Snapcord is an unofficial client and is not affiliated with Discord."), font(20), hex(0x6d6f78), Qt::AlignHCenter);
}

void renderFrame(QImage& image, double t)
{
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    background(p, t);

    struct Scene {
        double start, end;
        void (*draw)(QPainter&, double);
    };
    static constexpr Scene scenes[] = {
        {0, 6, sceneIntro}, {6, 12, sceneFast}, {12, 20, sceneLight},
        {20, 30, sceneComplete}, {30, 38, sceneSimple}, {38, kDuration, sceneOutro},
    };
    for (const Scene& s : scenes) {
        if (t < s.start || t >= s.end)
            continue;
        const double local = t - s.start;
        const bool last = s.end >= kDuration;
        const double exit = last ? 0.0 : ramp(t, s.end - 0.35, s.end);
        Layer layer(p, 1.0 - exit, 0, 0, 1.0 + 0.05 * easeInOut(exit), QPointF(kWidth / 2.0, kHeight / 2.0));
        s.draw(p, local);
    }
    // A flash on each cut, and the fade to black at the end.
    for (double cut : {6.0, 12.0, 20.0, 30.0, 38.0}) {
        const double f = 1.0 - ramp(t, cut, cut + 0.25);
        if (t >= cut && f > 0)
            p.fillRect(QRectF(0, 0, kWidth, kHeight), QColor(255, 255, 255, int(28 * f)));
    }
    const double fade = ramp(t, kDuration - 1.6, kDuration - 0.2);
    if (fade > 0)
        p.fillRect(QRectF(0, 0, kWidth, kHeight), QColor(0, 0, 0, int(255 * fade)));
}

// --- Media Foundation ------------------------------------------------------------------------------

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr()
    {
        if (p)
            p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

#define CHECK(expr)                                                                                     \
    do {                                                                                                \
        const HRESULT hr_ = (expr);                                                                     \
        if (FAILED(hr_)) {                                                                              \
            out() << #expr << " failed: 0x" << Qt::hex << uint32_t(hr_) << Qt::dec << Qt::endl;         \
            return false;                                                                               \
        }                                                                                               \
    } while (0)

bool writeSample(IMFSinkWriter* writer, DWORD stream, const void* data, DWORD size, LONGLONG time, LONGLONG duration)
{
    ComPtr<IMFMediaBuffer> buffer;
    CHECK(MFCreateMemoryBuffer(size, &buffer));
    BYTE* dst = nullptr;
    CHECK(buffer->Lock(&dst, nullptr, nullptr));
    std::memcpy(dst, data, size);
    buffer->Unlock();
    CHECK(buffer->SetCurrentLength(size));
    ComPtr<IMFSample> sample;
    CHECK(MFCreateSample(&sample));
    CHECK(sample->AddBuffer(buffer));
    CHECK(sample->SetSampleTime(time));
    CHECK(sample->SetSampleDuration(duration));
    CHECK(writer->WriteSample(stream, sample));
    return true;
}

bool encode(const QString& path)
{
    QElapsedTimer timer;
    timer.start();
    out() << "Synthesizing the soundtrack..." << Qt::endl;
    const std::vector<int16_t> pcm = renderSoundtrack();

    ComPtr<IMFAttributes> attributes;
    CHECK(MFCreateAttributes(&attributes, 1));
    CHECK(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE)); // the software encoder honours the bitrate
    ComPtr<IMFSinkWriter> writer;
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    CHECK(MFCreateSinkWriterFromURL(file.c_str(), nullptr, attributes, &writer));

    DWORD video = 0, audio = 0;
    {
        ComPtr<IMFMediaType> type;
        CHECK(MFCreateMediaType(&type));
        CHECK(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        CHECK(type->SetUINT32(MF_MT_AVG_BITRATE, 8'000'000));
        CHECK(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        CHECK(type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High));
        CHECK(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, kWidth, kHeight));
        CHECK(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, kFps, 1));
        CHECK(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        CHECK(writer->AddStream(type, &video));
    }
    {
        ComPtr<IMFMediaType> type;
        CHECK(MFCreateMediaType(&type));
        CHECK(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        CHECK(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        CHECK(type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(kWidth * 4))); // top-down rows, like QImage
        CHECK(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, kWidth, kHeight));
        CHECK(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, kFps, 1));
        CHECK(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        CHECK(writer->SetInputMediaType(video, type, nullptr));
    }
    {
        ComPtr<IMFMediaType> type;
        CHECK(MFCreateMediaType(&type));
        CHECK(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        CHECK(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
        CHECK(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        CHECK(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kSampleRate));
        CHECK(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        CHECK(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000)); // 192 kbit/s
        CHECK(writer->AddStream(type, &audio));
    }
    {
        ComPtr<IMFMediaType> type;
        CHECK(MFCreateMediaType(&type));
        CHECK(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        CHECK(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        CHECK(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        CHECK(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kSampleRate));
        CHECK(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        CHECK(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
        CHECK(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kSampleRate * 4));
        CHECK(writer->SetInputMediaType(audio, type, nullptr));
    }
    CHECK(writer->BeginWriting());

    const int frames = int(kDuration * kFps);
    const size_t samplesPerFrame = kSampleRate / kFps;
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    for (int i = 0; i < frames; ++i) {
        const LONGLONG start = LONGLONG(i) * 10'000'000 / kFps, end = LONGLONG(i + 1) * 10'000'000 / kFps;
        renderFrame(image, double(i) / kFps);
        if (!writeSample(writer, video, image.constBits(), DWORD(image.sizeInBytes()), start, end - start))
            return false;
        const size_t first = size_t(i) * samplesPerFrame * 2;
        if (!writeSample(writer, audio, pcm.data() + first, DWORD(samplesPerFrame * 4), start, end - start))
            return false;
        if (i % kFps == 0)
            out() << "  " << i / kFps << " / " << int(kDuration) << " s" << Qt::endl;
    }
    CHECK(writer->Finalize());
    out() << "Wrote " << path << " in " << timer.elapsed() / 1000 << " s" << Qt::endl;
    return true;
}

bool check(const QString& path, const QString& dir, const QStringList& times)
{
    ComPtr<IMFAttributes> attributes;
    CHECK(MFCreateAttributes(&attributes, 1));
    CHECK(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
    ComPtr<IMFSourceReader> reader;
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    CHECK(MFCreateSourceReaderFromURL(file.c_str(), attributes, &reader));
    {
        ComPtr<IMFMediaType> type;
        CHECK(MFCreateMediaType(&type));
        CHECK(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        CHECK(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type));
    }
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)))
        out() << "Duration: " << double(var.uhVal.QuadPart) / 1e7 << " s" << Qt::endl;
    PropVariantClear(&var);
    for (int stream = 0;; ++stream) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(DWORD(stream), 0, &type)))
            break;
        GUID major{};
        type->GetGUID(MF_MT_MAJOR_TYPE, &major);
        out() << "Stream " << stream << ": " << (major == MFMediaType_Audio ? "audio" : major == MFMediaType_Video ? "video" : "other") << Qt::endl;
    }

    QDir().mkpath(dir);
    for (const QString& value : times) {
        const LONGLONG target = LONGLONG(value.toDouble() * 1e7);
        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = target;
        CHECK(reader->SetCurrentPosition(GUID_NULL, position));
        for (;;) {
            DWORD index = 0, flags = 0;
            LONGLONG time = 0;
            ComPtr<IMFSample> sample;
            CHECK(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &index, &flags, &time, &sample));
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                break;
            if (!sample || time + 10'000'000 / kFps < target)
                continue;
            ComPtr<IMFMediaBuffer> buffer;
            CHECK(sample->ConvertToContiguousBuffer(&buffer));
            QImage frame;
            ComPtr<IMF2DBuffer> buffer2d;
            if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d)))) {
                BYTE* scan0 = nullptr;
                LONG pitch = 0;
                CHECK(buffer2d->Lock2D(&scan0, &pitch));
                frame = QImage(kWidth, kHeight, QImage::Format_RGB32);
                for (int y = 0; y < kHeight; ++y)
                    std::memcpy(frame.scanLine(y), scan0 + LONGLONG(y) * pitch, kWidth * 4);
                buffer2d->Unlock2D();
            } else {
                BYTE* data = nullptr;
                CHECK(buffer->Lock(&data, nullptr, nullptr));
                frame = QImage(data, kWidth, kHeight, QImage::Format_RGB32).copy();
                buffer->Unlock();
            }
            const QString name = QDir(dir).filePath(QStringLiteral("check_%1.png").arg(value));
            frame.save(name);
            out() << "Saved " << name << " (frame at " << double(time) / 1e7 << " s)" << Qt::endl;
            break;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() >= 2 && args[0] == QLatin1String("--frames")) {
        QDir().mkpath(args[1]);
        QImage image(kWidth, kHeight, QImage::Format_RGB32);
        for (const QString& value : args.mid(2)) {
            renderFrame(image, value.toDouble());
            image.save(QDir(args[1]).filePath(QStringLiteral("frame_%1.png").arg(value)));
        }
        return 0;
    }

    // Qt has already initialized COM on this thread; that is enough for Media Foundation.
    if (FAILED(MFStartup(MF_VERSION))) {
        out() << "Media Foundation is not available." << Qt::endl;
        return 1;
    }
    bool ok = false;
    if (args.size() >= 3 && args[0] == QLatin1String("--check"))
        ok = check(args[1], args[2], args.mid(3));
    else if (args.size() == 1)
        ok = encode(args[0]);
    else
        out() << "Usage: snapcord_promo <output.mp4> | --frames <dir> <seconds...> | --check <file.mp4> <dir> <seconds...>" << Qt::endl;
    MFShutdown();
    return ok ? 0 : 1;
}
