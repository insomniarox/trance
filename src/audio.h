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
    double m_sinceBeat = 1, m_time = 0;
    double m_lastBass = 0;
};

// A deterministic four-on-the-floor loop, so renders and tests have music
// to react to without a sound card.
void synthesize(float *interleavedStereo, int frames, qint64 startFrame, int sampleRate);
