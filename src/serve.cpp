#include "serve.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTcpSocket>
#include <cstdio>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "backend.h"
#include "httpserver.h"
#include "theme.h"
#include "universes.h"
#include "webapi.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

// --data, or the --universe named; false with a message for an unknown one.
bool dataOption(const QCommandLineParser &parser, QString *path) {
    *path = parser.value("data");
    if (parser.isSet("data") || !parser.isSet("universe"))
        return true;
    *path = Universes().dataPath(parser.value("universe"));
    if (path->isEmpty())
        print(stderr, QString("No universe named %1. See: needful universe").arg(parser.value("universe")));
    return !path->isEmpty();
}

// Your own list keeps the names it always had; another universe's gets
// its own beside it: needful-serve-hers.service, serve-hers.json, and so on.
QString suffix(const QString &universe) { return universe.isEmpty() ? QString() : "-" + universe; }
QString serveUnit(const QString &universe) { return "needful-serve" + suffix(universe) + ".service"; }
QString tailscaleUnit(const QString &universe) { return "needful-tailscale" + suffix(universe) + ".service"; }

// --universe as a name, empty for your own; false with a message for one
// this computer doesn't know, unless it's being removed.
bool universeOption(const QCommandLineParser &parser, QString *universe, bool mayBeGone = false) {
    *universe = parser.value("universe").trimmed().toLower();
    if (*universe == Universes::Mine)
        universe->clear();
    if (universe->isEmpty())
        return true;
    if (mayBeGone ? Universes::validName(*universe) : !Universes().dataPath(*universe).isEmpty())
        return true;
    print(stderr, QString("No universe named %1. See: needful universe").arg(*universe));
    return false;
}

// One argument for an ExecStart line: quoted when it has spaces, with
// systemd's % specifiers escaped so a path is taken literally.
QByteArray unitArgument(const QString &value) {
    QString escaped = value;
    escaped.replace("%", "%%");
    if (escaped.contains(' ') || escaped.contains('"') || escaped.contains('\\')) {
        escaped.replace("\\", "\\\\").replace("\"", "\\\"");
        escaped = "\"" + escaped + "\"";
    }
    return escaped.toUtf8();
}

QString setupFile(const QString &universe) {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/serve" + suffix(universe) + ".json";
}

int portIn(const QString &file) {
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly))
        return 0;
    const int port = QJsonDocument::fromJson(handle.readAll()).object().value("port").toInt();
    return port > 0 && port < 65536 ? port : 0;
}

