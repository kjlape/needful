#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// needful serve: the phone page as a daemon, and on Linux, the systemd user
// units and a Tailscale node of its own that put it on the tailnet. Nothing
// here is needed by the window. Installing is systemd on Linux and launchd on
// macOS; elsewhere only plain `needful serve` is offered.

struct ServeSetup {
    QString needful;     // Absolute path of the needful binary.
    int port = 8787;
    QString data;        // Empty for the usual data file.
    bool tailscale = false;
    QString tailscaled;  // Absolute path, when tailscale is on.
    QString stateDir;    // The node's own Tailscale state.
    QString universe;    // Empty for your own list; units for another are named for it.
};

constexpr int DefaultServePort = 8787;

// File name to contents, for ~/.config/systemd/user.
QMap<QString, QByteArray> serviceUnits(const ServeSetup &setup);
QString systemdUserDirectory();
// The same two services as launchd agents, for ~/Library/LaunchAgents on macOS.
QMap<QString, QByteArray> launchAgents(const ServeSetup &setup);
QString launchAgentsDirectory();
// For a universe other than your own (empty), each is its own.
QString tailscaleStateDirectory(const QString &universe = {});
QString tailscaleSocket(const QString &universe = {});

// needful sync rsync: a timer that copies each machine's file both ways
// between this machine's sync folder and another computer's, over SSH. Safe
// with plain rsync because no file in the folder has two writers, and rsync
// keeps whichever copy its writer saved last.
struct RsyncSetup {
    QString rsync;  // Absolute path of rsync.
    // False for macOS's own openrsync (and rsync 2), which has neither
    // --protect-args nor --mkpath; a remote path is then quoted for the shell.
    bool modern = true;
    QString local;  // This machine's sync folder.
    QString host;   // As ssh knows it: desktop, me@desktop.lan.
    QString remote; // The folder there; relative paths are from its home.
    QString every = "5m";
};
QMap<QString, QByteArray> rsyncUnits(const RsyncSetup &setup);
// The same swap as a launchd agent on macOS: one job that runs both commands
// every interval, and once at load.
QMap<QString, QByteArray> rsyncLaunchAgents(const RsyncSetup &setup);
// Homebrew's rsync 3 when there is one, since macOS ships an older openrsync.
QString findRsync();
bool rsyncIsModern(const QString &rsync);
extern const QString RsyncAgent;  // The agent's file name.

// Load or unload a launchd agent file from ~/Library/LaunchAgents, replacing
// one already loaded. False when launchctl refuses it.
bool loadLaunchAgent(const QString &directory, const QString &file);
void unloadLaunchAgent(const QString &file);
// Pull first, then push.
QList<QStringList> rsyncCommands(const RsyncSetup &setup);
extern const QString RsyncService, RsyncTimer;

// needful sync install: keeps the link to your paired computers up, on a
// machine where neither the window nor the phone page's service usually
// runs. data is empty for the usual data file.
QMap<QString, QByteArray> linkUnits(const QString &needful, const QString &data);
extern const QString LinkService;

int runServe(const QStringList &arguments);
// SIGTERM and Ctrl+C end the event loop, so a long-running command shuts
// down cleanly.
void stopOnSignals();
