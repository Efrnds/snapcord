#pragma once

#include "core/MessageStore.h"

#include <QWidget>

class QHBoxLayout;

// The files waiting to be sent with the next message, shown as cards above the composer text.
class AttachmentTray : public QWidget
{
    Q_OBJECT

public:
    explicit AttachmentTray(QWidget* parent = nullptr);

    void setFiles(const QList<OutgoingFile>& files);

signals:
    void removeRequested(int index);

private:
    QWidget* makeCard(const OutgoingFile& file, int index);

    QHBoxLayout* m_cards;
};
