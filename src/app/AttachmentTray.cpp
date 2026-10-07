#include "AttachmentTray.h"

#include <QBuffer>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int CardWidth = 148;
constexpr QSize PreviewSize(132, 84);

// A small preview of a picture, decoded straight at that size so large photos cost little memory.
QPixmap preview(const OutgoingFile& file, qreal devicePixelRatio)
{
    if (!file.contentType.startsWith(u"image/"))
        return {};
    QBuffer buffer;
    QImageReader reader;
    if (file.path.isEmpty()) {
        buffer.setData(file.data);
        buffer.open(QIODevice::ReadOnly);
        reader.setDevice(&buffer);
    } else {
        reader.setFileName(file.path);
    }
    reader.setAutoTransform(true);
    const QSize bounds = PreviewSize * devicePixelRatio;
    const QSize original = reader.size();
    if (original.isValid())
        reader.setScaledSize(original.scaled(bounds, Qt::KeepAspectRatio).boundedTo(original));
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (image.width() > bounds.width() || image.height() > bounds.height())
        image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

} // namespace

AttachmentTray::AttachmentTray(QWidget* parent)
    : QWidget(parent)
    , m_cards(new QHBoxLayout)
{
    setObjectName(QStringLiteral("attachmentTray"));
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("attachmentTrayContent"));
    m_cards->setContentsMargins(0, 0, 0, 0);
    m_cards->setSpacing(10);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addLayout(m_cards);
    contentLayout->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("attachmentTrayScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    scroll->setFixedHeight(150);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 4);
    layout->addWidget(scroll);
    hide();
}

void AttachmentTray::setFiles(const QList<OutgoingFile>& files)
{
    while (QLayoutItem* item = m_cards->takeAt(0)) {
        // Later: this can run from a card's own remove button.
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    for (qsizetype i = 0; i < files.size(); ++i)
        m_cards->addWidget(makeCard(files[i], static_cast<int>(i)));
    setVisible(!files.isEmpty());
}

QWidget* AttachmentTray::makeCard(const OutgoingFile& file, int index)
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("attachmentCard"));
    card->setFixedWidth(CardWidth);
    card->setToolTip(file.filename);

    auto* picture = new QLabel;
    picture->setObjectName(QStringLiteral("attachmentPreview"));
    picture->setFixedSize(PreviewSize);
    picture->setAlignment(Qt::AlignCenter);
    const QPixmap image = preview(file, devicePixelRatioF());
    if (!image.isNull())
        picture->setPixmap(image);
    else
        picture->setPixmap(QIcon(QStringLiteral(":/icons/file.svg")).pixmap(QSize(36, 48), devicePixelRatioF()));

    auto* name = new QLabel;
    name->setObjectName(QStringLiteral("attachmentName"));
    name->setText(name->fontMetrics().elidedText(file.filename, Qt::ElideMiddle, CardWidth - 16));
    auto* size = new QLabel(QLocale().formattedDataSize(file.size, 1, QLocale::DataSizeTraditionalFormat));
    size->setObjectName(QStringLiteral("attachmentSize"));

    auto* remove = new QToolButton(card);
    remove->setObjectName(QStringLiteral("attachmentRemove"));
    remove->setText(QStringLiteral("✕"));
    remove->setToolTip(tr("Remove Attachment"));
    remove->setCursor(Qt::PointingHandCursor);
    remove->setFixedSize(24, 24);
    remove->move(CardWidth - 28, 4);
    connect(remove, &QToolButton::clicked, this, [this, index] { emit removeRequested(index); });

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 8, 8, 6);
    layout->setSpacing(2);
    layout->addWidget(picture);
    layout->addWidget(name);
    layout->addWidget(size);
    remove->raise();
    return card;
}
