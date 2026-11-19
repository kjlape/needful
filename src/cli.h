#pragma once

#include <QStringList>

// Commands that need no display, for scripts and coding agents.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();