// The port picked at install time, so login and status need not be told
// again. Before it's installed, another universe takes the first port no
// other install uses, so it never collides with yours.
int installedPort(const QString &universe) {
    if (const int port = portIn(setupFile(universe)))
        return port;
    if (universe.isEmpty())
        return DefaultServePort;
    QList<int> used{DefaultServePort};
    const QDir folder(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    for (const QString &file : folder.entryList({"serve*.json"}, QDir::Files))
        used.append(portIn(folder.filePath(file)));
    int port = DefaultServePort;
    while (used.contains(port))
        ++port;
    return port;
}

bool readPort(const QCommandLineParser &parser, int *port, const QString &universe) {
    if (!parser.isSet("port")) {
        *port = installedPort(universe);
        return true;
    }
    bool ok = false;
    *port = parser.value("port").toInt(&ok);
    if (!ok || *port < 1 || *port > 65535) {
        print(stderr, "--port takes a number from 1 to 65535.");
        return false;
    }
    return true;
}

QString tailscale() { return QStandardPaths::findExecutable("tailscale"); }

// Runs tailscale against Needful's own node, never the system's.
int runTailscale(const QString &universe, const QStringList &arguments, QByteArray *output = nullptr) {
    QProcess process;
    if (!output)
        process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(tailscale(), QStringList{"--socket=" + tailscaleSocket(universe)} + arguments);
    if (!process.waitForStarted())
        return -1;
    process.waitForFinished(-1);
    if (output)
        *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

QString tailnetUrl(const QString &universe) {
    if (tailscale().isEmpty() || !QFileInfo::exists(tailscaleSocket(universe)))
        return {};
    QByteArray output;
    if (runTailscale(universe, {"status", "--json"}, &output) != 0)
        return {};
    const QJsonObject status = QJsonDocument::fromJson(output).object();
    QString name = status.value("Self").toObject().value("DNSName").toString();
    if (status.value("BackendState").toString() != "Running" || name.isEmpty())
        return {};
    if (name.endsWith('.'))
        name.chop(1);
    return "https://" + name + "/";
}

#ifdef Q_OS_UNIX
// SIGTERM from systemd and Ctrl+C both end the event loop, so the server
// shuts down cleanly. The handler only writes a byte; Qt does the rest.
int signalPipe[2];
void onSignal(int) {
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(signalPipe[0], &byte, 1);
}
void quitOnSignals() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(signalPipe[1], QSocketNotifier::Read, qApp);
    QObject::connect(notifier, &QSocketNotifier::activated, qApp, [] {
        char byte;
        [[maybe_unused]] const auto read = ::read(signalPipe[1], &byte, 1);
        QCoreApplication::quit();
    });
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
}
#else
void quitOnSignals() {}
#endif

int serve(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Serve the phone page and its API until stopped.\n\n"
        "Also: serve install [--tailscale] [--port N], serve login, serve status, serve uninstall\n"
        "(needful serve <command> --help for each).");
    parser.addHelpOption();
    parser.addPositionalArgument("serve", "", "serve");
    parser.addOption({"port", QString("Port to listen on (%1)").arg(DefaultServePort), "port"});
    parser.addOption({"host", "Address to listen on (127.0.0.1)", "address", "127.0.0.1"});
    parser.addOption({"data", "Use this data file instead of the usual one", "path"});
    parser.addOption({"universe", "Serve another universe's list, like hers", "name"});
    parser.process(arguments);
    if (parser.positionalArguments().size() > 1)
        return fail("Unknown serve command " + parser.positionalArguments().value(1) +
                    ". Use: serve, serve install, serve login, serve status, or serve uninstall.");
    QString universe;
    if (!universeOption(parser, &universe))
        return 1;
    // Another universe's page takes its own port, so it can run beside yours.
    int port = DefaultServePort;
    if ((parser.isSet("port") || !universe.isEmpty()) && !readPort(parser, &port, universe))
        return 1;
    const QHostAddress host(parser.value("host"));
    if (host.isNull())
        return fail("--host takes an IP address, like 127.0.0.1.");

    QString data;
    if (!dataOption(parser, &data))
        return 1;
    Backend backend(data);
    if (!backend.error().isEmpty())
        return fail(backend.error());
    // An explicit theme folder keeps the daemon from polling hyprctl.
    Theme theme(QDir::homePath() + "/.local/state/omarchy/current");
    WebApi api(&backend, &theme);
    HttpServer server([&api](const HttpRequest &request) { return api.handle(request); });
    if (!server.listen(host, port))
        return fail(QString("Could not listen on %1:%2: %3").arg(host.toString()).arg(port).arg(server.errorString()));
    print(stdout, QString("Serving Needful at http://%1:%2/ from %3")
                      .arg(host.protocol() == QAbstractSocket::IPv6Protocol ? "[" + host.toString() + "]" : host.toString())
                      .arg(server.port())
                      .arg(backend.dataPath()));
    if (!host.isLoopback())
        print(stderr, "Anyone who can reach this address can read and change your list; there is no password.");
    fflush(stdout);
    quitOnSignals();
    return QCoreApplication::exec();
}

