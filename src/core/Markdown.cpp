#include "core/Markdown.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace Markdown {

namespace {

// Generated HTML fragments are swapped for markers while the remaining text is processed, so later
// rules (bold, italic...) never touch the inside of a link, a code span or a mention.
constexpr QChar MarkerStart = QChar(0xE000);
constexpr QChar MarkerEnd = QChar(0xE001);

class Fragments
{
public:
    QString add(const QString& html)
    {
        m_items.append(html);
        return QString(MarkerStart) + QString::number(m_items.size() - 1) + MarkerEnd;
    }

    QString restore(QString text) const
    {
        static const QRegularExpression marker(QStringLiteral("(\\d+)"));
        // Fragments can contain other markers (e.g. a mention inside a quote), so repeat until none are left.
        for (int pass = 0; pass < 4 && text.contains(MarkerStart); ++pass) {
            QString result;
            qsizetype last = 0;
            auto it = marker.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                result += text.mid(last, match.capturedStart() - last);
                result += m_items.value(match.captured(1).toInt());
                last = match.capturedEnd();
            }
            result += text.mid(last);
            text = result;
        }
        return text;
    }

private:
    QStringList m_items;
};

template<typename Replace>
QString replaceAll(const QString& text, const QRegularExpression& pattern, Replace replace)
{
    QString result;
    qsizetype last = 0;
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        result += replace(match);
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

QString formatTimestamp(qint64 seconds, QChar style)
{
    const QDateTime time = QDateTime::fromSecsSinceEpoch(seconds);
    const QLocale locale;
    switch (style.unicode()) {
    case 't':
        return locale.toString(time.time(), QLocale::ShortFormat);
    case 'T':
        return locale.toString(time.time(), QLocale::LongFormat);
    case 'd':
        return locale.toString(time.date(), QLocale::ShortFormat);
    case 'D':
        return locale.toString(time.date(), QLocale::LongFormat);
    case 'F':
        return locale.toString(time, QLocale::LongFormat);
    case 'R': {
        const qint64 delta = QDateTime::currentDateTime().secsTo(time);
        const qint64 magnitude = std::abs(delta);
        // The counts are computed beforehand: Qt's lupdate hangs on arithmetic inside translate() arguments.
        const int secondCount = int(magnitude);
        const int minutes = secondCount / 60;
        const int hours = minutes / 60;
        const int days = hours / 24;
        const int months = days / 30;
        const int years = days / 365;
        QString amount;
        if (minutes == 0)
            amount = QCoreApplication::translate("Markdown", "%n second(s)", nullptr, secondCount);
        else if (hours == 0)
            amount = QCoreApplication::translate("Markdown", "%n minute(s)", nullptr, minutes);
        else if (days == 0)
            amount = QCoreApplication::translate("Markdown", "%n hour(s)", nullptr, hours);
        else if (months == 0)
            amount = QCoreApplication::translate("Markdown", "%n day(s)", nullptr, days);
        else if (years == 0)
            amount = QCoreApplication::translate("Markdown", "%n month(s)", nullptr, months);
        else
            amount = QCoreApplication::translate("Markdown", "%n year(s)", nullptr, years);
        return delta >= 0 ? QCoreApplication::translate("Markdown", "in %1").arg(amount)
                          : QCoreApplication::translate("Markdown", "%1 ago").arg(amount);
    }
    default: // 'f' and no style
        return locale.toString(time, QLocale::ShortFormat);
    }
}

QString mentionHtml(const QString& text, bool self, const QString& userId = {})
{
    // Mentions of the current user are highlighted more strongly, like in Discord.
    const char* background = self ? "#5865f2" : "#3c4270";
    const char* color = self ? "#ffffff" : "#c9cdfb";
    const QString span = QStringLiteral("<span style=\"background-color:%1;color:%2;\">%3</span>")
                             .arg(QLatin1String(background), QLatin1String(color), text.toHtmlEscaped());
    // User mentions are links, so clicking one opens the profile.
    if (userId.isEmpty())
        return span;
    return QStringLiteral("<a href=\"user:%1\" style=\"text-decoration:none;\">%2</a>").arg(userId, span);
}

QString linkHtml(const QString& url, const QString& label)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:#00a8fc;text-decoration:none;\">%2</a>")
        .arg(url.toHtmlEscaped(), label);
}

