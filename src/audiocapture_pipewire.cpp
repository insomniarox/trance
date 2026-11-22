// Listening on Linux: through PipeWire's pw-cat, with pw-dump to say what
// there is to listen to. PipeWire lets a recorder tap a playing stream, which
// is how one application is heard without rerouting it. The demo track is
// played by another pw-cat and tapped the same way.
#include "audiocapture.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <vector>

#include "audio.h"

namespace {

// How Trance's own recorder is named in PipeWire, so it can be told apart
// from the things worth listening to.
const char *const OwnNode = "trance";
// And the stream the demo track plays in, which is not on the list either.
const char *const DemoNode = "trance-demo";

// The stream to tap for an application: of those it has, one that is
// playing, and of several the newest. With onlyRunning, none unless one
// plays, and not besides. -1 for none.
qint64 streamOf(const QList<AudioCapture::Node> &nodes, const QString &application, bool onlyRunning,
                qint64 besides = -1) {
    qint64 best = -1;
    bool bestRunning = false;
    for (const AudioCapture::Node &node : nodes) {
        if (node.kind != AudioCapture::Node::Application || node.serial < 0 ||
            node.application.compare(application, Qt::CaseInsensitive) != 0 || (onlyRunning && !node.running) ||
            node.serial == besides)
            continue;
        if (best < 0 || (node.running && !bestRunning) || (node.running == bestRunning && node.serial > best)) {
            best = node.serial;
            bestRunning = node.running;
        }
    }
    return best;
}

} // namespace

struct AudioCapture::Platform {
    explicit Platform(AudioCapture *capture)
        : q(capture), program(QStandardPaths::findExecutable("pw-cat")),
          dumpProgram(QStandardPaths::findExecutable("pw-dump")) {}

    void capture(const QStringList &target);
    void read();
    // Starts the demo track playing, unless it is.
    void play();
    void startDump();
    void dumped();
    // While an application is quiet, whether another of its streams plays.
    void lookAround();

    AudioCapture *q;
    QString program, dumpProgram;
    QProcess process, dump;
    // The demo track's player, and the track, kept while it plays.
    QProcess player;
    QByteArray demo;
    // A pw-dump that hangs, as on a PipeWire that has stopped answering, is
    // stopped after this long, rather than leaving the listening waiting.
    QTimer dumpWatch;
    QByteArray pending;
    bool resolving = false; // the listing in progress is to find the application to tap
    bool lookingAround = false; // it is to see whether another of the application's streams plays
    bool switching = false; // pw-cat is being stopped to tap another stream
    qint64 tapped = -1;     // the stream being tapped
    // How long the tapped stream has been quiet, and how often another is
    // looked for then: less often the longer it lasts, as a paused player
    // can for hours.
    QElapsedTimer sounded;
    QTimer looking;
};

void AudioCapture::setUp() {
    m_platform = new Platform(this);
    Platform *p = m_platform;
    connect(&p->process, &QProcess::readyReadStandardOutput, this, [p] { p->read(); });
    connect(&p->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_wanted || error != QProcess::FailedToStart)
            return;
        setError(tr("Could not start pw-cat, which Trance listens through."));
        m_retry.start(2000);
    });
    connect(&p->process, &QProcess::finished, this, [this, p] {
        p->looking.stop();
        if (p->switching)
            return;
        const QString said = QString::fromUtf8(p->process.readAllStandardError()).trimmed();
        // A target that isn't there is said in the line before the last,
        // which is "stream node 4294967295 unconnected".
        if (said.contains("target not found"))
            lost(tr("the device is not connected."));
        else
            lost(said.section('\n', -1));
    });
    // The player is kept a loop or so ahead of what it has played: pw-cat
    // takes it no faster than it plays, and the rest waits in the pipe.
    connect(&p->player, &QProcess::bytesWritten, this, [p] {
        if (p->player.bytesToWrite() < p->demo.size())
            p->player.write(p->demo);
    });
    // Without its player the demo's stream is gone, and so is what was being
    // listened to: the recorder is stopped too, and both are tried again.
    connect(&p->player, &QProcess::finished, this, [p] {
        if (p->process.state() != QProcess::NotRunning)
            p->process.kill();
    });
    connect(&p->dump, &QProcess::finished, this, [p] { p->dumped(); });
    p->dumpWatch.setSingleShot(true);
    p->dumpWatch.setInterval(5000);
    connect(&p->dumpWatch, &QTimer::timeout, this, [p] { p->dump.kill(); });
    p->looking.setSingleShot(true);
    connect(&p->looking, &QTimer::timeout, this, [p] { p->lookAround(); });
    connect(&p->dump, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && p->resolving) {
            p->resolving = false;
            setError(tr("pw-dump was not found, so Trance can't find %1.").arg(m_source.mid(4)));
        }
    });
}

