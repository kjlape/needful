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
};

constexpr int DefaultServePort = 8787;

// File name to contents, for ~/.config/systemd/user.
QMap<QString, QByteArray> serviceUnits(const ServeSetup &setup);
QString systemdUserDirectory();
QString tailscaleStateDirectory();
QString tailscaleSocket();

int runServe(const QStringList &arguments);
