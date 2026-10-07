#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QStringView>

// A JSON object that keeps keys in insertion order. QJsonObject always sorts its keys, while the official
// client sends them in a fixed order; payloads that identify the client are built with this instead.
class OrderedJson
{
public:
    OrderedJson& insert(QStringView key, const QJsonValue& value);
    OrderedJson& insert(QStringView key, const OrderedJson& value);

    // Compact serialization, e.g. {"b":1,"a":2}.
    QByteArray toJson() const;

private:
    void appendKey(QStringView key);

    QByteArray m_body;
};