int install(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Install systemd user units that keep the phone page running.");
    parser.addHelpOption();
    parser.addPositionalArgument("serve install", "", "serve install");
    parser.addOption({"port", QString("Port for the page (%1)").arg(DefaultServePort), "port"});
    parser.addOption({"data", "Serve this data file instead of the usual one", "path"});
    parser.addOption({"universe", "Serve another universe's list instead of yours, like hers", "name"});
    parser.addOption({"tailscale", "Also run a Tailscale node named needful, for https://needful.<tailnet>.ts.net"});
    parser.process(arguments);
    QString universe;
    if (!universeOption(parser, &universe))
        return 1;
    int port = DefaultServePort;
    if (!readPort(parser, &port, universe))
        return 1;

    ServeSetup setup;
    setup.universe = universe;
    setup.needful = QCoreApplication::applicationFilePath();
    setup.port = port;
    QString data;
    if (!dataOption(parser, &data))
        return 1;
    setup.data = data.isEmpty() ? QString() : QFileInfo(data).absoluteFilePath();
    setup.tailscale = parser.isSet("tailscale");
    if (setup.tailscale) {
        setup.tailscaled = QStandardPaths::findExecutable("tailscaled");
        if (setup.tailscaled.isEmpty() || tailscale().isEmpty())
            return fail("--tailscale needs tailscale and tailscaled. On Arch: pacman -S tailscale "
                        "(the system's tailscaled service doesn't need to run).");
        setup.stateDir = tailscaleStateDirectory(universe);
        if (!QDir().mkpath(setup.stateDir))
            return fail("Could not create " + setup.stateDir);
    }

    const QDir units(systemdUserDirectory());
    if (!units.mkpath("."))
        return fail("Could not create " + units.path());
    const auto files = serviceUnits(setup);
    for (auto it = files.begin(); it != files.end(); ++it) {
        QSaveFile file(units.filePath(it.key()));
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) < 0 || !file.commit())
            return fail("Could not write " + file.fileName());
        print(stdout, "Wrote " + file.fileName());
    }
    QSaveFile config(setupFile(universe));
    QDir().mkpath(QFileInfo(setupFile(universe)).absolutePath());
    if (config.open(QIODevice::WriteOnly)) {
        config.write(QJsonDocument(QJsonObject{{"port", port}, {"tailscale", setup.tailscale}}).toJson());
        config.commit();
    }

    const QString also = universe.isEmpty() ? QString() : " --universe " + universe;
    QString steps = "\nNext:\n  systemctl --user daemon-reload\n  systemctl --user enable --now " + serveUnit(universe);
    if (setup.tailscale)
        steps += " " + tailscaleUnit(universe) + "\n  needful serve login" + also +
                 "       (once, to add the needful" + suffix(universe) + " node to your tailnet)";
    steps += QString("\n\nThen open http://127.0.0.1:%1/ here").arg(port);
    steps += setup.tailscale ? ", or https://needful" + suffix(universe) + ".<your tailnet>.ts.net/ on your phone." : ".";
    print(stdout, steps);
    return 0;
}

int login(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Add Needful's own node to your tailnet and serve the page over HTTPS.");
    parser.addHelpOption();
    parser.addPositionalArgument("serve login", "", "serve login");
    parser.addOption({"port", "The page's port (the installed one)", "port"});
    parser.addOption({"hostname", "The node's name on the tailnet (needful, or needful-<universe>)", "name"});
    parser.addOption({"universe", "Another universe's install, like hers", "name"});
    parser.process(arguments);
    QString universe;
    if (!universeOption(parser, &universe))
        return 1;
    int port = DefaultServePort;
    if (!readPort(parser, &port, universe))
        return 1;
    const QString hostname = parser.isSet("hostname") ? parser.value("hostname") : "needful" + suffix(universe);
    if (tailscale().isEmpty())
        return fail("tailscale isn't installed. On Arch: pacman -S tailscale");
    const QString also = universe.isEmpty() ? QString() : " --universe " + universe;
    if (!QFileInfo::exists(tailscaleSocket(universe)))
        return fail("Needful's Tailscale node isn't running. Install and start it first:\n"
                    "  needful serve install --tailscale" + also + "\n"
                    "  systemctl --user daemon-reload\n"
                    "  systemctl --user enable --now " + tailscaleUnit(universe));

    print(stdout, "Signing the " + hostname + " node in; open the link tailscale prints if it asks.");
    if (runTailscale(universe, {"up", "--hostname=" + hostname}) != 0)
        return fail("tailscale up didn't finish.");
    // Serve settings live in the node's own state, so this runs once.
    if (runTailscale(universe, {"serve", "--bg", "--https=443", QString("http://127.0.0.1:%1").arg(port)}) != 0)
        return fail("tailscale serve didn't finish. HTTPS certificates may need turning on for your tailnet "
                    "(admin console, DNS, HTTPS Certificates).");
    const QString url = tailnetUrl(universe);
    print(stdout, url.isEmpty() ? "Done." : "Needful is at " + url);
    return 0;
}

