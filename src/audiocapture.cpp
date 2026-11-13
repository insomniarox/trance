#include "audiocapture.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <cstring>
#include <vector>

#include "audio.h"

namespace {

// How Trance's own recorder is named in PipeWire, so it can be told apart
// from the things worth listening to.
const char *const OwnNode = "trance";

} // namespace

AudioCapture::AudioCapture(Analyzer *analyzer, QObject *parent, const QString &program)
    : QObject(parent), m_analyzer(analyzer),
      m_program(program.isEmpty() ? QStandardPaths::findExecutable("pw-cat") : program),
      m_dumpProgram(QStandardPaths::findExecutable("pw-dump")) {
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &AudioCapture::launch);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AudioCapture::read);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_wanted || error != QProcess::FailedToStart)
            return;
        setError(tr("Could not start pw-cat, which Trance listens through."));
        m_retry.start();
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        if (!m_wanted)
            return;
        // PipeWire restarting, the device going away, or the application
        // that was being listened to closing its sound: say so and keep
        // trying, so the picture comes back with the sound.
        const QString detail = QString::fromUtf8(m_process.readAllStandardError()).trimmed().section('\n', -1);
        if (m_source.startsWith("app:"))
            setError(tr("Waiting for %1 to play something.").arg(m_source.mid(4)));
        else
            setError(detail.isEmpty() ? tr("Audio capture stopped. Trying again…")
                                      : tr("Audio capture stopped: %1").arg(detail));
        m_retry.start();
    });
    connect(&m_dump, &QProcess::finished, this, &AudioCapture::dumped);
    connect(&m_dump, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_resolving) {
            m_resolving = false;
            setError(tr("pw-dump was not found, so Trance can't find %1.").arg(m_source.mid(4)));
        }
    });
}

AudioCapture::~AudioCapture() {
    stop();
    if (m_dump.state() != QProcess::NotRunning) {
        m_dump.kill();
        m_dump.waitForFinished(500);
    }
}

bool AudioCapture::isSource(const QString &source) {
    return source == "system" || source == "microphone" || (source.startsWith("app:") && source.size() > 4) ||
           (source.startsWith("output:") && source.size() > 7) || (source.startsWith("input:") && source.size() > 6);
}

void AudioCapture::start(const QString &source) {
    stop();
    m_source = isSource(source) ? source : QString("system");
    m_wanted = true;
    launch();
}

void AudioCapture::stop() {
    m_wanted = false;
    m_resolving = false;
    m_retry.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_pending.clear();
}

void AudioCapture::launch() {
    if (!m_wanted || m_process.state() != QProcess::NotRunning)
        return;
    if (m_program.isEmpty()) {
        setError(tr("pw-cat was not found. Install pipewire so Trance can hear the music."));
        return;
    }
    if (m_source.startsWith("app:")) {
        // An application's stream comes and goes with its sound and gets a
        // new number each time, so it is looked up by name at the moment of
        // listening. dumped() carries on from here.
        m_resolving = true;
        if (m_dump.state() == QProcess::NotRunning)
            m_dump.start(m_dumpProgram.isEmpty() ? QString("pw-dump") : m_dumpProgram, {});
        return;
    }
    if (m_source.startsWith("output:"))
        capture({"sink", m_source.mid(7)});
    else if (m_source.startsWith("input:"))
        capture({"source", m_source.mid(6)});
    else
        capture({m_source == "microphone" ? "source" : "sink"});
}

// Starts pw-cat. The target is what kind of thing to record ("sink" for an
// output's monitor, "source", or "stream" for an application) and, unless
// the default device is meant, which one.
void AudioCapture::capture(const QStringList &target) {
    // Raw 32-bit float stereo on stdout. Recording a sink (rather than a
    // source) is what makes this "what the speakers play"; leaving the
    // target unset lets PipeWire follow the default device when it changes.
    QStringList arguments{"--record", "--raw", "--format", "f32", "--rate", QString::number(m_analyzer->sampleRate()),
                          "--channels", "2", "--latency", "20ms"};
    QString properties = QString("node.name = %1, media.name = \"Trance\"").arg(OwnNode);
    if (target.first() == "sink")
        properties.prepend("stream.capture.sink = true, ");
    if (target.size() > 1) {
        arguments << "--target" << target.at(1);
        // If the chosen thing goes away, stop rather than quietly listen to
        // something else; the retry finds it again when it is back.
        properties.append(", node.dont-reconnect = true");
    }
    arguments << "-P" << "{ " + properties + " }" << "-";
    m_pending.clear();
    m_process.start(m_program, arguments);
}

