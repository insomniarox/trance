#pragma once

#include <QStringList>

// The commands that need no window: checking, rendering and listing presets,
// and steering a running Trance. `trance help` lists them.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();