// Escapes HTML and resolves everything that becomes an atomic fragment: escapes, code spans, emojis,
// mentions, timestamps and links. Returns text with markers in place of those fragments.
QString protectInline(const QString& escaped, const Context& context, Fragments& fragments, Result& result,
                      int emojiSize)
{
    static const QRegularExpression backslashEscape(QStringLiteral("\\\\([*_~`|\\\\>#\\-\\[\\]()])"));
    static const QRegularExpression inlineCode(QStringLiteral("(`+)(.+?)\\1"));
    static const QRegularExpression customEmoji(QStringLiteral("&lt;(a?):(\\w+):(\\d+)&gt;"));
    static const QRegularExpression userMention(QStringLiteral("&lt;@!?(\\d+)&gt;"));
    static const QRegularExpression roleMention(QStringLiteral("&lt;@&amp;(\\d+)&gt;"));
    static const QRegularExpression channelMention(QStringLiteral("&lt;#(\\d+)&gt;"));
    static const QRegularExpression timestamp(QStringLiteral("&lt;t:(-?\\d+)(?::([tTdDfFR]))?&gt;"));
    static const QRegularExpression everyone(QStringLiteral("@(everyone|here)\\b"));
    // [label](url), where the URL may be wrapped in <> to suppress its embed. The text is already escaped,
    // so "<" and ">" appear as entities here.
    static const QRegularExpression maskedLink(QStringLiteral("\\[([^\\]\\n]+)\\]\\((?:&lt;)?(https?://[^\\s)]+?)(?:&gt;)?\\)"));
    // A plain string literal on purpose: a raw string containing quotes makes Qt's lupdate loop forever.
    static const QRegularExpression autoLink(
        QStringLiteral("&lt;(https?://[^\\s]+?)&gt;|(https?://[^\\s<]+[^\\s<.,:;\\\"')\\]])"));

    QString text = escaped;
    text = replaceAll(text, inlineCode, [&](const QRegularExpressionMatch& m) {
        return fragments.add(QStringLiteral("<code style=\"background-color:#1e1f22;\">%1</code>").arg(m.captured(2)));
    });
    text = replaceAll(text, backslashEscape, [&](const QRegularExpressionMatch& m) { return fragments.add(m.captured(1)); });
    text = replaceAll(text, customEmoji, [&](const QRegularExpressionMatch& m) {
        const QString url = customEmojiUrl(m.captured(3), !m.captured(1).isEmpty());
        result.images.append(QUrl(url));
        return fragments.add(QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%2\" title=\":%3:\">")
                                 .arg(url).arg(emojiSize).arg(m.captured(2)));
    });
    text = replaceAll(text, userMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.userName ? context.userName(m.captured(1)) : QString();
        return fragments.add(mentionHtml(u'@' + (name.isEmpty() ? m.captured(1) : name), m.captured(1) == context.selfUserId,
                                         m.captured(1)));
    });
    text = replaceAll(text, roleMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.roleName ? context.roleName(m.captured(1)) : QString();
        return fragments.add(mentionHtml(u'@' + (name.isEmpty() ? QStringLiteral("role") : name), false));
    });
    text = replaceAll(text, channelMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.channelName ? context.channelName(m.captured(1)) : QString();
        return fragments.add(mentionHtml(u'#' + (name.isEmpty() ? QStringLiteral("channel") : name), false));
    });
    text = replaceAll(text, timestamp, [&](const QRegularExpressionMatch& m) {
        const QChar style = m.captured(2).isEmpty() ? QChar(u'f') : m.captured(2).at(0);
        return fragments.add(QStringLiteral("<span style=\"background-color:#2b2d31;\">%1</span>")
                                 .arg(formatTimestamp(m.captured(1).toLongLong(), style).toHtmlEscaped()));
    });
    text = replaceAll(text, everyone, [&](const QRegularExpressionMatch& m) {
        return fragments.add(mentionHtml(u'@' + m.captured(1), false));
    });
    text = replaceAll(text, maskedLink, [&](const QRegularExpressionMatch& m) {
        const QString url = QString(m.captured(2)).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return fragments.add(linkHtml(url, m.captured(1)));
    });
    text = replaceAll(text, autoLink, [&](const QRegularExpressionMatch& m) {
        const QString shown = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        const QString url = QString(shown).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return fragments.add(linkHtml(url, shown));
    });
    return text;
}

