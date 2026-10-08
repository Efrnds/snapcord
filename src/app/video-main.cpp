#include "Language.h"
#include "VideoViewer.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QRect>
#include <QUrl>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Snapcord"));
    QApplication::setOrganizationName(QStringLiteral("Snapcord"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.pedrordgsr.Snapcord"));

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2)
        return 2;
    const QUrl url(arguments.at(1));
    if (!VideoViewer::isDiscordFile(url))
        return 2;

    Language::install(app);

    QRect frame;
    if (arguments.size() >= 6) {
        frame = QRect(arguments.at(2).toInt(), arguments.at(3).toInt(), arguments.at(4).toInt(),
                      arguments.at(5).toInt());
    }
    VideoViewer::open(url, frame);
    return 0;
}
