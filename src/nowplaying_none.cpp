// A stand-in on Windows until it asks the system what is playing: no track
// is ever reported.
#include "nowplaying.h"

NowPlaying::NowPlaying(QObject *parent) : QObject(parent) {}
