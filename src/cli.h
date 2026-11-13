#pragma once

#include <QStringList>

// The commands that need no window: checking, rendering and listing presets,
// and steering a running Trance. `trance help` lists them.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();

// The D-Bus names a running Trance answers on.
namespace remote {
inline const char *const WindowService = "dev.trance.Visualizer";
inline const char *const Path = "/Visualizer";
inline const char *const Interface = "dev.trance.Visualizer";
} // namespace remote
