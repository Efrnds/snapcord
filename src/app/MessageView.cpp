#include "MessageView.h"

#include "Avatar.h"
#include "ImageCache.h"
#include "core/Markdown.h"
#include "core/MessageStore.h"
#include "core/Session.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>

#include <vector>

namespace {

constexpr int ContentLeft = 72;
constexpr int RightMargin = 24;
constexpr int AvatarSize = 40;
constexpr int GroupGapMs = 7 * 60 * 1000;
constexpr int MaxPictureWidth = 400;
constexpr int MaxPictureHeight = 300;
constexpr int MaxEmbedWidth = 432;
constexpr int ReactionHeight = 26;
const QColor TextColor(0xdb, 0xde, 0xe1);
const QColor MutedColor(0x94, 0x9b, 0xa4);
const QColor NameColor(0xf2, 0xf3, 0xf5);

QString documentStyle()
{
    return QStringLiteral("code, pre { font-family: Consolas, 'Cascadia Mono', 'Courier New', monospace; font-size: 13px; }"
                          "a { color: #00a8fc; text-decoration: none; }");
}

QFont messageFont(const QFont& base, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    QFont font = base;
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

QString formatTime(const QDateTime& time)
{
    const QLocale locale;
    const QDate today = QDate::currentDate();
    const QString clock = locale.toString(time.time(), QLocale::ShortFormat);
    if (time.date() == today)
        return QCoreApplication::translate("MessageView", "Today at %1").arg(clock);
    if (time.date() == today.addDays(-1))
        return QCoreApplication::translate("MessageView", "Yesterday at %1").arg(clock);
    return locale.toString(time.date(), QLocale::ShortFormat) + u' ' + clock;
}

QSize fitInto(int width, int height, int maxWidth, int maxHeight)
{
    if (width <= 0 || height <= 0)
        return {maxWidth, maxHeight / 2};
    const double scale = std::min({1.0, double(maxWidth) / width, double(maxHeight) / height});
    return {std::max(1, int(width * scale)), std::max(1, int(height * scale))};
}

// Discord's media proxy resizes images on the server, so only the displayed size is downloaded.
QUrl previewUrl(const QString& proxyUrl, const QSize& size)
{
    QUrl url(proxyUrl);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("width"), QString::number(size.width()));
    query.addQueryItem(QStringLiteral("height"), QString::number(size.height()));
    url.setQuery(query);
    return url;
}

QString formatSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

// --- MessageModel -----------------------------------------------------------------------------------

MessageModel::MessageModel(MessageStore* store, QObject* parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    // The store has already changed when it signals, so rows are announced right away.
    connect(m_store, &MessageStore::reset, this, [this](const QString& channelId) {
        if (channelId != m_channelId)
            return;
        beginResetModel();
        endResetModel();
    });
    connect(m_store, &MessageStore::olderLoaded, this, [this](const QString& channelId, int count) {
        if (channelId != m_channelId || count <= 0)
            return;
        emit aboutToPrepend();
        beginInsertRows({}, 0, count - 1);
        endInsertRows();
        // The first old message may now start or continue a group with the previous first message.
        emit messageChanged(message(count).id);
        emit prepended();
    });
    connect(m_store, &MessageStore::inserted, this, [this](const QString& channelId, int index) {
        if (channelId != m_channelId)
            return;
        beginInsertRows({}, index, index);
        endInsertRows();
        if (index + 1 < rowCount())
            emit messageChanged(message(index + 1).id);
    });
    connect(m_store, &MessageStore::changed, this, [this](const QString& channelId, int index) {
        if (channelId != m_channelId)
            return;
        emit messageChanged(message(index).id);
        emit dataChanged(this->index(index), this->index(index));
    });
    connect(m_store, &MessageStore::removed, this, [this](const QString& channelId, int index) {
        if (channelId != m_channelId)
            return;
        beginRemoveRows({}, index, index);
        endRemoveRows();
        if (index < rowCount())
            emit messageChanged(message(index).id);
    });
}

void MessageModel::setChannel(const QString& channelId)
{
    beginResetModel();
    m_channelId = channelId;
    endResetModel();
}

int MessageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_store->messages(m_channelId).size());
}

