#pragma once

#include "core/Message.h"

#include <QFrame>

class ImageCache;
class QLineEdit;
class QListWidget;
class Session;

// Popup grid of common Unicode emojis plus the current guild's custom emojis, with search.
class EmojiPicker : public QFrame
{
    Q_OBJECT

public:
    EmojiPicker(Session* session, ImageCache* images, const QString& guildId, QWidget* parent = nullptr);

    // Opens the popup with its bottom-right corner at `anchor` (in global coordinates).
    void popupAt(const QPoint& anchor);

signals:
    // `text` is what goes into a message (the emoji itself, or <:name:id>); `emoji` is for reactions.
    void picked(const QString& text, const Emoji& emoji);

private:
    void filter(const QString& query);

    QLineEdit* m_search;
    QListWidget* m_grid;
};
