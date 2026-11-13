#include "audio.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double Pi = 3.14159265358979323846;

// The ranges MilkDrop made standard: what drives a kick, a voice, a hi-hat.
constexpr double BandEdges[4] = {20, 250, 4000, 16000};
constexpr double SpectrumLow = 30, SpectrumHigh = 16000;

// Six ranges of about an octave and a half each, cut where a mix is usually
// cut: sub-bass, bass, low mids, high mids, presence, air.
constexpr BandInfo BandTable[AudioFrame::Bands] = {{"sub", 20, 60},          {"low", 60, 200},
                                                   {"lowmid", 200, 600},     {"highmid", 600, 2000},
                                                   {"presence", 2000, 6000}, {"air", 6000, 16000}};

// A per-frame smoothing rate tuned at 30 fps, applied over any frame time.
double rateFor(double rate, double dt) { return std::pow(rate, 30.0 * dt); }
} // namespace

const BandInfo &bandInfo(int band) { return BandTable[std::clamp(band, 0, AudioFrame::Bands - 1)]; }

Analyzer::Analyzer(int sampleRate) : m_rate(sampleRate) {
    for (auto &channel : m_ring)
        channel.assign(RingSize, 0.f);
    m_window.resize(FftSize);
    m_real.resize(FftSize);
    m_imag.resize(FftSize);
    m_magnitude.assign(FftSize / 2, 0.f);
    m_lowMagnitude.assign(FftSize / 2, 0.f);
    m_cos.resize(FftSize / 2);
    m_sin.resize(FftSize / 2);
    m_reversed.resize(FftSize);
    int bits = 0;
    while ((1 << bits) < FftSize)
        ++bits;
    for (int i = 0; i < FftSize; ++i) {
        m_window[i] = float(0.5 - 0.5 * std::cos(2 * Pi * i / (FftSize - 1)));
        int reversed = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                reversed |= 1 << (bits - 1 - b);
        m_reversed[i] = reversed;
    }
    for (int i = 0; i < FftSize / 2; ++i) {
        m_cos[i] = float(std::cos(2 * Pi * i / FftSize));
        m_sin[i] = float(std::sin(2 * Pi * i / FftSize));
    }
}

void Analyzer::push(const float *samples, int frames) {
    for (int i = 0; i < frames; ++i) {
        const int at = int((m_written + i) % RingSize);
        m_ring[0][at] = samples[i * 2];
        m_ring[1][at] = samples[i * 2 + 1];
    }
    m_written += frames;
}

// In-place radix-2 FFT of the newest FftSize samples; leaves magnitudes
// scaled so a full-scale sine reads 1.0.
void Analyzer::transform(int stride, std::vector<float> &magnitude) {
    const qint64 start = m_written - qint64(FftSize) * stride;
    for (int i = 0; i < FftSize; ++i) {
        // Neighbouring samples are averaged into one, which is all the
        // filtering the low end needs: what would fold down onto it from the
        // top of the range is cancelled almost entirely.
        float sample = 0;
        for (int k = 0; k < stride; ++k) {
            const int at = int(((start + qint64(i) * stride + k) % RingSize + RingSize) % RingSize);
            sample += 0.5f * (m_ring[0][at] + m_ring[1][at]);
        }
        m_real[m_reversed[i]] = sample / float(stride) * m_window[i];
        m_imag[m_reversed[i]] = 0.f;
    }
    for (int size = 2; size <= FftSize; size <<= 1) {
        const int half = size / 2, step = FftSize / size;
        for (int base = 0; base < FftSize; base += size) {
            for (int k = 0; k < half; ++k) {
                const float c = m_cos[k * step], s = -m_sin[k * step];
                const int a = base + k, b = a + half;
                const float re = m_real[b] * c - m_imag[b] * s;
                const float im = m_real[b] * s + m_imag[b] * c;
                m_real[b] = m_real[a] - re;
                m_imag[b] = m_imag[a] - im;
                m_real[a] += re;
                m_imag[a] += im;
            }
        }
    }
    for (int i = 0; i < FftSize / 2; ++i)
        magnitude[size_t(i)] = std::sqrt(m_real[i] * m_real[i] + m_imag[i] * m_imag[i]) / (FftSize / 4);
}