QVariant MessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    if (role == Qt::UserRole)
        return message(index.row()).id;
    return {};
}

const Message& MessageModel::message(int row) const
{
    return m_store->messages(m_channelId).at(row);
}

int MessageModel::rowOf(const QString& messageId) const
{
    const auto& messages = m_store->messages(m_channelId);
    for (qsizetype i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].id == messageId)
            return static_cast<int>(i);
    }
    return -1;
}

bool MessageModel::startsGroup(int row) const
{
    if (row == 0 || startsDay(row))
        return true;
    const Message& current = message(row);
    const Message& previous = message(row - 1);
    if (current.isSystemMessage() || previous.isSystemMessage() || current.type == Message::Reply)
        return true;
    return current.author.id != previous.author.id
        || previous.timestamp.msecsTo(current.timestamp) > GroupGapMs;
}

bool MessageModel::startsDay(int row) const
{
    return row == 0 || message(row).timestamp.date() != message(row - 1).timestamp.date();
}

// --- MessageDelegate --------------------------------------------------------------------------------

struct MessageDelegate::Layout
{
    struct Picture
    {
        QRect rect;
        QUrl source;
        QString openUrl;
        bool file = false;
        QString name;
        QString detail;
    };
    struct EmbedBox
    {
        QRect box;
        QColor color;
        QString author;
        QRect authorRect;
        QString title;
        QString titleUrl;
        QRect titleRect;
        std::unique_ptr<QTextDocument> description;
        QPoint descriptionPos;
        bool hasPicture = false;
        Picture picture;
    };

    int width = 0;
    int height = 0;
    bool groupStart = false;
    bool dayStart = false;
    bool system = false;
    bool mentioned = false;
    int top = 0;
    QString dayText;
    QRect avatarRect;
    QRect nameRect;
    QString name;
    QString time;
    QRect replyRect;
    QString replyName;
    QString replyText;
    std::unique_ptr<QTextDocument> content;
    QPoint contentPos;
    QList<Picture> pictures;
    std::vector<EmbedBox> embeds;
    QList<QRect> reactionRects;
    QString stickers;
    QRect stickerRect;
};

MessageDelegate::MessageDelegate(Session* session, ImageCache* images, MessageModel* model, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_session(session)
    , m_images(images)
    , m_model(model)
    , m_layouts(400)
{
    connect(m_model, &MessageModel::messageChanged, this, &MessageDelegate::invalidate);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MessageDelegate::invalidateAll);
}

MessageDelegate::~MessageDelegate() = default;

void MessageDelegate::invalidate(const QString& messageId)
{
    m_layouts.remove(messageId);
}

void MessageDelegate::invalidateAll()
{
    m_layouts.clear();
    m_avatars.clear();
}

void MessageDelegate::revealSpoilers(const QString& messageId)
{
    m_revealedSpoilers.insert(messageId);
    invalidate(messageId);
}

QPixmap MessageDelegate::avatar(const User& user, int size) const
{
    const QString key = user.id + u'/' + QString::number(size);
    const auto it = m_avatars.constFind(key);
    if (it != m_avatars.cend())
        return *it;
    const QImage picture = m_images->image(ImageCache::avatarUrl(user));
    const QPixmap pixmap = makeAvatar(user.displayName(), picture, size, 2.0);
    if (!picture.isNull())
        m_avatars.insert(key, pixmap); // placeholders are not cached, so the real picture replaces them
    return pixmap;
}

QString MessageDelegate::systemText(const Message& message) const
{
    const QString name = message.author.displayName();
    switch (message.type) {
    case Message::UserJoin:
        return tr("%1 joined the server.").arg(name);
    case Message::ChannelPinnedMessage:
        return tr("%1 pinned a message to this channel.").arg(name);
    case Message::Call:
        return tr("%1 started a call.").arg(name);
    case Message::RecipientAdd:
        return tr("%1 added someone to the group.").arg(name);
    case Message::RecipientRemove:
        return tr("%1 left the group.").arg(name);
    case Message::ChannelNameChange:
        return tr("%1 changed the channel name: %2").arg(name, message.content);
    default:
        return message.content.isEmpty() ? tr("%1 did something Snapcord can't show yet.").arg(name) : message.content;
    }
}

