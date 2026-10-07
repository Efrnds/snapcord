#pragma once

#include <QList>
#include <QString>

#include <functional>

// A mention as the composer shows it ("@Ana", "#general") and as Discord expects it ("<@123>", "<#456>").
struct MentionToken
{
    QString display;
    QString raw;
};

namespace Mentions {

// Replaces every display form in `text` with its raw form. A display form only matches as a whole word,
// and longer ones are tried first, so "@Ana Paula" wins over "@Ana" and "@Anastasia" is left alone.
QString encode(const QString& text, const QList<MentionToken>& tokens);

// The opposite, for editing a sent message: raw mentions become readable text, and `tokens` receives
// what is needed to encode them back. `nameOf` gets the kind ('@' user, '&' role, '#' channel) and the
// ID, and returns the display form, or an empty string to keep the raw form.
QString decode(const QString& content, const std::function<QString(QChar kind, const QString& id)>& nameOf,
               QList<MentionToken>* tokens);

} // namespace Mentions