void AudioCapture::refreshSources() {
    if (m_dump.state() == QProcess::NotRunning && !m_dumpProgram.isEmpty())
        m_dump.start(m_dumpProgram, {});
}

void AudioCapture::dumped() {
    const QList<Node> nodes = parseNodes(m_dump.readAllStandardOutput());
    const QVariantList sources = describe(nodes, m_source);
    if (sources != m_sources) {
        m_sources = sources;
        emit sourcesChanged();
    }
    if (!m_resolving || !m_wanted)
        return;
    m_resolving = false;
    const QString wanted = m_source.mid(4);
    for (const Node &node : nodes) {
        if (node.mediaClass == "Stream/Output/Audio" && node.application.compare(wanted, Qt::CaseInsensitive) == 0 &&
            node.serial >= 0) {
            capture({"stream", QString::number(node.serial)});
            return;
        }
    }
    setError(tr("Waiting for %1 to play something.").arg(wanted));
    m_retry.start();
}

QList<AudioCapture::Node> AudioCapture::parseNodes(const QByteArray &dump) {
    // A sound card, as far as naming and plugging go. One card shows up as
    // several nodes, one for each of its sockets.
    struct Route {
        QString direction;
        QList<int> sockets;
        bool unplugged = false;
    };
    struct Card {
        QString description;
        bool builtIn = false;
        QList<Route> routes;
    };
    QHash<int, Card> cards;
    QHash<int, int> cardOf, socketOf; // by position in the list of nodes

    QList<Node> nodes;
    const QJsonArray objects = QJsonDocument::fromJson(dump).array();
    for (const auto &value : objects) {
        const QJsonObject object = value.toObject();
        const QJsonObject info = object.value("info").toObject();
        const QJsonObject props = info.value("props").toObject();
        const QString type = object.value("type").toString();
        if (type == "PipeWire:Interface:Device") {
            Card card;
            card.description = props.value("device.description").toString();
            card.builtIn = props.value("device.bus").toString() == "pci" ||
                           props.value("device.form-factor").toString() == "internal";
            for (const auto &entry : info.value("params").toObject().value("EnumRoute").toArray()) {
                const QJsonObject route = entry.toObject();
                Route parsed;
                parsed.direction = route.value("direction").toString();
                parsed.unplugged = route.value("available").toString() == "no";
                for (const auto &socket : route.value("devices").toArray())
                    parsed.sockets << socket.toInt(-1);
                card.routes << parsed;
            }
            cards.insert(object.value("id").toInt(-1), card);
            continue;
        }
        if (type != "PipeWire:Interface:Node")
            continue;
        Node node;
        node.mediaClass = props.value("media.class").toString();
        if (node.mediaClass != "Stream/Output/Audio" && node.mediaClass != "Audio/Sink" &&
            node.mediaClass != "Audio/Source")
            continue;
        node.name = props.value("node.name").toString();
        if (node.name == OwnNode)
            continue;
        node.application = props.value("application.name").toString();
        if (node.application.isEmpty())
            node.application = props.value("application.process.binary").toString();
        if (node.application.isEmpty())
            node.application = node.name;
        node.description = props.value("node.description").toString();
        if (node.description.isEmpty())
            node.description = props.value("node.nick").toString();
        if (node.description.isEmpty())
            node.description = node.name;
        node.label = node.description;
        node.serial = qint64(props.value("object.serial").toDouble(-1));
        if (props.contains("device.id")) {
            cardOf.insert(int(nodes.size()), props.value("device.id").toInt(-1));
            socketOf.insert(int(nodes.size()), props.value("card.profile.device").toInt(-1));
        }
        nodes.append(node);
    }

    // The cards may come after their nodes in the dump, so the two are put
    // together only now.
    for (int i = 0; i < nodes.size(); ++i) {
        Node &node = nodes[i];
        if (node.mediaClass == "Stream/Output/Audio" || !cards.contains(cardOf.value(i, -1)))
            continue;
        const Card &card = cards[cardOf[i]];
        // Unplugged when every way sound could get in or out of this socket
        // says so. A card that can't tell ("unknown") counts as plugged.
        const QString direction = node.mediaClass == "Audio/Sink" ? "Output" : "Input";
        int ways = 0, unplugged = 0;
        for (const Route &route : card.routes) {
            if (route.direction != direction || !route.sockets.contains(socketOf.value(i, -1)))
                continue;
            ++ways;
            unplugged += route.unplugged;
        }
        node.available = ways == 0 || unplugged < ways;
        // PipeWire names a socket "<card> <socket>". For the computer's own
        // sound card the card's name is a chipset nobody knows it by, so the
        // socket is the name ("Speaker"). For anything plugged in it is the
        // other way round: the device's name says it, unless it has several
        // sockets of the kind.
        int siblings = 0;
        for (int j = 0; j < nodes.size(); ++j)
            siblings += cardOf.value(j, -2) == cardOf[i] && nodes[j].mediaClass == node.mediaClass;
        if (card.description.isEmpty())
            continue;
        if (card.builtIn) {
            if (node.description.startsWith(card.description + ' '))
                node.label = node.description.mid(card.description.size() + 1).trimmed();
        } else if (siblings == 1) {
            node.label = card.description;
        }
        if (node.label.isEmpty())
            node.label = node.description;
    }
    // Two devices that would be called the same keep their whole names.
    QStringList shortNames;
    for (const Node &node : nodes)
        shortNames << node.label;
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].mediaClass == "Stream/Output/Audio")
            continue;
        for (int j = 0; j < nodes.size(); ++j)
            if (j != i && nodes[j].mediaClass == nodes[i].mediaClass && shortNames[j] == shortNames[i])
                nodes[i].label = nodes[i].description;
    }
    return nodes;
}

