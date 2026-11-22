#pragma once

#include <QStringList>

class QImage;

// The commands that need no window: checking, rendering and listing presets,
// and steering a running Trance. `trance help` lists them.
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
QString cliSummary();

// How a picture is lit: the share of its pixels with a channel at the top
// (250 of 255 or more), and its mean luminance, both 0 to 1. What
// `render --json` reports, and what the mash-up audit in the tests reads.
struct Exposure {
    double clipped = 0, mean = 0;
};
Exposure measureExposure(const QImage &image);