int status(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Say whether the page is being served, and where.");
    parser.addHelpOption();
    parser.addPositionalArgument("serve status", "", "serve status");
    parser.addOption({"port", "The page's port (the installed one)", "port"});
    parser.addOption({"json", "Print the result as JSON"});
    parser.addOption({"universe", "Another universe's install, like hers", "name"});
    parser.process(arguments);
    QString universe;
    if (!universeOption(parser, &universe))
        return 1;
    int port = DefaultServePort;
    if (!readPort(parser, &port, universe))
        return 1;

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    bool answering = false;
    if (socket.waitForConnected(1500)) {
        socket.write("GET /api/state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        QByteArray reply;
        while (socket.waitForReadyRead(1500))
            reply += socket.readAll();
        answering = reply.startsWith("HTTP/1.1 200");
    }
    const QString local = QString("http://127.0.0.1:%1/").arg(port);
    const QString tailnet = tailnetUrl(universe);
    if (parser.isSet("json")) {
        fputs(QJsonDocument(QJsonObject{{"serving", answering}, {"local", local}, {"tailnet", tailnet}})
                  .toJson().constData(), stdout);
    } else {
        print(stdout, answering ? "Serving at " + local : "Not serving on port " + QString::number(port) +
                                                              ". Start it with: systemctl --user start " + serveUnit(universe));
        if (!tailnet.isEmpty())
            print(stdout, "On your tailnet at " + tailnet);
        else if (QFileInfo::exists(tailscaleSocket(universe)))
            print(stdout, "The needful" + suffix(universe) + " Tailscale node is running but not signed in: needful serve login" +
                              (universe.isEmpty() ? QString() : " --universe " + universe));
    }
    return answering ? 0 : 1;
}

int uninstall(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Stop and remove the systemd user units.");
    parser.addHelpOption();
    parser.addPositionalArgument("serve uninstall", "", "serve uninstall");
    parser.addOption({"universe", "Another universe's install, like hers", "name"});
    parser.process(arguments);
    QString universe;
    if (!universeOption(parser, &universe, true))
        return 1;
    QDir units(systemdUserDirectory());
    QStringList present;
    for (const QString &name : {serveUnit(universe), tailscaleUnit(universe)})
        if (units.exists(name))
            present.append(name);
    if (present.isEmpty())
        return fail("Nothing to remove; no Needful units in " + units.path());
    const QString systemctl = QStandardPaths::findExecutable("systemctl");
    if (!systemctl.isEmpty())
        QProcess::execute(systemctl, QStringList{"--user", "disable", "--now"} + present);
    for (const QString &name : present) {
        units.remove(name);
        print(stdout, "Removed " + units.filePath(name));
    }
    if (!systemctl.isEmpty())
        QProcess::execute(systemctl, {"--user", "daemon-reload"});
    QFile::remove(setupFile(universe));
    if (QFileInfo::exists(tailscaleStateDirectory(universe)))
        print(stdout, "The needful" + suffix(universe) + " node's Tailscale state stays in " + tailscaleStateDirectory(universe) +
                          "; delete it, and remove the device in the Tailscale admin console, to forget it.");
    return 0;
}
}

QString systemdUserDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/systemd/user";
}

QString tailscaleStateDirectory(const QString &universe) {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tailscale" + suffix(universe);
}

QString tailscaleSocket(const QString &universe) {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/needful-tailscaled" + suffix(universe) +
           ".sock";
}

