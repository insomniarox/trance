// A stand-in on Windows until it has listening of its own: hears nothing,
// and says so.
#include "audiocapture.h"

struct AudioCapture::Platform {};

void AudioCapture::setUp() {}
void AudioCapture::tearDown() {}

void AudioCapture::open() { setError(tr("Trance can't hear anything on Windows yet.")); }
void AudioCapture::close() {}

void AudioCapture::refreshSources() { listed({}); }

bool AudioCapture::listNodes(QList<Node> *nodes, QString *) {
    nodes->clear();
    return true;
}
