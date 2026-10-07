#include "MentionPopup.h"

#include "Avatar.h"
#include "Theme.h"

#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

constexpr int RowHeight = 36;
constexpr int MaxVisibleRows = 8;

enum ItemRole { IndexRole = Qt::UserRole };

// Icon, name and a dim detail on the right, like Discord's autocomplete rows.
class SuggestionDelegate : public QStyledItemDelegate
{
public:
    SuggestionDelegate(const QList<MentionSuggestion>* suggestions, QObject* parent)
        : QStyledItemDelegate(parent)
        , m_suggestions(suggestions)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {option.rect.width(), RowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int i = index.data(IndexRole).toInt();
        if (i < 0 || i >= m_suggestions->size())
            return;
        const MentionSuggestion& suggestion = m_suggestions->at(i);
        const Theme::Palette& colors = Theme::instance().palette();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect row = option.rect.adjusted(4, 1, -4, -1);
        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.hover);
            painter->drawRoundedRect(row, 4, 4);
        }

        const QRect icon(row.left() + 8, row.center().y() - 12, 24, 24);
        if (suggestion.kind == MentionSuggestion::User) {
            painter->drawPixmap(icon, makeAvatar(suggestion.label, suggestion.avatar, 24, painter->device()->devicePixelRatioF()));
        } else if (!suggestion.icon.isEmpty()) {
            QIcon(suggestion.icon).paint(painter, icon.adjusted(2, 2, -2, -2));
        } else {
            QFont symbol = option.font;
            symbol.setPixelSize(18);
            symbol.setBold(true);
            painter->setFont(symbol);
            painter->setPen(suggestion.color.isValid() ? suggestion.color : colors.textMuted);
            painter->drawText(icon, Qt::AlignCenter, QStringLiteral("@"));
        }

        QFont font = option.font;
        font.setPixelSize(15);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const int textLeft = icon.right() + 10;
        const int detailWidth = std::min(metrics.horizontalAdvance(suggestion.detail) + 8, (row.right() - textLeft) / 2);
        const QRect labelRect(textLeft, row.top(), row.right() - textLeft - detailWidth - 8, row.height());
        painter->setPen(suggestion.color.isValid() ? suggestion.color : colors.textBright);
        painter->drawText(labelRect, Qt::AlignVCenter, metrics.elidedText(suggestion.label, Qt::ElideRight, labelRect.width()));

        if (!suggestion.detail.isEmpty()) {
            QFont small = option.font;
            small.setPixelSize(13);
            painter->setFont(small);
            painter->setPen(colors.textMuted);
            const QRect detailRect(row.right() - detailWidth - 8, row.top(), detailWidth, row.height());
            painter->drawText(detailRect, Qt::AlignVCenter | Qt::AlignRight,
                              QFontMetrics(small).elidedText(suggestion.detail, Qt::ElideRight, detailRect.width()));
        }
        painter->restore();
    }

private:
    const QList<MentionSuggestion>* m_suggestions;
};

} // namespace

MentionPopup::MentionPopup(QWidget* parent)
    : QFrame(parent)
    , m_title(new QLabel)
    , m_list(new QListWidget)
{
    setObjectName(QStringLiteral("mentionPopup"));
    setAttribute(Qt::WA_StyledBackground);
    m_title->setObjectName(QStringLiteral("mentionPopupTitle"));
    m_list->setObjectName(QStringLiteral("mentionList"));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setItemDelegate(new SuggestionDelegate(&m_suggestions, m_list));
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) { m_list->setCurrentItem(item); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { pick(m_list->row(item)); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->setSpacing(4);
    layout->addWidget(m_title);
    layout->addWidget(m_list);
    hide();
}

void MentionPopup::setSuggestions(const QString& title, const QList<MentionSuggestion>& suggestions)
{
    const QString current = m_list->currentRow() >= 0 && m_list->currentRow() < m_suggestions.size()
        ? m_suggestions[m_list->currentRow()].raw : QString();
    m_suggestions = suggestions;
    m_title->setText(title.toUpper());
    m_list->clear();
    int selected = 0;
    for (qsizetype i = 0; i < suggestions.size(); ++i) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(IndexRole, static_cast<int>(i));
        if (suggestions[i].raw == current)
            selected = static_cast<int>(i);
    }
    m_list->setCurrentRow(selected);
    const int rows = std::min<int>(static_cast<int>(suggestions.size()), MaxVisibleRows);
    m_list->setFixedHeight(rows * RowHeight + 2);
    adjustSize();
}

void MentionPopup::placeAbove(const QRect& anchor)
{
    resize(anchor.width(), sizeHint().height());
    move(anchor.left(), anchor.top() - height() - 6);
    raise();
}

bool MentionPopup::handleKey(QKeyEvent* event)
{
    if (!isVisible() || m_suggestions.isEmpty())
        return false;
    const int count = static_cast<int>(m_suggestions.size());
    switch (event->key()) {
    case Qt::Key_Up:
        m_list->setCurrentRow((m_list->currentRow() + count - 1) % count);
        return true;
    case Qt::Key_Down:
        m_list->setCurrentRow((m_list->currentRow() + 1) % count);
        return true;
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (event->modifiers() & Qt::ShiftModifier)
            return false;
        pick(m_list->currentRow());
        return true;
    case Qt::Key_Escape:
        hide();
        emit dismissed();
        return true;
    default:
        return false;
    }
}

void MentionPopup::pick(int row)
{
    if (row < 0 || row >= m_suggestions.size())
        return;
    const MentionSuggestion suggestion = m_suggestions[row];
    hide();
    emit picked(suggestion);
}