QVariantList AudioCapture::describe(const QList<Node> &nodes, const QString &current) {
    const auto entry = [](const QString &id, const QString &label, const QString &kind, const QString &section) {
        return QVariantMap{{"id", id}, {"label", label}, {"kind", kind}, {"section", section}};
    };
    QVariantList list{entry("system", tr("Everything that's playing"), "all", {})};

    const QString applications = tr("Applications");
    QStringList seen;
    for (const Node &node : nodes) {
        if (node.mediaClass != "Stream/Output/Audio" || node.application.isEmpty() ||
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
    const auto devices = [&](const QString &mediaClass, const QString &prefix, const QString &kind,
                             const QString &section) {
        int usable = 0;
        for (const Node &node : nodes)
            usable += node.mediaClass == mediaClass && node.available;
        bool found = !current.startsWith(prefix);
        for (const Node &node : nodes) {
            if (node.mediaClass != mediaClass)
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
    devices("Audio/Sink", "output:", "output", tr("Outputs"));
    // The microphone goes in before the inputs it is one of, so its place is
    // kept open here and filled once they have been counted.
    const qsizetype place = list.size();
    const int inputs = devices("Audio/Source", "input:", "input", tr("Inputs"));
    list.insert(place, entry("microphone", inputs > 1 ? tr("Default microphone") : tr("Microphone"), "microphone",
                             tr("Inputs")));
    return list;
}

void AudioCapture::read() {
    // Sound that arrives while stopping is dropped: nobody is listening, and
    // at exit the analyzer may already be gone.
    if (!m_wanted) {
        m_process.readAllStandardOutput();
        return;
    }
    m_pending += m_process.readAllStandardOutput();
    const qsizetype frames = m_pending.size() / qsizetype(2 * sizeof(float));
    if (frames == 0)
        return;
    static thread_local std::vector<float> samples;
    samples.resize(size_t(frames) * 2);
    std::memcpy(samples.data(), m_pending.constData(), samples.size() * sizeof(float));
    m_pending.remove(0, frames * qsizetype(2 * sizeof(float)));
    m_analyzer->push(samples.data(), int(frames));
    setError({});
}

void AudioCapture::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}
