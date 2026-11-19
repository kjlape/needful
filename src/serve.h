#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// needful serve: the phone page as a daemon, and on Linux, the systemd user
// units and a Tailscale node of its own that put it on the tailnet. Nothing
// here is needed by the window, so other platforms simply don't offer the
// install commands' Linux parts.

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
// For a universe other than your own (empty), each is its own.
QString tailscaleStateDirectory(const QString &universe = {});
QString tailscaleSocket(const QString &universe = {});

// needful sync rsync: a timer that copies each machine's file both ways
// between this machine's sync folder and another computer's, over SSH. Safe
// with plain rsync because no file in the folder has two writers, and rsync
// keeps whichever copy its writer saved last.
struct RsyncSetup {
    QString rsync;  // Absolute path of rsync.
    QString local;  // This machine's sync folder.
    QString host;   // As ssh knows it: desktop, me@desktop.lan.
    QString remote; // The folder there; relative paths are from its home.
    QString every = "5m";
};
QMap<QString, QByteArray> rsyncUnits(const RsyncSetup &setup);
// Pull first, then push.
QList<QStringList> rsyncCommands(const RsyncSetup &setup);
extern const QString RsyncService, RsyncTimer;

int runServe(const QStringList &arguments);