const AudioFrame &Analyzer::analyze(double dt) {
    dt = std::min(std::max(dt, 1e-4), 0.25);
    m_time += dt;

    // Waveform: the newest 1024 samples halved to 512 points, started on a
    // rising zero crossing so a steady tone stands still instead of crawling.
    constexpr int Span = AudioFrame::WaveSize * 2, Search = 512;
    const qint64 newest = m_written - Span - Search;
    auto sample = [&](int channel, qint64 index) {
        return m_ring[channel][int((index % RingSize + RingSize) % RingSize)];
    };
    int offset = Search;
    for (int i = Search - 1; i > 0; --i) {
        const float before = sample(0, newest + i - 1) + sample(1, newest + i - 1);
        const float after = sample(0, newest + i) + sample(1, newest + i);
        if (before < 0 && after >= 0) {
            offset = i;
            break;
        }
    }
    float peak = 0;
    for (int i = 0; i < Span + Search; ++i)
        peak = std::max(peak, std::max(std::abs(sample(0, newest + i)), std::abs(sample(1, newest + i))));

    // Automatic gain: follow the loudest recent peak, quickly up and slowly
    // down, so quiet and loud sources fill the screen alike.
    if (peak > m_level)
        m_level += (peak - m_level) * std::min(1.0, dt / 0.05);
    else
        m_level *= std::exp(-dt / 6.0);
    m_level = std::max(m_level, 1e-4);
    const double gain = m_sensitivity * 0.7 / std::max(m_level, 0.004);

    m_quietFor = peak > 2e-4 ? 0 : m_quietFor + dt;
    m_frame.silent = m_quietFor > 0.75;

    for (int i = 0; i < AudioFrame::WaveSize; ++i) {
        const qint64 at = newest + offset + i * 2;
        const float left = 0.5f * (sample(0, at) + sample(0, at + 1));
        const float right = 0.5f * (sample(1, at) + sample(1, at + 1));
        m_frame.wave[1][i] = float(left * gain);
        m_frame.wave[2][i] = float(right * gain);
        m_frame.wave[0][i] = float(0.5 * (left + right) * gain);
    }

    transform(1, m_magnitude);
    // The bottom two narrow ranges are 40 and 140 Hz wide, less than the
    // usual 43 ms of sound can tell apart, so they get twice as long a
    // listen. Everything faster than a bass line keeps the short one.
    transform(2, m_lowMagnitude);
    const double binWidth = double(m_rate) / FftSize;

    // Spectrum: log-spaced bands, tilted up 3 dB an octave (music falls off
    // that fast), in decibels over a 34 dB window, with a quick rise and a
    // slower fall so bars feel alive without flickering.
    const double fall = std::exp(-dt * 9.0);
    for (int band = 0; band < AudioFrame::SpectrumSize; ++band) {
        const double f0 = SpectrumLow * std::pow(SpectrumHigh / SpectrumLow, double(band) / AudioFrame::SpectrumSize);
        const double f1 = SpectrumLow * std::pow(SpectrumHigh / SpectrumLow, double(band + 1) / AudioFrame::SpectrumSize);
        const double b0 = f0 / binWidth, b1 = f1 / binWidth;
        double magnitude = 0;
        if (b1 - b0 < 1.0) {
            const double center = 0.5 * (b0 + b1);
            const int low = std::min(int(center), FftSize / 2 - 2);
            const double t = center - low;
            magnitude = m_magnitude[low] * (1 - t) + m_magnitude[low + 1] * t;
        } else {
            for (int bin = int(b0); bin < std::min(int(std::ceil(b1)), FftSize / 2); ++bin)
                magnitude = std::max(magnitude, double(m_magnitude[bin]));
        }
        const double center = std::sqrt(f0 * f1);
        const double tilt = std::min(18.0, std::max(-4.0, 3.0 * std::log2(center / 120.0)));
        const double decibels = 20 * std::log10(magnitude * gain + 1e-9) + tilt;
        const float value = float(std::min(1.0, std::max(0.0, (decibels + 38.0) / 34.0)));
        float &shown = m_frame.spectrum[band];
        shown = value > shown ? shown + (value - shown) * 0.7f : std::max(value, float(shown * fall));
    }

    // Bass, mid and treble relative to the track's own long-running average,
    // and then the six narrow ranges the same way.
    double levels[3 + AudioFrame::Bands];
    for (int range = 0; range < 3 + AudioFrame::Bands; ++range) {
        const double from = range < 3 ? BandEdges[range] : BandTable[range - 3].low;
        const double to = range < 3 ? BandEdges[range + 1] : BandTable[range - 3].high;
        const bool fine = range >= 3 && to <= 250;
        const std::vector<float> &magnitude = fine ? m_lowMagnitude : m_magnitude;
        const double width = fine ? binWidth / 2 : binWidth;
        double sum = 0;
        const int first = std::max(1, int(from / width));
        const int last = std::min(FftSize / 2, std::max(first + 1, int(to / width)));
        for (int bin = first; bin < last; ++bin)
            sum += magnitude[size_t(bin)];
        const double rate = rateFor(sum > m_average[range] ? 0.2 : 0.5, dt);
        m_average[range] = m_average[range] * rate + sum * (1 - rate);
        // The long average settles fast at the start of a song, then drifts.
        const double longRate = rateFor(m_time < 1.5 ? 0.9 : 0.992, dt);
        m_longAverage[range] = m_longAverage[range] * longRate + sum * (1 - longRate);
        const bool audible = m_longAverage[range] > 1e-5 && !m_frame.silent;
        levels[range] = std::min(audible ? m_sensitivity * sum / m_longAverage[range] : 0, 8.0);
        const double smooth = std::min(audible ? m_sensitivity * m_average[range] / m_longAverage[range] : 0, 8.0);
        if (range < 3) {
            (range == 0 ? m_frame.bassAtt : range == 1 ? m_frame.midAtt : m_frame.trebAtt) = float(smooth);
        } else {
            m_frame.band[range - 3] = float(levels[range]);
            m_frame.bandAtt[range - 3] = float(smooth);
        }
    }
    m_frame.bass = float(std::min(levels[0], 8.0));
    m_frame.mid = float(std::min(levels[1], 8.0));
    m_frame.treb = float(std::min(levels[2], 8.0));
    m_frame.vol = (m_frame.bass + m_frame.mid + m_frame.treb) / 3;
    m_frame.volAtt = (m_frame.bassAtt + m_frame.midAtt + m_frame.trebAtt) / 3;

    // A beat is bass that jumps well clear of where it has just been.
    m_sinceBeat += dt;
    const double bass = levels[0] / std::max(m_sensitivity, 1e-6);
    const double smoothBass = m_frame.bassAtt / std::max(m_sensitivity, 1e-6);
    m_frame.beat *= float(std::exp(-dt * 5.0));
    if (!m_frame.silent && m_sinceBeat > 0.22 && bass > 1.25 && bass > smoothBass * 1.12 && bass > m_lastBass) {
        m_frame.beat = 1;
        ++m_frame.beats;
        m_sinceBeat = 0;
    }
    m_lastBass = bass;
    return m_frame;
}

