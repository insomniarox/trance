#pragma once

#include <QtGlobal>
#include <vector>

// What one frame of the visuals knows about the music.
struct AudioFrame {
    static constexpr int WaveSize = 512;
    static constexpr int SpectrumSize = 256;

    // Mix, left, right: about 21 ms of signal, levelled to roughly -1..1.
    float wave[3][WaveSize] = {};
    // 0..1 per band, log-spaced from 30 Hz to 16 kHz.
    float spectrum[SpectrumSize] = {};
    // The transform those bands are made from, before any of that shaping:
    // even steps of rawStep Hz from 0 up, levelled like the waveform, so a
    // lone full-scale tone reads about 0.7 and a full mix far less a step.
    static constexpr int RawSize = 1024;
    float raw[RawSize] = {};
    float rawStep = 48000.f / (2 * RawSize);
    // What the user set the sensitivity to, for whoever levels raw itself.
    float sensitivity = 1;
    // Loudness per range relative to the song's own recent average, so 1.0
    // is "normal for this track" at any volume. The _att values are smoothed.
    float bass = 0, mid = 0, treb = 0, vol = 0;
    float bassAtt = 0, midAtt = 0, trebAtt = 0, volAtt = 0;
    // The same again for six narrower ranges, so the parts of a picture can
    // each follow a different part of the sound: the rumble under a kick,
    // the kick and the bass line, the warmth above them, the voices and
    // leads, the snap of a snare, and the shimmer of cymbals.
    static constexpr int Bands = 6;
    float band[Bands] = {}, bandAtt[Bands] = {};
    // Jumps to 1 on a beat and falls away; beats counts them.
    float beat = 0;
    int beats = 0;
    bool silent = true;
};

// What the six narrow ranges are called in formulas, lowest first, and
// where each begins and ends in Hz.
struct BandInfo {
    const char *name;
    double low, high;
};
const BandInfo &bandInfo(int band);

// Turns the captured stream into AudioFrames. Feed it samples as they
// arrive, ask for a frame whenever one is drawn.
class Analyzer {
public:
    explicit Analyzer(int sampleRate = 48000);

    void push(const float *interleavedStereo, int frames);
    const AudioFrame &analyze(double dt);
    const AudioFrame &frame() const { return m_frame; }

    void setSensitivity(double sensitivity) { m_sensitivity = sensitivity; }
    int sampleRate() const { return m_rate; }

private:
    // Fills a spectrum from the newest samples. A stride of 2 reads twice
    // as far back at half the rate, which halves what it can tell apart.
    void transform(int stride, std::vector<float> &magnitude);

    static constexpr int FftSize = 2048;
    static constexpr int RingSize = 8192;

    int m_rate;
    double m_sensitivity = 1.0;
    std::vector<float> m_ring[2];
    qint64 m_written = 0;

    std::vector<float> m_window, m_real, m_imag, m_cos, m_sin, m_magnitude, m_lowMagnitude;
    std::vector<int> m_reversed;

    AudioFrame m_frame;
    double m_level = 0.05;      // envelope the automatic gain follows
    double m_quietFor = 10;     // seconds without signal
    double m_average[3 + AudioFrame::Bands] = {}, m_longAverage[3 + AudioFrame::Bands] = {};
    double m_sinceBeat = 1;
    double m_heardFor = 0; // seconds of sound since the last silence
    double m_lastBass = 0;
};

// A second reading of the spectrum, the way SoundSpectrum's WhiteCap takes
// it, for the wireframe layer. Where AudioFrame::spectrum spaces its bands by
// octave and measures them in decibels, so that every part of the sound
// shows, this one keeps what stands out: a stretch of the transform is cut
// into bins of equal width in Hz, each the average of all that falls in it
// (a bucket collects over its whole mouth), loudness is left as it is rather
// than turned into decibels, the top of the stretch is weighted up, the
// neighbours are smoothed into each other, and the loudest bin of late is
// made 1. A bin rises at once and drifts back down slowly, so a held note
// piles up into a crest and a hit leaves a wake.
class Bins {
public:
    struct Settings {
        int count = 48;
        double low = 40, high = 5000; // the stretch, in Hz
        double tilt = 10;             // weight: 1 at the bottom of the stretch, 1 + tilt at the top
        double smooth = 0.35;         // 0 to 1, between neighbouring bins
        double fade = 0.35;           // seconds a bin takes to fall to about a third
    };
    // One call a frame. The values run from 0 to about 1, bottom of the
    // stretch first; a sudden hit overshoots for a moment.
    const std::vector<float> &update(const AudioFrame &frame, const Settings &settings, double dt);

private:
    std::vector<float> m_now, m_held;
    double m_level = 0;
};

// A deterministic four-on-the-floor loop, so renders and tests have music
// to react to without a sound card.
void synthesize(float *interleavedStereo, int frames, qint64 startFrame, int sampleRate);
