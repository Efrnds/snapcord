// Renders the application icon (resources/icons/snapcord.svg) into the formats each platform's packaging
// needs: PNGs for Linux, an .ico for Windows and an .icns for macOS. Run it after changing the SVG:
//   snapcord_render_icons <path to snapcord.svg> <output directory>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

namespace {

QImage render(QSvgRenderer& svg, int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    svg.render(&painter, QRectF(0, 0, size, size));
    return image;
}

bool save(const QImage& image, const QString& path, const char* format)
{
    QImageWriter writer(path, format);
    if (!writer.write(image)) {
        QTextStream(stderr) << "Could not write " << path << ": " << writer.errorString() << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        QTextStream(stderr) << "usage: snapcord_render_icons <icon.svg> <output dir>\n";
        return 2;
    }
    QSvgRenderer svg{QString::fromLocal8Bit(argv[1])};
    if (!svg.isValid())
        return 1;
    const QDir output(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(output.path());

    bool ok = true;
    // Linux: hicolor theme sizes.
    for (int size : {16, 32, 48, 64, 128, 256, 512})
        ok &= save(render(svg, size), output.filePath(QStringLiteral("snapcord-%1.png").arg(size)), "png");
    // Windows: the ICO writer stores one image, so use the size Explorer scales best from.
    ok &= save(render(svg, 256), output.filePath(QStringLiteral("snapcord.ico")), "ico");
    // macOS: the ICNS writer builds the icon family from a 1024 px image.
    ok &= save(render(svg, 1024), output.filePath(QStringLiteral("snapcord.icns")), "icns");
    return ok ? 0 : 1;
}
