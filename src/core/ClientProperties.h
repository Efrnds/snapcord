#pragma once

#include <QByteArray>
#include <QJsonObject>

// Client identification sent to Discord. Snapcord presents itself like the official desktop client, since unusual
// client properties are a common reason for anti-abuse systems to flag third-party clients.
namespace ClientProperties {

// Properties sent in the Gateway IDENTIFY payload.
QJsonObject identifyProperties();

// Base64-encoded properties for the X-Super-Properties HTTP header.
QByteArray superPropertiesHeader();

QByteArray userAgent();

QString systemLocale();

} // namespace ClientProperties
