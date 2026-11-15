// Listening on Linux: through PipeWire's pw-cat, with pw-dump to say what
// there is to listen to. PipeWire lets a recorder tap a playing stream, which
// is how one application is heard without rerouting it.
#include "audiocapture.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <cstring>
#include <vector>

#include "audio.h"

namespace {

// How Trance's own recorder is named in PipeWire, so it can be told apart
// from the things worth listening to.
const char *const OwnNode = "trance";

} // namespace

struct AudioCapture::Platform {
    explicit Platform(AudioCapture *capture)
        : q(capture), program(QStandardPaths::findExecutable("pw-cat")),
          dumpProgram(QStandardPaths::findExecutable("pw-dump")) {}

    void capture(const QStringList &target);
    void read();
    void dumped();

    AudioCapture *q;
    QString program, dumpProgram;
    QProcess process, dump;
    QByteArray pending;
    bool resolving = false; // the listing in progress is to find the application to tap
};

void AudioCapture::setUp() {
    m_platform = new Platform(this);
    Platform *p = m_platform;
    connect(&p->process, &QProcess::readyReadStandardOutput, this, [p] { p->read(); });
    connect(&p->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_wanted || error != QProcess::FailedToStart)
            return;
        setError(tr("Could not start pw-cat, which Trance listens through."));
        m_retry.start();
    });
    connect(&p->process, &QProcess::finished, this, [this, p] {
        lost(QString::fromUtf8(p->process.readAllStandardError()).trimmed().section('\n', -1));
    });
    connect(&p->dump, &QProcess::finished, this, [p] { p->dumped(); });
    connect(&p->dump, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && p->resolving) {
            p->resolving = false;
            setError(tr("pw-dump was not found, so Trance can't find %1.").arg(m_source.mid(4)));
        }
    });
}

void AudioCapture::tearDown() {
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
        if (p.dump.state() == QProcess::NotRunning)
            p.dump.start(p.dumpProgram.isEmpty() ? QString("pw-dump") : p.dumpProgram, {});
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
    if (p.process.state() != QProcess::NotRunning) {
        p.process.kill();
        p.process.waitForFinished(1000);
    }
    p.pending.clear();
}

// Starts pw-cat. The target is what kind of thing to record ("sink" for an
// output's monitor, "source", or "stream" for an application) and, unless
// the default device is meant, which one.
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
        // If the chosen thing goes away, stop rather than quietly listen to
        // something else; the retry finds it again when it is back.
        properties.append(", node.dont-reconnect = true");
    }
    arguments << "-P" << "{ " + properties + " }" << "-";
    pending.clear();
    process.start(program, arguments);
}

void AudioCapture::refreshSources() {
    Platform &p = *m_platform;
    if (p.dump.state() == QProcess::NotRunning && !p.dumpProgram.isEmpty())
        p.dump.start(p.dumpProgram, {});
}

void AudioCapture::Platform::dumped() {
    const QList<Node> nodes = parseNodes(dump.readAllStandardOutput());
    q->listed(nodes);
    if (!resolving || !q->m_wanted)
        return;
    resolving = false;
    const QString wanted = q->m_source.mid(4);
    for (const Node &node : nodes) {
        if (node.kind == Node::Application && node.application.compare(wanted, Qt::CaseInsensitive) == 0 &&
            node.serial >= 0) {
            capture({"stream", QString::number(node.serial)});
            return;
        }
    }
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
    pending.remove(0, frames * qsizetype(2 * sizeof(float)));
    q->heard(samples.data(), int(frames));
}
