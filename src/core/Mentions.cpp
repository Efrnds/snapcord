#include "core/Mentions.h"

#include <QRegularExpression>

#include <algorithm>

namespace Mentions {

QString encode(const QString& text, const QList<MentionToken>& tokens)
{
    QList<MentionToken> sorted = tokens;
    std::sort(sorted.begin(), sorted.end(),
              [](const MentionToken& a, const MentionToken& b) { return a.display.size() > b.display.size(); });

    QString result;
    result.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        bool replaced = false;
        for (const MentionToken& token : std::as_const(sorted)) {
            if (token.display.isEmpty() || !QStringView(text).mid(i).startsWith(token.display))
                continue;
            const qsizetype end = i + token.display.size();
            if (end < text.size() && (text.at(end).isLetterOrNumber() || text.at(end) == u'_'))
                continue;
            result += token.raw;
            i = end;
            replaced = true;
            break;
        }
        if (!replaced)
            result += text.at(i++);
    }
    return result;
}

QString decode(const QString& content, const std::function<QString(QChar kind, const QString& id)>& nameOf,
               QList<MentionToken>* tokens)
{
    static const QRegularExpression pattern(QStringLiteral("<(@!?|@&|#)(\\d+)>"));
    QString result;
    qsizetype last = 0;
    auto it = pattern.globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString prefix = match.captured(1);
        const QChar kind = prefix == u"@&" ? u'&' : prefix.at(0);
        const QString display = nameOf(kind, match.captured(2));
        result += QStringView(content).mid(last, match.capturedStart() - last);
        if (display.isEmpty()) {
            result += match.captured();
        } else {
            result += display;
            if (tokens) {
                const bool known = std::any_of(tokens->cbegin(), tokens->cend(),
                                               [&](const MentionToken& token) { return token.display == display; });
                if (!known)
                    tokens->append({display, match.captured()});
            }
        }
        last = match.capturedEnd();
    }
    result += QStringView(content).mid(last);
    return result;
}

} // namespace Mentions
