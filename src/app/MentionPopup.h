#pragma once

#include <QColor>
#include <QFrame>
#include <QImage>
#include <QList>
#include <QString>

class QKeyEvent;
class QLabel;
class QListWidget;

// One entry of the mention list: a member, a role, @everyone/@here or a channel.
struct MentionSuggestion
{
    enum Kind { User, Role, Everyone, Channel };

    Kind kind = User;
    QString display; // what goes into the composer ("@Ana", "#general")
    QString raw;     // what Discord receives ("<@123>", "<#456>")
    QString label;   // the main text of the row
    QString detail;  // the dim text on the right (username, explanation, category)
    QImage avatar;   // users only
    QString icon;    // channels: resource path of their icon
    QColor color;    // role color, invalid = default
};

// The list of suggestions shown above the composer while typing "@..." or "#...". It never takes the
// keyboard focus: the composer forwards the navigation keys to it.
class MentionPopup : public QFrame
{
    Q_OBJECT

public:
    explicit MentionPopup(QWidget* parent = nullptr);

    void setSuggestions(const QString& title, const QList<MentionSuggestion>& suggestions);
    // Up, Down, Tab, Enter and Escape; returns true if the key was used.
    bool handleKey(QKeyEvent* event);
    // Places the list right above `anchor` (in parent coordinates), as wide as it.
    void placeAbove(const QRect& anchor);

signals:
    void picked(const MentionSuggestion& suggestion);
    void dismissed();

private:
    void pick(int row);

    QLabel* m_title;
    QListWidget* m_list;
    QList<MentionSuggestion> m_suggestions;
};
