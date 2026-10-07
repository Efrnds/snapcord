#include "core/OrderedJson.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

// QJsonDocument only serializes arrays and objects, so a single value is wrapped in an array and unwrapped again.
QByteArray serialize(const QJsonValue& value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

} // namespace

OrderedJson& OrderedJson::insert(QStringView key, const QJsonValue& value)
{
    appendKey(key);
    m_body += serialize(value);
    return *this;
}

OrderedJson& OrderedJson::insert(QStringView key, const OrderedJson& value)
{
    appendKey(key);
    m_body += value.toJson();
    return *this;
}

QByteArray OrderedJson::toJson() const
{
    return '{' + m_body + '}';
}

void OrderedJson::appendKey(QStringView key)
{
    if (!m_body.isEmpty())
        m_body += ',';
    m_body += serialize(key.toString());
    m_body += ':';
}
