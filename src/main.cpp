// Needful — what needs doing, most needful first.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QByteArray>
#include <QFileInfo>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "peerpanel.h"
#include "peers.h"
#include "theme.h"
#include "translator.h"
#include "universes.h"
#ifdef NEEDFUL_SPARKLE
#include "updater.h"
#endif

int main(int argc, char *argv[]) {
    // Bare needful prints help, as a command line tool should; the launcher
    // says needful open. Commands draw no window, so they need no display.
    // Launched from Finder (no arguments, inside an .app bundle) it opens the
    // window, since a double-click has no way to say needful open.
    const bool fromBundle = argc == 1 && QByteArray(argv[0]).contains(".app/Contents/MacOS/");
    const bool command = !fromBundle && (argc == 1 || isCliCommand(argv[1]));
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
    if (!fromBundle && app.arguments().value(1) != "open") {
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

    // The window opens on your own list unless asked for another universe,
    // or for a data file, which wins over a universe as it does for commands.
    QString dataPath, dataOption, universe;
    const QStringList arguments = app.arguments();
    for (int i = 2; i < arguments.size(); ++i) {
        for (const QString option : {"--data", "--universe"}) {
            QString value;
            if (arguments[i] == option) {
                if (i + 1 >= arguments.size()) {
                    fprintf(stderr, "%s needs a value.\n", qPrintable(option));
                    return 1;
                }
                value = arguments[++i];
            } else if (arguments[i].startsWith(option + "=")) {
                value = arguments[i].mid(option.size() + 1);
            } else {
                continue;
            }
            (option == "--data" ? dataOption : universe) = value;
            break;
        }
    }
    if (!dataOption.isEmpty()) {
        dataPath = QFileInfo(dataOption).absoluteFilePath();
    } else if (!universe.isEmpty()) {
        dataPath = Universes().dataPath(universe);
        if (dataPath.isEmpty()) {
            fprintf(stderr, "No universe named %s. See: needful universe\n", qPrintable(universe));
            return 1;
        }
    }

    Theme theme;
    Backend backend(dataPath);
    // The open window keeps your paired computers in step, unless another
    // Needful here (the phone page's service, sync install) already does.
    // A data file given with --data is a list apart, as with serve.
    PeerLink link;
    QString linkError;
    if (dataOption.isEmpty() && !link.start(&linkError) && !linkError.isEmpty())
        fprintf(stderr, "needful: %s\n", qPrintable(linkError));
    PeerPanel peers(&link, PeerLink::appDirFor({}));

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    // Due dates and "3 days late" are written in C++, so they follow too.
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::refresh);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("peers", &peers);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef NEEDFUL_SPARKLE
    // Only the window looks for updates; commands and services never do.
    // Once the event loop runs, so Qt has built the app menu.
    QTimer::singleShot(0, &app, startUpdater);
#endif

    return app.exec();
}