QString applyFormatting(QString text, const Context& context)
{
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression underline(QStringLiteral("__(.+?)__"));
    static const QRegularExpression italicStar(QStringLiteral("\\*(?!\\s)(.+?)\\*"));
    static const QRegularExpression italicUnderscore(QStringLiteral("(?<![\\w])_(?!\\s)(.+?)_(?![\\w])"));
    static const QRegularExpression strike(QStringLiteral("~~(.+?)~~"));
    static const QRegularExpression spoiler(QStringLiteral("\\|\\|(.+?)\\|\\|"));

    text.replace(bold, QStringLiteral("<b>\\1</b>"));
    text.replace(underline, QStringLiteral("<u>\\1</u>"));
    text.replace(italicStar, QStringLiteral("<i>\\1</i>"));
    text.replace(italicUnderscore, QStringLiteral("<i>\\1</i>"));
    text.replace(strike, QStringLiteral("<s>\\1</s>"));
    // Hidden spoilers are a dark block; clicking them ("spoiler:" link) reveals the message's spoilers.
    text.replace(spoiler, context.revealSpoilers
                              ? QStringLiteral("<span style=\"background-color:#3c3e44;\">\\1</span>")
                              : QStringLiteral("<a href=\"spoiler:\" style=\"background-color:#1e1f22;color:#1e1f22;"
                                               "text-decoration:none;\">\\1</a>"));
    return text;
}

bool isEmojiOnly(const QString& text)
{
    static const QRegularExpression customEmoji(QStringLiteral("<a?:\\w+:\\d+>"));
    QString rest = text;
    const int customCount = static_cast<int>(rest.count(customEmoji));
    rest.remove(customEmoji);
    rest.remove(u' ');
    if (rest.isEmpty())
        return customCount > 0 && customCount <= 27;
    // What is left must be emoji: symbols outside the basic Latin and punctuation ranges.
    int symbols = 0;
    for (const QChar ch : rest) {
        const bool latinOrPunctuation = ch.unicode() < 0x2000 && ch.unicode() != 0x00A9 && ch.unicode() != 0x00AE;
        if (ch.isLetterOrNumber() || latinOrPunctuation)
            return false;
        if (!ch.isLowSurrogate() && ch.unicode() != 0x200D && ch.unicode() != 0xFE0F)
            ++symbols;
    }
    return customCount + symbols <= 27;
}

} // namespace

QString customEmojiUrl(const QString& id, bool animated)
{
    return QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=48").arg(id, animated ? QStringLiteral("gif") : QStringLiteral("png"));
}

