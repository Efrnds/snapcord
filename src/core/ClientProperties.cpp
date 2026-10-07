#include "core/ClientProperties.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimeZone>
#include <QUuid>

#include <vector>

namespace {

// Chrome version Snapcord presents itself as. Needs an occasional bump to stay close to current Chrome.
constexpr auto ChromeVersion = "150.0.0.0";

// Used until the current build number has been fetched from discord.com.
constexpr int FallbackBuildNumber = 600539;
constexpr qint64 BuildNumberMaxAgeMs = 24 * 60 * 60 * 1000;
constexpr int BuildNumberTimeoutMs = 10000;

constexpr qint64 HeartbeatSessionIdleMs = 30 * 60 * 1000;

const auto BuildNumberKey = QStringLiteral("discord/buildNumber");
const auto BuildNumberFetchedKey = QStringLiteral("discord/buildNumberFetched");
// Manual override, for when fetching the build number stops working.
const auto BuildNumberOverrideKey = QStringLiteral("discord/clientBuildNumber");
const auto HeartbeatIdKey = QStringLiteral("discord/heartbeatSession/id");
const auto HeartbeatCreatedKey = QStringLiteral("discord/heartbeatSession/createdAt");
const auto HeartbeatLastUsedKey = QStringLiteral("discord/heartbeatSession/lastUsedAt");

struct StoredHeartbeatSession
{
    QString id;
    qint64 createdAtMs = 0;
    qint64 lastUsedAtMs = 0;
};

struct State
{
    int buildNumber = 0; // the one in use, 0 until read from the settings
    bool buildNumberFresh = false;
    bool buildNumberCached = false; // a fetched value is known, even if old
    QString launchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString launchSignature;
    QString discordLocale;
    bool focused = true;
    bool rtcConnected = false;
    bool fetchingBuildNumber = false;
    std::vector<std::function<void()>> buildNumberWaiters;
    std::optional<StoredHeartbeatSession> heartbeatSession;
};

// The official client marks detected client mods by setting bits of this UUID. Clearing those bits from a random
// UUID gives a signature that reports none.
QString generateLaunchSignature()
{
    static constexpr quint8 Mask[16] = {0xff, 0x7f, 0xef, 0xef, 0xf7, 0xef, 0xf7, 0xff,
                                        0xdf, 0x7e, 0xff, 0xbf, 0xfe, 0xff, 0xf7, 0xff};
    QByteArray bytes = QUuid::createUuid().toRfc4122();
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(static_cast<quint8>(bytes[i]) & Mask[i]);
    return QUuid::fromRfc4122(bytes).toString(QUuid::WithoutBraces);
}

State& state()
{
    static State s = [] {
        State initial;
        initial.launchSignature = generateLaunchSignature();

        QSettings settings;
        const int override = settings.value(BuildNumberOverrideKey).toInt();
        const int cached = settings.value(BuildNumberKey).toInt();
        const qint64 fetched = settings.value(BuildNumberFetchedKey).toLongLong();
        if (override > 0) {
            initial.buildNumber = override;
            initial.buildNumberFresh = true;
            initial.buildNumberCached = true;
        } else {
            initial.buildNumber = cached > 0 ? cached : FallbackBuildNumber;
            initial.buildNumberCached = cached > 0;
            initial.buildNumberFresh = cached > 0 && QDateTime::currentMSecsSinceEpoch() - fetched < BuildNumberMaxAgeMs;
        }

        const QString id = settings.value(HeartbeatIdKey).toString();
        if (!id.isEmpty()) {
            initial.heartbeatSession = StoredHeartbeatSession{id, settings.value(HeartbeatCreatedKey).toLongLong(),
                                                              settings.value(HeartbeatLastUsedKey).toLongLong()};
        }
        return initial;
    }();
    return s;
}

QString operatingSystem()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Mac OS X");
#else
    return QStringLiteral("Linux");
#endif
}

// What Chrome reports: its user agent freezes the OS version, so the web client sees the same values.
QString operatingSystemVersion()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("10");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("10.15.7");
#else
    return QString();
