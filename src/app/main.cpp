#include "AppController.h"
#include "Demo.h"
#include "Language.h"
#include "Motion.h"
#include "Theme.h"
#include "core/Log.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Snapcord"));
    QApplication::setOrganizationName(QStringLiteral("Snapcord"));
    QApplication::setApplicationVersion(QStringLiteral(SNAPCORD_VERSION));

    // Demo mode keeps its settings, logs and cache apart from the real ones.
    const QStringList arguments = QApplication::arguments();
    const bool demo = arguments.contains(QStringLiteral("--demo"));
    const qsizetype screenshotsIndex = arguments.indexOf(QStringLiteral("--screenshots"));
    const QString screenshotFolder = screenshotsIndex >= 0 ? arguments.value(screenshotsIndex + 1) : QString();
    if (demo)
        QApplication::setApplicationName(QStringLiteral("Snapcord Demo"));
    // Matches the .desktop file, so Linux desktops (Wayland especially) show the right icon in the taskbar.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.pedrordgsr.Snapcord"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/snapcord.svg")));

    Log::installFileHandler();
    Language::install(app);

    // Fusion looks the same on all three platforms; Theme paints Discord-like colors on top.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // Font family comes from Theme settings (default stack: Noto Sans / Inter / Segoe UI).
    Theme::instance().apply(app);
    Motion::installWindowFades();

    if (demo) {
        // Dates and times in English too, so screenshots look the same on every machine.
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        // Screenshots must not catch a transition halfway.
        if (!screenshotFolder.isEmpty())
            Motion::suppress();
        DemoController controller;
        controller.start(screenshotFolder);
        return app.exec();
    }

    AppController controller;
    controller.start();
    return app.exec();
}