QMap<QString, QByteArray> serviceUnits(const ServeSetup &setup) {
    QByteArray exec = unitArgument(setup.needful) + " serve --port " + QByteArray::number(setup.port);
    if (!setup.data.isEmpty())
        exec += " --data " + unitArgument(setup.data);
    QMap<QString, QByteArray> units;
    const QString universe = setup.universe;
    units[serveUnit(universe)] = "[Unit]\n"
                       "Description=Needful on your phone: the page and its API" +
                       (universe.isEmpty() ? QByteArray() : ", for " + universe.toUtf8()) + "\n"
                       "After=network.target\n\n"
                       "[Service]\n"
                       "ExecStart=" + exec + "\n"
                       "Restart=on-failure\n"
                       "RestartSec=5\n\n"
                       "[Install]\n"
                       "WantedBy=default.target\n";
    if (setup.tailscale) {
        // A node of its own, in userspace, so the system's Tailscale (and
        // this machine's identity on the tailnet) is left exactly as it was.
        units[tailscaleUnit(universe)] = "[Unit]\n"
                               "Description=Needful's own Tailscale node, separate from the system's\n"
                               "After=network-online.target\n"
                               "Wants=network-online.target\n\n"
                               "[Service]\n"
                               "ExecStart=" + unitArgument(setup.tailscaled) +
                               " --tun=userspace-networking " + unitArgument("--statedir=" + setup.stateDir) +
                               " --socket=%t/needful-tailscaled" + suffix(universe).toUtf8() + ".sock --port=0\n"
                               "Restart=on-failure\n"
                               "RestartSec=5\n\n"
                               "[Install]\n"
                               "WantedBy=default.target\n";
    }
    return units;
}

const QString RsyncService = "needful-rsync.service";
const QString RsyncTimer = "needful-rsync.timer";

QList<QStringList> rsyncCommands(const RsyncSetup &setup) {
    // -u keeps the receiver's copy when it's newer. Every copy of a file
    // carries the time its one writer saved it, so clocks that disagree
    // between computers can't make an old copy look new. Nothing is deleted.
    const QStringList options{"-au", "--protect-args", "--timeout=60",
                              "-e", "ssh -o BatchMode=yes -o ConnectTimeout=15",
                              "--include=/*.json", "--exclude=*"};
    const QString here = setup.local + "/machines/", there = setup.host + ":" + setup.remote + "/machines/";
    return {QStringList{setup.rsync} + options + QStringList{there, here},
            QStringList{setup.rsync} + options + QStringList{"--mkpath", here, there}};
}

QMap<QString, QByteArray> rsyncUnits(const RsyncSetup &setup) {
    QByteArray exec;
    for (const QStringList &command : rsyncCommands(setup)) {
        QByteArrayList arguments;
        for (const QString &argument : command)
            arguments.append(unitArgument(argument));
        exec += "ExecStart=" + arguments.join(' ') + "\n";
    }
    QMap<QString, QByteArray> units;
    units[RsyncService] = "[Unit]\n"
                          "Description=Needful: swap machines' changes with " + setup.host.toUtf8() + " over rsync\n"
                          "After=network-online.target\n"
                          "Wants=network-online.target\n\n"
                          "[Service]\n"
                          "Type=oneshot\n" + exec;
    units[RsyncTimer] = "[Unit]\n"
                        "Description=Needful: swap changes with " + setup.host.toUtf8() + " every " +
                        setup.every.toUtf8() + "\n\n"
                        "[Timer]\n"
                        "OnStartupSec=1m\n"
                        "OnUnitInactiveSec=" + setup.every.toUtf8() + "\n\n"
                        "[Install]\n"
                        "WantedBy=timers.target\n";
    return units;
}

int runServe(const QStringList &arguments) {
    const QString command = arguments.value(2);
    if (command == "install") return install(arguments);
    if (command == "uninstall") return uninstall(arguments);
    if (command == "login") return login(arguments);
    if (command == "status") return status(arguments);
    return serve(arguments);
}
