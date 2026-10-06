#include "AppController.h"
#include "Language.h"
#include "core/Log.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QPalette>

namespace {

// Dark palette for standard widgets (menus, dialogs); the QSS theme styles everything else.
QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x31, 0x33, 0x38));
    palette.setColor(QPalette::WindowText, QColor(0xdb, 0xde, 0xe1));
    palette.setColor(QPalette::Base, QColor(0x1e, 0x1f, 0x22));
    palette.setColor(QPalette::AlternateBase, QColor(0x2b, 0x2d, 0x31));
    palette.setColor(QPalette::Text, QColor(0xdb, 0xde, 0xe1));
    palette.setColor(QPalette::PlaceholderText, QColor(0x94, 0x9b, 0xa4));
    palette.setColor(QPalette::Button, QColor(0x4e, 0x50, 0x58));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(0x58, 0x65, 0xf2));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(0x11, 0x12, 0x14));
    palette.setColor(QPalette::ToolTipText, QColor(0xdb, 0xde, 0xe1));
    palette.setColor(QPalette::Link, QColor(0x00, 0xa8, 0xfc));
    return palette;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Snapcord"));
    QApplication::setOrganizationName(QStringLiteral("Snapcord"));
    QApplication::setApplicationVersion(QStringLiteral(SNAPCORD_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/snapcord.svg")));

    Log::installFileHandler();
    Language::install(app);

    // Fusion looks the same on all three platforms; the dark theme is applied on top of it.
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(darkPalette());

    QFont font = QApplication::font();
    font.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("Inter"), QStringLiteral("Segoe UI")});
    QApplication::setFont(font);

    QFile theme(QStringLiteral(":/theme/dark.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    AppController controller;
    controller.start();
    return app.exec();
}
