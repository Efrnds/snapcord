#include "EmojiPicker.h"

#include "ImageCache.h"
#include "core/Markdown.h"
#include "core/Session.h"

#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace {

enum Role { TextRole = Qt::UserRole, IdRole, NameRole, AnimatedRole, SearchRole };

struct UnicodeEmoji
{
    const char* emoji;
    const char* keywords;
};

// The most used emojis, with English keywords for search.
constexpr UnicodeEmoji CommonEmojis[] = {
    {"😀", "grinning smile happy"}, {"😃", "smiley happy"}, {"😄", "smile happy"}, {"😁", "grin"},
    {"😆", "laughing"}, {"😅", "sweat smile"}, {"🤣", "rofl rolling laughing"}, {"😂", "joy tears laughing"},
    {"🙂", "slight smile"}, {"🙃", "upside down"}, {"😉", "wink"}, {"😊", "blush"}, {"😇", "innocent halo"},
    {"🥰", "love hearts"}, {"😍", "heart eyes"}, {"🤩", "star struck"}, {"😘", "kiss"}, {"😋", "yum"},
    {"😛", "tongue"}, {"😜", "wink tongue"}, {"🤪", "zany crazy"}, {"🤑", "money"}, {"🤗", "hug"},
    {"🤭", "hand over mouth"}, {"🤫", "shush quiet"}, {"🤔", "thinking"}, {"🤐", "zipper mouth"},
    {"🤨", "raised eyebrow"}, {"😐", "neutral"}, {"😑", "expressionless"}, {"😶", "no mouth"}, {"😏", "smirk"},
    {"😒", "unamused"}, {"🙄", "eye roll"}, {"😬", "grimace"}, {"😌", "relieved"}, {"😔", "pensive"},
    {"😪", "sleepy"}, {"😴", "sleeping"}, {"😷", "mask sick"}, {"🤒", "thermometer sick"}, {"🤢", "nauseated"},
    {"🤮", "vomit"}, {"🥵", "hot"}, {"🥶", "cold"}, {"🥴", "woozy"}, {"😵", "dizzy"}, {"🤯", "exploding head mind blown"},
    {"🤠", "cowboy"}, {"🥳", "party"}, {"😎", "sunglasses cool"}, {"🤓", "nerd"}, {"😕", "confused"},
    {"😟", "worried"}, {"🙁", "frown"}, {"😮", "open mouth wow"}, {"😯", "hushed"}, {"😲", "astonished"},
    {"😳", "flushed"}, {"🥺", "pleading"}, {"😦", "frowning"}, {"😧", "anguished"}, {"😨", "fearful"},
    {"😰", "anxious sweat"}, {"😥", "sad relieved"}, {"😢", "cry sad"}, {"😭", "sob crying"}, {"😱", "scream"},
    {"😖", "confounded"}, {"😣", "persevere"}, {"😞", "disappointed"}, {"😓", "sweat"}, {"😩", "weary"},
    {"😫", "tired"}, {"🥱", "yawn"}, {"😤", "triumph angry"}, {"😡", "rage angry"}, {"😠", "angry"},
    {"🤬", "cursing"}, {"😈", "devil"}, {"💀", "skull dead"}, {"💩", "poop"}, {"🤡", "clown"},
    {"👻", "ghost"}, {"👽", "alien"}, {"🤖", "robot"}, {"😺", "cat smile"}, {"🙈", "see no evil monkey"},
    {"👍", "thumbs up like yes"}, {"👎", "thumbs down dislike no"}, {"👌", "ok"}, {"✌️", "victory peace"},
    {"🤞", "crossed fingers"}, {"🤟", "love you"}, {"🤘", "rock horns"}, {"👋", "wave hello bye"},
    {"👏", "clap"}, {"🙌", "raised hands"}, {"🙏", "pray please thanks"}, {"💪", "muscle strong"},
    {"👀", "eyes look"}, {"🧠", "brain"}, {"❤️", "heart love red"}, {"🧡", "orange heart"}, {"💛", "yellow heart"},
    {"💚", "green heart"}, {"💙", "blue heart"}, {"💜", "purple heart"}, {"🖤", "black heart"}, {"🤍", "white heart"},
    {"💔", "broken heart"}, {"💯", "hundred"}, {"💥", "boom"}, {"🔥", "fire lit"}, {"✨", "sparkles"},
    {"⭐", "star"}, {"🎉", "tada party"}, {"🎊", "confetti"}, {"🎁", "gift"}, {"🏆", "trophy"},
    {"🎮", "video game"}, {"🎧", "headphones"}, {"🎵", "music note"}, {"🍕", "pizza"}, {"🍔", "burger"},
    {"🍺", "beer"}, {"☕", "coffee"}, {"🐶", "dog"}, {"🐱", "cat"}, {"🐸", "frog"}, {"🦆", "duck"},
    {"🚀", "rocket"}, {"💤", "zzz sleep"}, {"✅", "check yes"}, {"❌", "x no cross"}, {"❓", "question"},
    {"❗", "exclamation"}, {"⚠️", "warning"}, {"🆗", "ok button"}, {"🇧🇷", "brazil flag"},
};

} // namespace