void synthesize(float *out, int frames, qint64 startFrame, int rate) {
    const double beatLength = 0.5; // 120 BPM
    static const double chord[4][3] = {{110, 164.8, 220}, {87.3, 130.8, 174.6}, {130.8, 196, 261.6}, {98, 146.8, 196}};
    for (int i = 0; i < frames; ++i) {
        const qint64 n = startFrame + i;
        const double t = double(n) / rate;
        const double inBeat = std::fmod(t, beatLength);
        const int beat = int(t / beatLength);
        // Kick: a sine that drops from 150 to 45 Hz as it dies.
        const double kickPhase = 2 * Pi * (45 * inBeat + (150 - 45) * (1 - std::exp(-inBeat * 30)) / 30);
        const double kick = std::sin(kickPhase) * std::exp(-inBeat * 9) * 0.8;
        // Hat: hashed noise on the off-beat.
        const double off = std::fmod(t + beatLength / 2, beatLength);
        uint32_t h = uint32_t(n) * 2654435761u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        const double hat = (double(h & 0xFFFF) / 32768.0 - 1.0) * std::exp(-off * 45) * 0.25;
        // Pad and a wandering lead, panned a little apart.
        const double *notes = chord[(beat / 4) % 4];
        double pad = 0;
        for (int k = 0; k < 3; ++k)
            pad += std::sin(2 * Pi * notes[k] * t + k) * 0.09;
        const double leadNote = notes[(beat + (beat / 2) % 3) % 3] * 4;
        const double lead = std::sin(2 * Pi * leadNote * t + 2 * std::sin(2 * Pi * 5 * t)) * 0.07 *
                            (0.5 + 0.5 * std::sin(2 * Pi * t / 4));
        out[i * 2] = float(kick + hat * 0.7 + pad + lead * 0.4);
        out[i * 2 + 1] = float(kick + hat + pad * 0.9 + lead);
    }
}