void AudioCapture::tearDown() {
    // A listing cut short here has nobody left to tell.
    disconnect(&m_platform->dump, nullptr, this, nullptr);
    m_platform->dumpWatch.stop();
    if (m_platform->dump.state() != QProcess::NotRunning) {
        m_platform->dump.kill();
        m_platform->dump.waitForFinished(500);
    }
    delete m_platform;
    m_platform = nullptr;
}

void AudioCapture::open() {
    Platform &p = *m_platform;
    if (p.process.state() != QProcess::NotRunning)
        return;
    if (p.program.isEmpty()) {
        setError(tr("pw-cat was not found. Install pipewire so Trance can hear the music."));
        return;
    }
    if (m_source.startsWith("app:")) {
        // An application's stream comes and goes with its sound and gets a
        // new number each time, so it is looked up by name at the moment of
        // listening. dumped() carries on from here.
        p.resolving = true;
        p.startDump();
        return;
    }
    if (m_source == "demo") {
        p.play();
        p.capture({"demo", DemoNode});
        return;
    }
    if (m_source.startsWith("output:"))
        p.capture({"sink", m_source.mid(7)});
    else if (m_source.startsWith("input:"))
        p.capture({"source", m_source.mid(6)});
    else
        p.capture({m_source == "microphone" ? "source" : "sink"});
}

void AudioCapture::close() {
    Platform &p = *m_platform;
    p.resolving = false;
    p.lookingAround = false;
    p.looking.stop();
    p.tapped = -1;
    if (p.process.state() != QProcess::NotRunning) {
        p.process.kill();
        p.process.waitForFinished(1000);
    }
    if (p.player.state() != QProcess::NotRunning) {
        p.player.kill();
        p.player.waitForFinished(1000);
    }
    p.demo.clear();
    p.pending.clear();
}

// Starts pw-cat. The target is what kind of thing to record ("sink" for an
// output's monitor, "source", "stream" for an application, or "demo" for the
// demo track's stream) and, unless the default device is meant, which one.
void AudioCapture::Platform::capture(const QStringList &target) {
    // Raw 32-bit float stereo on stdout. Recording a sink (rather than a
    // source) is what makes this "what the speakers play"; leaving the
    // target unset lets PipeWire follow the default device when it changes.
    QStringList arguments{"--record", "--raw", "--format", "f32", "--rate",
                          QString::number(q->m_analyzer->sampleRate()), "--channels", "2", "--latency", "20ms"};
    QString properties = QString("node.name = %1, media.name = \"Trance\"").arg(OwnNode);
    if (target.first() == "sink")
        properties.prepend("stream.capture.sink = true, ");
    if (target.size() > 1) {
        arguments << "--target" << target.at(1);
        // If the chosen thing is not there, or goes away, stop rather than
        // quietly listen to the default instead: the retry finds it again
        // when it is back.
        properties.append(", node.dont-reconnect = true, node.dont-fallback = true");
    }
    tapped = target.first() == "stream" ? target.at(1).toLongLong() : -1;
    if (tapped >= 0) {
        // A stream changed to while looking around keeps the quiet it was
        // found in, and how often to look again: two that run in silence
        // are then changed between ever less often.
        if (!switching) {
            sounded.start();
            looking.setInterval(2000);
        }
        looking.start();
    }
    arguments << "-P" << "{ " + properties + " }" << "-";
    pending.clear();
    process.start(program, arguments);
}

void AudioCapture::Platform::play() {
    if (player.state() != QProcess::NotRunning)
        return;
    if (demo.isEmpty())
        demo = AudioCapture::demoTrack();
    player.start(program, {"--playback", "--raw", "--format", "s16", "--rate", QString::number(DemoRate),
                           "--channels", "2", "-P",
                           QString("{ node.name = %1, media.name = \"Demo track\", application.name = Trance, "
                                   "media.role = Music }")
                               .arg(DemoNode),
                           "-"});
    player.write(demo);
}

void AudioCapture::refreshSources() {
    Platform &p = *m_platform;
    if (!p.dumpProgram.isEmpty())
        p.startDump();
}

