#include "audiocapture.h"

#include <algorithm>

#include "audio.h"

AudioCapture::AudioCapture(Analyzer *analyzer, QObject *parent) : QObject(parent), m_analyzer(analyzer) {
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &AudioCapture::launch);
    setUp();
}

AudioCapture::~AudioCapture() {
    stop();
    tearDown();
}

bool AudioCapture::isSource(const QString &source) {
    return source == "system" || source == "microphone" || (source.startsWith("app:") && source.size() > 4) ||
           (source.startsWith("output:") && source.size() > 7) || (source.startsWith("input:") && source.size() > 6);
}

void AudioCapture::start(const QString &source) {
    stop();
    m_source = isSource(source) ? source : QString("system");
    m_wanted = true;
    m_retriedSoon = false;
    launch();
}

void AudioCapture::stop() {
    m_wanted = false;
    m_retry.stop();
    close();
}

void AudioCapture::launch() {
    if (m_wanted)
        open();
}

void AudioCapture::heard(const float *interleavedStereo, int frames) {
    // Sound that arrives while stopping is dropped: nobody is listening, and
    // at exit the analyzer may already be gone.
    if (!m_wanted)
        return;
    m_analyzer->push(interleavedStereo, frames);
    if (m_retriedSoon && std::any_of(interleavedStereo, interleavedStereo + 2 * frames,
                                     [](float sample) { return sample != 0.f; }))
        m_retriedSoon = false;
    setError({});
}

// The sound system restarting, the device going away, or the application
// that was being listened to closing its sound: say so and keep trying, so
// the picture comes back with the sound.
void AudioCapture::lost(const QString &detail) {
    if (!m_wanted)
        return;
    // An application may end one stream and open the next at once, as a
    // browser can between tracks: looked for again straight away the first
    // time, and only then said to be waited for.
    if (m_source.startsWith("app:") && !m_retriedSoon) {
        m_retriedSoon = true;
        m_retry.start(300);
        return;
    }
    m_retry.setInterval(2000);
    if (m_source.startsWith("app:"))
        setError(tr("Waiting for %1 to play something.").arg(m_source.mid(4)));
    else
        setError(detail.isEmpty() ? tr("Audio capture stopped. Trying again…")
                                  : tr("Audio capture stopped: %1").arg(detail));
    m_retry.start();
}

void AudioCapture::listed(const QList<Node> &nodes) {
    const QVariantList sources = describe(nodes, m_source);
    if (sources != m_sources) {
        m_sources = sources;
        emit sourcesChanged();
    }
}

QVariantList AudioCapture::describe(const QList<Node> &nodes, const QString &current) {
    const auto entry = [](const QString &id, const QString &label, const QString &kind, const QString &section) {
        return QVariantMap{{"id", id}, {"label", label}, {"kind", kind}, {"section", section}};
    };
    QVariantList list{entry("system", tr("Everything that's playing"), "all", {})};

    const QString applications = tr("Applications");
    QStringList seen;
    for (const Node &node : nodes) {
        if (node.kind != Node::Application || node.application.isEmpty() ||
            seen.contains(node.application, Qt::CaseInsensitive))
            continue;
        seen << node.application;
        list << entry("app:" + node.application, node.application, "app", applications);
    }
    // The chosen application stays on the list while it is silent, so the
    // choice still shows and can be seen to be waiting.
    if (current.startsWith("app:") && !seen.contains(current.mid(4), Qt::CaseInsensitive))
        list << entry(current, tr("%1 (not playing)").arg(current.mid(4)), "app", applications);

    // Devices of one kind. One on its own is what "everything" (or the
    // microphone) already listens to, so only a real choice is listed. The
    // chosen device always is, plugged in or not.
    const auto devices = [&](Node::Kind which, const QString &prefix, const QString &kind, const QString &section) {
        int usable = 0;
        for (const Node &node : nodes)
            usable += node.kind == which && node.available;
        bool found = !current.startsWith(prefix);
        for (const Node &node : nodes) {
            if (node.kind != which)
                continue;
            const bool chosen = current == prefix + node.name;
            found = found || chosen;
            if (chosen && !node.available)
                list << entry(current, tr("%1 (not connected)").arg(node.label), kind, section);
            else if (chosen || (node.available && usable > 1))
                list << entry(prefix + node.name, node.label, kind, section);
        }
        if (!found)
            list << entry(current, tr("%1 (not connected)").arg(current.mid(prefix.size())), kind, section);
        return usable;
    };
    devices(Node::Output, "output:", "output", tr("Outputs"));
    // The microphone goes in before the inputs it is one of, so its place is
    // kept open here and filled once they have been counted.
    const qsizetype place = list.size();
    const int inputs = devices(Node::Input, "input:", "input", tr("Inputs"));
    list.insert(place, entry("microphone", inputs > 1 ? tr("Default microphone") : tr("Microphone"), "microphone",
                             tr("Inputs")));
    return list;
}

void AudioCapture::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}