Result toHtml(const QString& text, const Context& context)
{
    Result result;
    result.jumbo = isEmojiOnly(text.trimmed());
    const int emojiSize = result.jumbo ? 48 : 22;
    Fragments fragments;

    // Code blocks first: their content is shown verbatim.
    static const QRegularExpression codeBlock(QStringLiteral("```(?:([\\w+\\-.#]+)\\n)?\\n?([\\s\\S]*?)```"));
    QString escaped = text.toHtmlEscaped();
    escaped = replaceAll(escaped, codeBlock, [&](const QRegularExpressionMatch& m) {
        QString code = m.captured(2);
        if (code.endsWith(u'\n'))
            code.chop(1);
        return fragments.add(QStringLiteral("<table width=\"100%\" cellpadding=\"8\" bgcolor=\"#2b2d31\" "
                                            "style=\"margin-top:4px;margin-bottom:4px;\"><tr><td><pre style=\"margin:0;\">%1</pre></td></tr></table>")
                                 .arg(code));
    });

    static const QRegularExpression header(QStringLiteral("^(#{1,3}) (.+)$"));
    static const QRegularExpression subtext(QStringLiteral("^-# (.+)$"));
    static const QRegularExpression listItem(QStringLiteral("^(\\s*)[-*] (.+)$"));
    static const QRegularExpression quote(QStringLiteral("^&gt; ?(.*)$"));
    static const QRegularExpression blockQuote(QStringLiteral("^&gt;&gt;&gt; ?(.*)$"));

    const QStringList lines = escaped.split(u'\n');
    QStringList output;
    QStringList quoted;
    bool quoteRest = false;
    auto flushQuote = [&] {
        if (quoted.isEmpty())
            return;
        output.append(QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\"><tr><td width=\"4\" bgcolor=\"#4e5058\"></td>"
                                     "<td style=\"padding-left:10px;\">%1</td></tr></table>")
                          .arg(quoted.join(QStringLiteral("<br>"))));
        quoted.clear();
    };

    for (const QString& rawLine : lines) {
        QString line = protectInline(rawLine, context, fragments, result, emojiSize);
        QRegularExpressionMatch match;
        if (!quoteRest && (match = blockQuote.match(line)).hasMatch()) {
            quoteRest = true; // ">>> " quotes everything until the end of the message
            quoted.append(applyFormatting(match.captured(1), context));
            continue;
        }
        if (quoteRest) {
            quoted.append(applyFormatting(line, context));
            continue;
        }
        if ((match = quote.match(line)).hasMatch()) {
            quoted.append(applyFormatting(match.captured(1), context));
            continue;
        }
        flushQuote();
        if ((match = header.match(line)).hasMatch()) {
            const int level = static_cast<int>(match.captured(1).size());
            const int size = level == 1 ? 22 : level == 2 ? 19 : 16;
            output.append(QStringLiteral("<span style=\"font-size:%1px;font-weight:700;\">%2</span>")
                              .arg(size)
                              .arg(applyFormatting(match.captured(2), context)));
        } else if ((match = subtext.match(line)).hasMatch()) {
            output.append(QStringLiteral("<span style=\"font-size:12px;color:#949ba4;\">%1</span>")
                              .arg(applyFormatting(match.captured(1), context)));
        } else if ((match = listItem.match(line)).hasMatch()) {
            const QString indent = QString(match.captured(1).size() / 2 + 1, QChar(0x2003));
            output.append(indent + QStringLiteral("•&nbsp;") + applyFormatting(match.captured(2), context));
        } else {
            output.append(applyFormatting(line, context));
        }
    }
    flushQuote();

    result.html = fragments.restore(output.join(QStringLiteral("<br>")));
    if (result.jumbo)
        result.html = QStringLiteral("<span style=\"font-size:44px;\">%1</span>").arg(result.html);
    return result;
}

QString toPlainText(const QString& text, const Context& context)
{
    static const QRegularExpression userMention(QStringLiteral("<@!?(\\d+)>"));
    static const QRegularExpression roleMention(QStringLiteral("<@&(\\d+)>"));
    static const QRegularExpression channelMention(QStringLiteral("<#(\\d+)>"));
    static const QRegularExpression customEmoji(QStringLiteral("<a?:(\\w+):\\d+>"));
    static const QRegularExpression formatting(QStringLiteral("\\*\\*|__|~~|\\|\\||`"));

    QString plain = text;
    plain = replaceAll(plain, userMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.userName ? context.userName(m.captured(1)) : QString();
        return u'@' + (name.isEmpty() ? m.captured(1) : name);
    });
    plain = replaceAll(plain, roleMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.roleName ? context.roleName(m.captured(1)) : QString();
        return u'@' + (name.isEmpty() ? QStringLiteral("role") : name);
    });
    plain = replaceAll(plain, channelMention, [&](const QRegularExpressionMatch& m) {
        const QString name = context.channelName ? context.channelName(m.captured(1)) : QString();
        return u'#' + (name.isEmpty() ? QStringLiteral("channel") : name);
    });
    plain.replace(customEmoji, QStringLiteral(":\\1:"));
    plain.remove(formatting);
    return plain.simplified();
}

} // namespace Markdown
