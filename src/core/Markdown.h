#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <functional>

// Converts Discord-flavored Markdown to the HTML subset understood by QTextDocument.
namespace Markdown {

struct Context
{
    std::function<QString(const QString& userId)> userName;
    std::function<QString(const QString& channelId)> channelName;
    std::function<QString(const QString& roleId)> roleName;
    QString selfUserId;
    bool revealSpoilers = false;
};

struct Result
{
    QString html;
    QList<QUrl> images; // custom emoji images referenced by the HTML (to be loaded as resources)
    bool jumbo = false; // the message is only emojis: Discord shows them large
};

Result toHtml(const QString& text, const Context& context);

// Plain-text version (for notifications and reply previews): mentions resolved, formatting removed.
QString toPlainText(const QString& text, const Context& context);

QString customEmojiUrl(const QString& id, bool animated);

} // namespace Markdown