MessageDelegate::Layout& MessageDelegate::layout(const QModelIndex& index, int width) const
{
    const Message& message = m_model->message(index.row());
    const bool groupStart = m_model->startsGroup(index.row());
    const bool dayStart = m_model->startsDay(index.row());
    if (Layout* cached = m_layouts.object(message.id)) {
        if (cached->width == width && cached->groupStart == groupStart && cached->dayStart == dayStart)
            return *cached;
    }

    auto* l = new Layout;
    l->width = width;
    l->groupStart = groupStart;
    l->dayStart = dayStart;
    l->system = message.isSystemMessage();
    const QFont base = QGuiApplication::font();
    const int contentWidth = std::max(120, width - ContentLeft - RightMargin);

    int y = 0;
    if (dayStart) {
        l->dayText = QLocale().toString(message.timestamp.date(), QLocale::LongFormat);
        y += 40;
    }
    y += groupStart ? 14 : 1;
    l->top = y;

    Markdown::Context context;
    context.selfUserId = m_session->self().id;
    context.revealSpoilers = m_revealedSpoilers.contains(message.id);
    context.userName = [this](const QString& id) { return m_session->user(id).displayName(); };
    const QString guildId = message.guildId;
    context.channelName = [this, guildId](const QString& id) {
        const Channel* channel = m_session->channel(guildId, id);
        return channel ? channel->name : QString();
    };
    context.roleName = [this, guildId](const QString& id) {
        const Guild* guild = m_session->guild(guildId);
        return guild ? guild->roles.value(id).name : QString();
    };
    l->mentioned = message.mentionedUserIds.contains(context.selfUserId) || message.mentionsEveryone;

    if (l->system) {
        l->avatarRect = QRect(ContentLeft - 40, y + 2, 16, 16);
        QFontMetrics metrics(messageFont(base, 15));
        l->name = systemText(message);
        const QRect bounds = metrics.boundingRect(QRect(ContentLeft, y, contentWidth, 10000), Qt::TextWordWrap, l->name);
        l->nameRect = QRect(ContentLeft, y, contentWidth, bounds.height());
        l->time = formatTime(message.timestamp);
        y += bounds.height() + 4;
        l->height = y;
        m_layouts.insert(message.id, l);
        return *l;
    }

    if (message.type == Message::Reply && (!message.referencedMessageId.isEmpty() || message.referencedDeleted)) {
        l->replyRect = QRect(ContentLeft, y, contentWidth, 20);
        if (message.referencedDeleted) {
            l->replyText = tr("Original message was deleted");
        } else {
            l->replyName = message.referencedAuthor.displayName();
            l->replyText = Markdown::toPlainText(message.referencedContent, context);
            if (l->replyText.isEmpty())
                l->replyText = tr("Click to see attachment");
        }
        y += 24;
    }

    if (groupStart) {
        l->avatarRect = QRect(16, y, AvatarSize, AvatarSize);
        l->name = message.author.displayName();
        l->time = formatTime(message.timestamp);
        const QFontMetrics nameMetrics(messageFont(base, 16, QFont::DemiBold));
        l->nameRect = QRect(ContentLeft, y, nameMetrics.horizontalAdvance(l->name), 22);
        y += 22;
    } else {
        l->time = QLocale().toString(message.timestamp.time(), QLocale::ShortFormat);
    }

    if (!message.content.isEmpty() || message.editedTimestamp.isValid()) {
        const Markdown::Result markdown = Markdown::toHtml(message.content, context);
        QString html = markdown.html;
        if (message.editedTimestamp.isValid())
            html += QStringLiteral(" <span style=\"font-size:10px;color:#949ba4;\">%1</span>").arg(tr("(edited)"));
        l->content = std::make_unique<QTextDocument>();
        l->content->setDefaultFont(messageFont(base, 15));
        l->content->setDefaultStyleSheet(documentStyle());
        l->content->setDocumentMargin(0);
        // Custom emoji images: real ones when loaded, transparent placeholders (same size) until then.
        for (const QUrl& url : markdown.images) {
            QImage image = m_images->image(url);
            if (image.isNull()) {
                image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
            }
            l->content->addResource(QTextDocument::ImageResource, url, image);
        }
        l->content->setHtml(html);
        l->content->setTextWidth(contentWidth);
        l->contentPos = QPoint(ContentLeft, y);
        y += int(std::ceil(l->content->size().height()));
    }

    for (const Attachment& attachment : message.attachments) {
        Layout::Picture picture;
        picture.openUrl = attachment.url;
        if (attachment.isImage()) {
            const QSize size = fitInto(attachment.width, attachment.height, std::min(MaxPictureWidth, contentWidth), MaxPictureHeight);
            picture.rect = QRect(ContentLeft, y + 4, size.width(), size.height());
            picture.source = previewUrl(attachment.proxyUrl, size);
        } else {
            picture.file = true;
            picture.name = attachment.filename;
            picture.detail = formatSize(attachment.size);
            picture.rect = QRect(ContentLeft, y + 4, std::min(400, contentWidth), 56);
        }
        l->pictures.append(picture);
        y += picture.rect.height() + 6;
    }

    for (const Embed& embed : message.embeds) {
        // Plain image/video links are shown as the picture alone, like Discord does.
        const bool mediaOnly = (embed.type == u"image" || embed.type == u"gifv") && !embed.imageUrl.isEmpty();
        Layout::EmbedBox box;
        const int boxWidth = std::min(MaxEmbedWidth, contentWidth);
        const int innerLeft = ContentLeft + (mediaOnly ? 0 : 16);
        const int innerWidth = boxWidth - (mediaOnly ? 0 : 32);
        int by = y + 4 + (mediaOnly ? 0 : 10);
        box.color = embed.color >= 0 ? QColor::fromRgb(QRgb(embed.color)) : QColor(0x1e, 0x1f, 0x22);
        if (!mediaOnly) {
            if (!embed.authorName.isEmpty() || !embed.providerName.isEmpty()) {
                box.author = embed.authorName.isEmpty() ? embed.providerName : embed.authorName;
                box.authorRect = QRect(innerLeft, by, innerWidth, 18);
                by += 22;
            }
            if (!embed.title.isEmpty()) {
                box.title = embed.title;
                box.titleUrl = embed.url;
                const QFontMetrics metrics(messageFont(base, 15, QFont::DemiBold));
                const QRect bounds = metrics.boundingRect(QRect(innerLeft, by, innerWidth, 1000), Qt::TextWordWrap, embed.title);
                box.titleRect = QRect(innerLeft, by, innerWidth, bounds.height());
                by += bounds.height() + 4;
            }
            if (!embed.description.isEmpty()) {
                box.description = std::make_unique<QTextDocument>();
                box.description->setDefaultFont(messageFont(base, 14));
                box.description->setDefaultStyleSheet(documentStyle());
                box.description->setDocumentMargin(0);
                box.description->setHtml(Markdown::toHtml(embed.description, context).html);
                box.description->setTextWidth(innerWidth);
                box.descriptionPos = QPoint(innerLeft, by);
                by += int(std::ceil(box.description->size().height())) + 6;
            }
        }
        if (!embed.imageUrl.isEmpty()) {
            box.hasPicture = true;
            const QSize size = embed.imageIsThumbnail && !mediaOnly
                ? fitInto(embed.imageWidth, embed.imageHeight, 80, 80)
                : fitInto(embed.imageWidth, embed.imageHeight, std::min(MaxPictureWidth, innerWidth), MaxPictureHeight);
            box.picture.rect = QRect(innerLeft, by, size.width(), size.height());
            box.picture.source = previewUrl(embed.imageUrl, size);
            box.picture.openUrl = embed.url.isEmpty() ? embed.imageUrl : embed.url;
            by += size.height() + 6;
        }
        const int boxHeight = by - y - 4 + (mediaOnly ? 0 : 6);
        box.box = QRect(ContentLeft, y + 4, mediaOnly ? box.picture.rect.width() : boxWidth, boxHeight);
        y += boxHeight + 8;
        l->embeds.push_back(std::move(box));
    }

    if (!message.stickerNames.isEmpty()) {
        l->stickers = tr("Sticker: %1").arg(message.stickerNames.join(QStringLiteral(", ")));
        l->stickerRect = QRect(ContentLeft, y + 2, contentWidth, 20);
        y += 24;
    }

    if (!message.reactions.isEmpty()) {
        const QFontMetrics metrics(messageFont(base, 13, QFont::DemiBold));
        int x = ContentLeft;
        int rowTop = y + 4;
        for (const Reaction& reaction : message.reactions) {
            const int chipWidth = 8 + 18 + 6 + metrics.horizontalAdvance(QString::number(reaction.count)) + 8;
            if (x + chipWidth > ContentLeft + contentWidth && x > ContentLeft) {
                x = ContentLeft;
                rowTop += ReactionHeight + 4;
            }
            l->reactionRects.append(QRect(x, rowTop, chipWidth, ReactionHeight));
            x += chipWidth + 4;
        }
        y = rowTop + ReactionHeight + 2;
    }

    if (groupStart)
        y = std::max(y, l->avatarRect.bottom() + 1);
    l->height = y + 3;
    m_layouts.insert(message.id, l);
    return *l;
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const
{
    return {m_viewWidth, layout(index, m_viewWidth).height};
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const Message& message = m_model->message(index.row());
    const Layout& l = layout(index, m_viewWidth);
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    painter->translate(option.rect.topLeft());
    const QFont base = option.font;

    if (l.dayStart) {
        const int lineY = 20;
        painter->setFont(messageFont(base, 12, QFont::DemiBold));
        const int textWidth = painter->fontMetrics().horizontalAdvance(l.dayText) + 16;
        const int center = l.width / 2;
        painter->setPen(QColor(0x3f, 0x41, 0x47));
        painter->drawLine(16, lineY, center - textWidth / 2, lineY);
        painter->drawLine(center + textWidth / 2, lineY, l.width - 16, lineY);
        painter->setPen(MutedColor);
        painter->drawText(QRect(center - textWidth / 2, lineY - 10, textWidth, 20), Qt::AlignCenter, l.dayText);
    }

    // Row background: mention highlight, then hover.
    const QRect row(0, l.top - (l.groupStart ? 2 : 0), l.width, l.height - l.top + (l.groupStart ? 2 : 0));
    if (l.mentioned) {
        painter->fillRect(row, QColor(0xf0, 0xb2, 0x32, 26));
        painter->fillRect(QRect(row.left(), row.top(), 2, row.height()), QColor(0xf0, 0xb2, 0x32));
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(row, QColor(0x2e, 0x30, 0x35));
    }

    const QColor textColor = message.failed ? QColor(0xf2, 0x3f, 0x43) : message.pending ? QColor(0x80, 0x84, 0x8e) : TextColor;

    if (l.system) {
        QIcon(QStringLiteral(":/icons/arrow-right.svg")).paint(painter, l.avatarRect);
        painter->setFont(messageFont(base, 15));
        painter->setPen(MutedColor);
        painter->drawText(l.nameRect, Qt::TextWordWrap, l.name);
        painter->restore();
        return;
    }

    if (!l.replyRect.isNull()) {
        // Connector from the avatar column to the replied message.
        painter->setPen(QPen(QColor(0x4e, 0x50, 0x58), 2));
        QPainterPath spine;
        spine.moveTo(36, l.replyRect.top() + 18);
        spine.lineTo(36, l.replyRect.center().y() + 2);
        spine.quadTo(36, l.replyRect.center().y() - 2, 42, l.replyRect.center().y() - 2);
        spine.lineTo(ContentLeft - 6, l.replyRect.center().y() - 2);
        painter->drawPath(spine);
        int x = l.replyRect.left();
        painter->setFont(messageFont(base, 13, QFont::DemiBold));
        if (!l.replyName.isEmpty()) {
            painter->setPen(NameColor);
            const QString name = u'@' + l.replyName;
            painter->drawText(QRect(x, l.replyRect.top(), 300, 20), Qt::AlignVCenter, name);
            x += painter->fontMetrics().horizontalAdvance(name) + 6;
        }
        painter->setFont(messageFont(base, 13));
        painter->setPen(MutedColor);
        painter->drawText(QRect(x, l.replyRect.top(), l.replyRect.right() - x, 20), Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(l.replyText, Qt::ElideRight, l.replyRect.right() - x));
    }

    if (l.groupStart) {
        painter->drawPixmap(l.avatarRect, avatar(message.author, AvatarSize));
        painter->setFont(messageFont(base, 16, QFont::DemiBold));
        painter->setPen(NameColor);
        painter->drawText(l.nameRect, Qt::AlignVCenter, l.name);
        painter->setFont(messageFont(base, 12));
        painter->setPen(MutedColor);
        painter->drawText(QRect(l.nameRect.right() + 8, l.nameRect.top() + 1, 300, l.nameRect.height()), Qt::AlignVCenter, l.time);
    } else if (option.state & QStyle::State_MouseOver) {
        // Grouped messages show their time in the avatar column on hover.
        painter->setFont(messageFont(base, 11));
        painter->setPen(MutedColor);
        painter->drawText(QRect(0, l.top + 2, ContentLeft - 6, 18), Qt::AlignRight | Qt::AlignVCenter, l.time);
    }

    if (l.content) {
        painter->save();
        painter->translate(l.contentPos);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, textColor);
        l.content->documentLayout()->draw(painter, context);
        painter->restore();
    }

    auto drawPicture = [&](const Layout::Picture& picture) {
        if (picture.file) {
            painter->setPen(QColor(0x1e, 0x1f, 0x22));
            painter->setBrush(QColor(0x2b, 0x2d, 0x31));
            painter->drawRoundedRect(picture.rect, 6, 6);
            QIcon(QStringLiteral(":/icons/file.svg")).paint(painter, QRect(picture.rect.left() + 12, picture.rect.center().y() - 16, 24, 32));
            painter->setFont(messageFont(base, 15));
            painter->setPen(QColor(0x00, 0xa8, 0xfc));
            const QRect nameRect(picture.rect.left() + 48, picture.rect.top() + 8, picture.rect.width() - 60, 22);
            painter->drawText(nameRect, Qt::AlignVCenter, painter->fontMetrics().elidedText(picture.name, Qt::ElideMiddle, nameRect.width()));
            painter->setFont(messageFont(base, 12));
            painter->setPen(MutedColor);
            painter->drawText(QRect(nameRect.left(), nameRect.bottom(), nameRect.width(), 18), Qt::AlignVCenter, picture.detail);
            return;
        }
        const QImage image = m_images->image(picture.source, picture.rect.size() * 2);
        QPainterPath clip;
        clip.addRoundedRect(picture.rect, 6, 6);
        painter->save();
        painter->setClipPath(clip);
        if (image.isNull())
            painter->fillRect(picture.rect, QColor(0x2b, 0x2d, 0x31));
        else
            painter->drawImage(picture.rect, image);
        painter->restore();
    };
    for (const Layout::Picture& picture : l.pictures)
        drawPicture(picture);

    for (const Layout::EmbedBox& box : l.embeds) {
        const bool mediaOnly = box.title.isEmpty() && box.author.isEmpty() && !box.description && box.hasPicture
            && box.box.width() == box.picture.rect.width();
        if (!mediaOnly) {
            QPainterPath shape;
            shape.addRoundedRect(box.box, 4, 4);
            painter->fillPath(shape, QColor(0x2b, 0x2d, 0x31));
            painter->fillRect(QRect(box.box.left(), box.box.top(), 4, box.box.height()), box.color);
        }
        if (!box.author.isEmpty()) {
            painter->setFont(messageFont(base, 13, QFont::DemiBold));
            painter->setPen(NameColor);
            painter->drawText(box.authorRect, Qt::AlignVCenter, painter->fontMetrics().elidedText(box.author, Qt::ElideRight, box.authorRect.width()));
        }
        if (!box.title.isEmpty()) {
            painter->setFont(messageFont(base, 15, QFont::DemiBold));
            painter->setPen(box.titleUrl.isEmpty() ? NameColor : QColor(0x00, 0xa8, 0xfc));
            painter->drawText(box.titleRect, Qt::TextWordWrap, box.title);
        }
        if (box.description) {
            painter->save();
            painter->translate(box.descriptionPos);
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette.setColor(QPalette::Text, TextColor);
            box.description->documentLayout()->draw(painter, context);
            painter->restore();
        }
        if (box.hasPicture)
            drawPicture(box.picture);
    }

    if (!l.stickers.isEmpty()) {
        painter->setFont(messageFont(base, 13));
        painter->setPen(MutedColor);
        painter->drawText(l.stickerRect, Qt::AlignVCenter, l.stickers);
    }

    for (qsizetype i = 0; i < l.reactionRects.size() && i < message.reactions.size(); ++i) {
        const Reaction& reaction = message.reactions[i];
        const QRect chip = l.reactionRects[i];
        painter->setPen(reaction.me ? QPen(QColor(0x58, 0x65, 0xf2)) : Qt::NoPen);
        painter->setBrush(reaction.me ? QColor(0x37, 0x3a, 0x54) : QColor(0x2b, 0x2d, 0x31));
        painter->drawRoundedRect(QRectF(chip).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        const QRect emojiRect(chip.left() + 8, chip.center().y() - 9, 18, 18);
        if (reaction.emoji.isCustom()) {
            const QImage image = m_images->image(QUrl(Markdown::customEmojiUrl(reaction.emoji.id, reaction.emoji.animated)));
            if (!image.isNull())
                painter->drawImage(emojiRect, image);
        } else {
            painter->setFont(messageFont(base, 14));
            painter->setPen(TextColor);
            painter->drawText(emojiRect.adjusted(-2, -2, 2, 2), Qt::AlignCenter, reaction.emoji.name);
        }
        painter->setFont(messageFont(base, 13, QFont::DemiBold));
        painter->setPen(reaction.me ? QColor(0xc9, 0xcd, 0xfb) : QColor(0xb5, 0xba, 0xc1));
        painter->drawText(QRect(emojiRect.right() + 6, chip.top(), chip.right() - emojiRect.right() - 6, chip.height()),
                          Qt::AlignVCenter, QString::number(reaction.count));
    }
    painter->restore();
}

MessageDelegate::Hit MessageDelegate::hitTest(const QModelIndex& index, const QRect& itemRect, const QPoint& position) const
{
    Hit hit;
    if (!index.isValid())
        return hit;
    const Message& message = m_model->message(index.row());
    const Layout& l = layout(index, m_viewWidth);
    const QPoint p = position - itemRect.topLeft();
    hit.messageId = message.id;

    for (qsizetype i = 0; i < l.reactionRects.size(); ++i) {
        if (l.reactionRects[i].contains(p)) {
            hit.kind = Hit::Reaction;
            hit.reactionIndex = static_cast<int>(i);
            return hit;
        }
    }
    for (const Layout::Picture& picture : l.pictures) {
        if (picture.rect.contains(p)) {
            hit.kind = picture.file ? Hit::File : Hit::Image;
            hit.url = picture.openUrl;
            return hit;
        }
    }
    for (const Layout::EmbedBox& box : l.embeds) {
        if (box.hasPicture && box.picture.rect.contains(p)) {
            hit.kind = Hit::Image;
            hit.url = box.picture.openUrl;
            return hit;
        }
        if (!box.titleUrl.isEmpty() && box.titleRect.contains(p)) {
            hit.kind = Hit::Link;
            hit.url = box.titleUrl;
            return hit;
        }
        if (box.description) {
            const QString anchor = box.description->documentLayout()->anchorAt(p - box.descriptionPos);
            if (!anchor.isEmpty()) {
                hit.kind = Hit::Link;
                hit.url = anchor;
                return hit;
            }
        }
    }
    if (!l.replyRect.isNull() && l.replyRect.contains(p) && !message.referencedDeleted) {
        hit.kind = Hit::Reply;
        hit.url = message.referencedMessageId;
        return hit;
    }
    if (l.content) {
        const QString anchor = l.content->documentLayout()->anchorAt(p - l.contentPos);
        if (anchor == u"spoiler:") {
            hit.kind = Hit::Spoiler;
        } else if (!anchor.isEmpty()) {
            hit.kind = Hit::Link;
            hit.url = anchor;
        }
    }
    return hit;
}

// --- MessageListView --------------------------------------------------------------------------------

MessageListView::MessageListView(MessageDelegate* delegate, QWidget* parent)
    : QListView(parent)
    , m_delegate(delegate)
{
    setObjectName(QStringLiteral("messageList"));
    setItemDelegate(delegate);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalScrollBar()->setSingleStep(24);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setUniformItemSizes(false);
    setResizeMode(QListView::Adjust);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    setFrameShape(QFrame::NoFrame);
}

bool MessageListView::isAtBottom() const
{
    return verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 8;
}

MessageDelegate::Hit MessageListView::hitAt(const QPoint& position) const
{
    const QModelIndex index = indexAt(position);
    if (!index.isValid())
        return {};
    return m_delegate->hitTest(index, visualRect(index), position);
}

void MessageListView::mouseMoveEvent(QMouseEvent* event)
{
    const auto hit = hitAt(event->position().toPoint());
    viewport()->setCursor(hit.kind == MessageDelegate::Hit::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
    QListView::mouseMoveEvent(event);
}

void MessageListView::mouseReleaseEvent(QMouseEvent* event)
{
    QListView::mouseReleaseEvent(event);
    if (event->button() != Qt::LeftButton)
        return;
    const auto hit = hitAt(event->position().toPoint());
    switch (hit.kind) {
    case MessageDelegate::Hit::Link:
    case MessageDelegate::Hit::Image:
    case MessageDelegate::Hit::File:
        emit linkActivated(hit.url);
        break;
    case MessageDelegate::Hit::Reaction:
        emit reactionClicked(hit.messageId, hit.reactionIndex);
        break;
    case MessageDelegate::Hit::Reply:
        emit replyClicked(hit.url);
        break;
    case MessageDelegate::Hit::Spoiler:
        m_delegate->revealSpoilers(hit.messageId);
        doItemsLayout();
        break;
    case MessageDelegate::Hit::None:
        break;
    }
}

void MessageListView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid())
        emit messageContextMenuRequested(index.data(Qt::UserRole).toString(), event->globalPos());
}

void MessageListView::resizeEvent(QResizeEvent* event)
{
    const bool atBottom = isAtBottom();
    m_delegate->setViewWidth(viewport()->width());
    QListView::resizeEvent(event);
    if (atBottom)
        QTimer::singleShot(0, this, [this] { scrollToBottom(); });
}

void MessageListView::scrollContentsBy(int dx, int dy)
{
    QListView::scrollContentsBy(dx, dy);
    if (verticalScrollBar()->value() <= verticalScrollBar()->minimum() + 200)
        emit topReached();
}

void MessageListView::rowsInserted(const QModelIndex& parent, int start, int end)
{
    // New messages at the end keep the view pinned to the bottom if it was there.
    const bool appended = end == model()->rowCount() - 1;
    const bool atBottom = isAtBottom();
    QListView::rowsInserted(parent, start, end);
    if (appended && atBottom)
        QTimer::singleShot(0, this, [this] { scrollToBottom(); });
}