EmojiPicker::EmojiPicker(Session* session, ImageCache* images, const QString& guildId, QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , m_search(new QLineEdit)
    , m_grid(new QListWidget)
{
    setObjectName(QStringLiteral("emojiPicker"));
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(380, 360);

    m_search->setObjectName(QStringLiteral("emojiSearch"));
    m_search->setPlaceholderText(tr("Find the perfect emoji"));
    m_grid->setObjectName(QStringLiteral("emojiGrid"));
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setGridSize({42, 42});
    m_grid->setIconSize({32, 32});
    m_grid->setUniformItemSizes(true);
    m_grid->setFocusPolicy(Qt::NoFocus);

    QFont emojiFont = m_grid->font();
    emojiFont.setPixelSize(26);
    for (const UnicodeEmoji& entry : CommonEmojis) {
        auto* item = new QListWidgetItem(QString::fromUtf8(entry.emoji), m_grid);
        item->setFont(emojiFont);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(TextRole, item->text());
        item->setData(NameRole, item->text());
        item->setData(SearchRole, QString::fromUtf8(entry.keywords));
        item->setToolTip(QString::fromUtf8(entry.keywords).section(u' ', 0, 0));
    }

    // Custom emojis of the current guild; their pictures load asynchronously.
    for (const CustomEmoji& custom : session->customEmojis(guildId)) {
        auto* item = new QListWidgetItem(m_grid);
        const QUrl url(Markdown::customEmojiUrl(custom.id, custom.animated));
        item->setIcon(QIcon(QPixmap::fromImage(images->image(url))));
        item->setData(TextRole, QStringLiteral("<%1:%2:%3>").arg(custom.animated ? QStringLiteral("a") : QString(), custom.name, custom.id));
        item->setData(IdRole, custom.id);
        item->setData(NameRole, custom.name);
        item->setData(AnimatedRole, custom.animated);
        item->setData(SearchRole, custom.name.toLower());
        item->setToolTip(u':' + custom.name + u':');
        item->setData(Qt::UserRole + 10, url);
    }
    connect(images, &ImageCache::imageLoaded, this, [this, images](const QUrl& url) {
        for (int i = 0; i < m_grid->count(); ++i) {
            QListWidgetItem* item = m_grid->item(i);
            if (item->data(Qt::UserRole + 10).toUrl() == url)
                item->setIcon(QIcon(QPixmap::fromImage(images->image(url))));
        }
    });

    connect(m_search, &QLineEdit::textChanged, this, &EmojiPicker::filter);
    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        Emoji emoji;
        emoji.id = item->data(IdRole).toString();
        emoji.name = item->data(NameRole).toString();
        emoji.animated = item->data(AnimatedRole).toBool();
        emit picked(item->data(TextRole).toString(), emoji);
        close();
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_search);
    layout->addWidget(m_grid);
}

void EmojiPicker::popupAt(const QPoint& anchor)
{
    move(anchor.x() - width(), anchor.y() - height());
    show();
    m_search->setFocus();
}

void EmojiPicker::filter(const QString& query)
{
    const QString needle = query.trimmed().toLower();
    for (int i = 0; i < m_grid->count(); ++i) {
        QListWidgetItem* item = m_grid->item(i);
        item->setHidden(!needle.isEmpty() && !item->data(SearchRole).toString().contains(needle));
    }
}