void AudioCapture::Platform::startDump() {
    if (dump.state() != QProcess::NotRunning)
        return;
    dump.start(dumpProgram.isEmpty() ? QString("pw-dump") : dumpProgram, {});
    dumpWatch.start();
}

void AudioCapture::Platform::lookAround() {
    if (tapped < 0 || process.state() == QProcess::NotRunning)
        return;
    if (sounded.elapsed() < 3000) {
        looking.start(2000);
        return;
    }
    lookingAround = true;
    startDump();
    looking.start(qMin(2 * looking.interval(), 10000));
}

void AudioCapture::Platform::dumped() {
    dumpWatch.stop();
    const bool around = lookingAround;
    lookingAround = false;
    // Stopped, failed or cut short, what it printed may be nothing or half
    // of the list, which is not to be taken for what there is.
    const QByteArray printed = dump.readAllStandardOutput();
    const bool answered = dump.exitStatus() == QProcess::NormalExit && dump.exitCode() == 0 && !printed.isEmpty();
    const QList<Node> nodes = answered ? parseNodes(printed) : QList<Node>();
    if (answered)
        q->listed(nodes);
    if (!q->m_wanted)
        return;
    if (around && answered && !resolving && tapped >= 0) {
        // Another of the application's streams is playing while the one
        // tapped is quiet: that one is listened to instead.
        const qint64 other = streamOf(nodes, q->m_source.mid(4), true, tapped);
        if (other >= 0) {
            switching = true;
            process.kill();
            process.waitForFinished(1000);
            capture({"stream", QString::number(other)});
            switching = false;
        }
        return;
    }
    if (!resolving)
        return;
    resolving = false;
    const qint64 stream = answered ? streamOf(nodes, q->m_source.mid(4), false) : -1;
    if (stream >= 0)
        capture({"stream", QString::number(stream)});
    else
        q->lost({});
}

bool AudioCapture::listNodes(QList<Node> *nodes, QString *error) {
    QProcess dump;
    dump.start("pw-dump", QStringList());
    if (!dump.waitForFinished(4000) || dump.exitStatus() != QProcess::NormalExit) {
        *error = "Could not ask PipeWire (pw-dump) what is playing.";
        return false;
    }
    *nodes = parseNodes(dump.readAllStandardOutput());
    return true;
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
        const QString mediaClass = props.value("media.class").toString();
        if (mediaClass == "Stream/Output/Audio")
            node.kind = Node::Application;
        else if (mediaClass == "Audio/Sink")
            node.kind = Node::Output;
        else if (mediaClass == "Audio/Source")
            node.kind = Node::Input;
        else
            continue;
        node.name = props.value("node.name").toString();
        if (node.name == OwnNode || node.name == DemoNode)
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
        node.running = info.value("state").toString() == "running";
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
        if (node.kind == Node::Application || !cards.contains(cardOf.value(i, -1)))
            continue;
        const Card &card = cards[cardOf[i]];
        // Unplugged when every way sound could get in or out of this socket
        // says so. A card that can't tell ("unknown") counts as plugged.
        const QString direction = node.kind == Node::Output ? "Output" : "Input";
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
            siblings += cardOf.value(j, -2) == cardOf[i] && nodes[j].kind == node.kind;
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
        if (nodes[i].kind == Node::Application)
            continue;
        for (int j = 0; j < nodes.size(); ++j)
            if (j != i && nodes[j].kind == nodes[i].kind && shortNames[j] == shortNames[i])
                nodes[i].label = nodes[i].description;
    }
    return nodes;
}

void AudioCapture::Platform::read() {
    // What arrives while stopping is taken out of the pipe and dropped.
    if (!q->m_wanted) {
        process.readAllStandardOutput();
        return;
    }
    pending += process.readAllStandardOutput();
    const qsizetype frames = pending.size() / qsizetype(2 * sizeof(float));
    if (frames == 0)
        return;
    static thread_local std::vector<float> samples;
    samples.resize(size_t(frames) * 2);
    std::memcpy(samples.data(), pending.constData(), samples.size() * sizeof(float));
    if (tapped >= 0 && std::any_of(samples.begin(), samples.end(), [](float sample) { return sample != 0.f; })) {
        sounded.restart();
        if (looking.interval() != 2000)
            looking.setInterval(2000);
    }
    pending.remove(0, frames * qsizetype(2 * sizeof(float)));
    q->heard(samples.data(), int(frames));
}
