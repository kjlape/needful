// Needful — what needs doing, most needful first.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Bare needful prints help, as a command line tool should; the launcher
    // says needful open. Commands draw no window, so they need no display.
    const bool command = argc == 1 || isCliCommand(argv[1]);
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("needful");
    app.setApplicationVersion(APP_VERSION);

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes. Commands use it too,
    // for plurals and dates.
    Translator translator;
    app.installTranslator(&translator);
    if (command)
        return runCli(app.arguments());
    if (app.arguments().value(1) != "open") {
        fprintf(stderr, "Unknown command %s\n\nUsage: needful <command> [options]\n\n%s\n", argv[1],
                qPrintable(cliSummary()));
        return 1;
    }

    // Matches the window to needful.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon. A Flatpak installs
    // them under its app ID and says so in FLATPAK_ID.
    const QString desktopFile = qEnvironmentVariable("FLATPAK_ID", "needful");
    app.setDesktopFileName(desktopFile);
    app.setWindowIcon(QIcon::fromTheme(desktopFile));

    QQuickStyle::setStyle("Material");

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    // Due dates and "3 days late" are written in C++, so they follow too.
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::refresh);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
