#include "app/MainWindow.h"
#include "app/SingleInstance.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>

namespace {

// The Ubuntu family ships inside the executable so the editor renders
// identically on machines without the fonts installed.
void loadBundledFonts()
{
    const char *files[] = {
        "Ubuntu-R.ttf",     "Ubuntu-B.ttf",     "Ubuntu-RI.ttf",
        "UbuntuMono-R.ttf", "UbuntuMono-B.ttf", "UbuntuMono-RI.ttf",
        "UbuntuMono-BI.ttf",
    };
    for (const char *file : files)
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + QLatin1String(file));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral(APP_NAME));
    app.setApplicationName(QStringLiteral(APP_NAME));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/images/CRLF_basic_light.png")));

    loadBundledFonts();
    app.setFont(QFont(QStringLiteral("Ubuntu"), 10));

    // Resolve relative arguments against this launch's working directory
    // before they cross into another process or a restored session.
    QStringList paths;
    for (const QString &arg : app.arguments().mid(1))
        paths.append(QFileInfo(arg).absoluteFilePath());

    SingleInstance instance;
    if (!instance.isPrimary()) {
        instance.forwardToPrimary(paths);
        return 0;
    }

    MainWindow window;
    QObject::connect(&instance, &SingleInstance::pathsReceived,
                     &window, &MainWindow::openForwardedPaths);
    window.startSession(paths);
    window.show();
    return QApplication::exec();
}
