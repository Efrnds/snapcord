#include "core/ClientProperties.h"

#include <QJsonDocument>
#include <QLocale>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>

namespace {

// Values of a recent official desktop client. The build number can be overridden in the settings
// (key "discord/clientBuildNumber") when Discord starts requiring a newer one.
constexpr int DefaultClientBuildNumber = 452000;
constexpr auto ClientVersion = "1.0.9210";
constexpr auto ElectronVersion = "37.6.0";
constexpr auto ChromeVersion = "138.0.7204.251";

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

QString architecture()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == u"x86_64")
        return QStringLiteral("x64");
    return arch;
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

int clientBuildNumber()
{
    return QSettings().value(QStringLiteral("discord/clientBuildNumber"), DefaultClientBuildNumber).toInt();
}

const QString& launchId()
{
    static const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return id;
}

QJsonObject baseProperties()
{
    return {
        {QStringLiteral("os"), operatingSystem()},
        {QStringLiteral("browser"), QStringLiteral("Discord Client")},
        {QStringLiteral("release_channel"), QStringLiteral("stable")},
        {QStringLiteral("client_version"), QLatin1String(ClientVersion)},
        {QStringLiteral("os_version"), QSysInfo::kernelVersion()},
        {QStringLiteral("os_arch"), architecture()},
        {QStringLiteral("app_arch"), architecture()},
        {QStringLiteral("system_locale"), ClientProperties::systemLocale()},
        {QStringLiteral("has_client_mods"), false},
        {QStringLiteral("browser_user_agent"), QString::fromLatin1(ClientProperties::userAgent())},
        {QStringLiteral("browser_version"), QLatin1String(ElectronVersion)},
        {QStringLiteral("client_build_number"), clientBuildNumber()},
        {QStringLiteral("native_build_number"), QJsonValue::Null},
        {QStringLiteral("client_event_source"), QJsonValue::Null},
        {QStringLiteral("client_launch_id"), launchId()},
    };
}

} // namespace

namespace ClientProperties {

QJsonObject identifyProperties()
{
    return baseProperties();
}

QByteArray superPropertiesHeader()
{
    return QJsonDocument(baseProperties()).toJson(QJsonDocument::Compact).toBase64();
}

QByteArray userAgent()
{
    return QStringLiteral("Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, like Gecko) discord/%2 Chrome/%3 "
                          "Electron/%4 Safari/537.36")
        .arg(userAgentPlatform(), QLatin1String(ClientVersion), QLatin1String(ChromeVersion),
             QLatin1String(ElectronVersion))
        .toLatin1();
}

QString systemLocale()
{
    return QLocale::system().name().replace(u'_', u'-');
}

} // namespace ClientProperties