#endif
}

QString userAgentPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows NT 10.0; Win64; x64");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Macintosh; Intel Mac OS X 10_15_7");
#else
    return QStringLiteral("X11; Linux x86_64");
#endif
}

void finishBuildNumberFetch(int fetched)
{
    State& s = state();
    if (fetched > 0) {
        s.buildNumber = fetched;
        s.buildNumberFresh = true;
        s.buildNumberCached = true;
        QSettings settings;
        settings.setValue(BuildNumberKey, fetched);
        settings.setValue(BuildNumberFetchedKey, QDateTime::currentMSecsSinceEpoch());
        qCInfo(lcGateway) << "client build number:" << fetched;
    } else {
        qCWarning(lcGateway) << "could not fetch the client build number, using" << s.buildNumber;
    }
    s.fetchingBuildNumber = false;
    auto waiters = std::move(s.buildNumberWaiters);
    s.buildNumberWaiters.clear();
    for (auto& waiter : waiters)
        waiter();
}

QNetworkRequest pageRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, ClientProperties::userAgent());
    request.setTransferTimeout(BuildNumberTimeoutMs);
    return request;
}

// The build number is in the Sentry script that the web app page loads.
void fetchBuildNumber()
{
    static QNetworkAccessManager* network = new QNetworkAccessManager(QCoreApplication::instance());
    QNetworkReply* page = network->get(pageRequest(QUrl(QStringLiteral("https://discord.com/app"))));
    QObject::connect(page, &QNetworkReply::finished, page, [page] {
        page->deleteLater();
        const QRegularExpressionMatch script =
            QRegularExpression(QStringLiteral(R"(/assets/sentry[^"']*?\.js)")).match(QString::fromUtf8(page->readAll()));
        if (page->error() != QNetworkReply::NoError || !script.hasMatch()) {
            finishBuildNumberFetch(0);
            return;
        }
        QNetworkReply* sentry =
            network->get(pageRequest(QUrl(QStringLiteral("https://discord.com") + script.captured(0))));
        QObject::connect(sentry, &QNetworkReply::finished, sentry, [sentry] {
            sentry->deleteLater();
            const QRegularExpressionMatch number =
                QRegularExpression(QStringLiteral(R"(buildNumber",\s*"(\d+))")).match(QString::fromUtf8(sentry->readAll()));
            finishBuildNumberFetch(sentry->error() == QNetworkReply::NoError && number.hasMatch()
                                       ? number.captured(1).toInt()
                                       : 0);
        });
    });
}

bool isExpired(qint64 lastUsedAtMs, qint64 nowMs)
{
    return nowMs - lastUsedAtMs >= HeartbeatSessionIdleMs;
}

OrderedJson baseProperties(bool identify)
{
    const State& s = state();
    OrderedJson properties;
    properties.insert(u"os", operatingSystem())
        .insert(u"browser", QStringLiteral("Chrome"))
        .insert(u"device", QString())
        .insert(u"system_locale", ClientProperties::systemLocale())
        .insert(u"has_client_mods", false)
        .insert(u"browser_user_agent", QString::fromLatin1(ClientProperties::userAgent()))
        .insert(u"browser_version", QLatin1String(ChromeVersion))
        .insert(u"os_version", operatingSystemVersion())
        .insert(u"referrer", QString())
        .insert(u"referring_domain", QString())
        .insert(u"referrer_current", QString())
        .insert(u"referring_domain_current", QString())
        .insert(u"release_channel", QStringLiteral("stable"))
        .insert(u"client_build_number", s.buildNumber)
        .insert(u"client_event_source", QJsonValue::Null)
        .insert(u"client_launch_id", s.launchId)
        .insert(u"launch_signature", s.launchSignature)
        .insert(u"client_app_state", s.focused ? QStringLiteral("focused") : QStringLiteral("unfocused"));
    if (identify) {
        properties.insert(u"is_fast_connect", false).insert(u"gateway_connect_reasons", QStringLiteral("AppSkeleton"));
    }
    if (const auto session = ClientProperties::heartbeatSession())
        properties.insert(u"client_heartbeat_session_id", session->id);
    return properties;
}

} // namespace

namespace ClientProperties {

void ensureBuildNumber(std::function<void()> done)
{
    State& s = state();
    if (!s.buildNumberFresh && !s.fetchingBuildNumber) {
        s.fetchingBuildNumber = true;
        fetchBuildNumber();
    }
    // Only the very first start waits: an older build number is still realistic (the official client also keeps
    // its build while it stays open), and the refreshed one is used as soon as it arrives.
    if (s.buildNumberCached || s.buildNumberFresh)
        done();
    else
        s.buildNumberWaiters.push_back(std::move(done));
}

OrderedJson identifyProperties()
{
    return baseProperties(true);
}

QByteArray superPropertiesHeader()
{
    return baseProperties(false).toJson().toBase64();
}

QByteArray userAgent()
{
    return QStringLiteral("Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%2 Safari/537.36")
        .arg(userAgentPlatform(), QLatin1String(ChromeVersion))
        .toLatin1();
}

QString systemLocale()
{
    static const QString locale = [] {
        QString name = QLocale::system().name();
        if (name.isEmpty() || name == u"C")
            return QStringLiteral("en-US");
        return name.replace(u'_', u'-');
    }();
    return locale;
}

void setDiscordLocale(const QString& locale)
{
    if (!locale.isEmpty())
        state().discordLocale = locale;
}

QString origin()
{
    return QStringLiteral("https://discord.com");
}

void applyApiHeaders(QNetworkRequest& request, const QByteArray& verb, const QString& referer)
{
    static const QByteArray timezone = QTimeZone::systemTimeZoneId();
    const QString& locale = state().discordLocale.isEmpty() ? systemLocale() : state().discordLocale;

    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // Browsers only send Origin with same-origin requests that can change something.
    if (verb != "GET" && verb != "HEAD")
        request.setRawHeader("Origin", origin().toLatin1());
    if (!timezone.isEmpty())
        request.setRawHeader("X-Discord-Timezone", timezone);
    request.setRawHeader("X-Discord-Locale", locale.toLatin1());
    request.setRawHeader("X-Super-Properties", superPropertiesHeader());
    request.setRawHeader("X-Debug-Options", "bugReporterEnabled");
    request.setRawHeader("Referer", referer.toUtf8());
}

void setActivity(bool focused, bool rtcConnected)
{
    state().focused = focused;
    state().rtcConnected = rtcConnected;
}

HeartbeatSessionUpdate touchHeartbeatSession()
{
    State& s = state();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool expired = !s.heartbeatSession || isExpired(s.heartbeatSession->lastUsedAtMs, now);
    QSettings settings;

    // Only time spent using the app counts: in the background and outside calls the session is left to expire.
    if (!s.focused && !s.rtcConnected) {
        if (expired && s.heartbeatSession) {
            s.heartbeatSession.reset();
            settings.remove(QStringLiteral("discord/heartbeatSession"));
        }
        return HeartbeatSessionUpdate::Unchanged;
    }

    if (!expired) {
        s.heartbeatSession->lastUsedAtMs = now;
        settings.setValue(HeartbeatLastUsedKey, now);
        return HeartbeatSessionUpdate::Touched;
    }
    s.heartbeatSession = StoredHeartbeatSession{QUuid::createUuid().toString(QUuid::WithoutBraces), now, now};
    settings.setValue(HeartbeatIdKey, s.heartbeatSession->id);
    settings.setValue(HeartbeatCreatedKey, now);
    settings.setValue(HeartbeatLastUsedKey, now);
    return HeartbeatSessionUpdate::Created;
}

std::optional<HeartbeatSession> heartbeatSession()
{
    const auto& stored = state().heartbeatSession;
    if (!stored || isExpired(stored->lastUsedAtMs, QDateTime::currentMSecsSinceEpoch()))
        return std::nullopt;
    return HeartbeatSession{stored->id, stored->createdAtMs};
}

QString clientLaunchId()
{
    return state().launchId;
}

} // namespace ClientProperties
