#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

class Analyzer;

// Listens to the music and feeds what it hears to the analyzer.
//
// What it listens to is the user's choice: everything the speakers play, one
// application's own sound, one output or input device, or the microphone.
// Listening to an application takes no rerouting: the sound goes on to the
// speakers as before.
//
// How the listening is done differs by system, and so does the file it is
// in: audiocapture_pipewire.cpp on Linux. What the sources are called, what
// goes on the list of them, and trying again when the sound goes away are the
// same everywhere, and are in audiocapture.cpp.
class AudioCapture : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(Analyzer *analyzer, QObject *parent = nullptr);
    ~AudioCapture() override;

    // "system" (what is playing), "microphone" (the default input),
    // "app:Spotify" (that application only), or "output:<name>" /
    // "input:<name>" for one device by the name its system knows it by.
    void start(const QString &source);
    void stop();
    static bool isSource(const QString &source);
    // What is keeping the music from arriving; empty while it does.
    QString error() const { return m_error; }

    // What there is to listen to right now, each with an id (as start()
    // takes), a label, a kind (all, app, output, microphone or input) and
    // the heading it goes under in a list. Only what can be heard is on it:
    // a socket with nothing plugged in is left out, and so is a lone output
    // or input, which "everything" and "microphone" already mean.
    QVariantList sources() const { return m_sources; }
    // Asks the system again; sourcesChanged() follows if the answer differs.
    void refreshSources();

    // One thing that could be listened to, as its system reports it.
    struct Node {
        enum Kind { Application, Output, Input };
        Kind kind = Application;
        // The application a stream belongs to, the name a device is chosen
        // by, and the device's name in full.
        QString application, name, description;
        // What to call a device in a list: "Speaker" rather than the sound
        // card's whole name in front of it.
        QString label;
        qint64 serial = -1;
        // False for a socket with nothing in it (an HDMI port without a
        // screen, a headphone jack without headphones).
        bool available = true;
    };
    static QVariantList describe(const QList<Node> &nodes, const QString &current);
    // What there is right now, waited for rather than announced: for a
    // command, which has no window to keep open. False, with the reason,
    // when the system could not be asked.
    static bool listNodes(QList<Node> *nodes, QString *error);
    // The audio nodes in pw-dump's output, without Trance's own. PipeWire only.
    static QList<Node> parseNodes(const QByteArray &dump);

signals:
    void errorChanged();
    void sourcesChanged();

private:
    void launch();
    void setError(const QString &error);
    // For the system's side to report with: sound that arrived, the
    // listening having stopped (it is tried again), and what there is.
    void heard(const float *interleavedStereo, int frames);
    void lost(const QString &detail);
    void listed(const QList<Node> &nodes);

    // The system's side.
    struct Platform;
    void setUp();
    void tearDown();
    // Starts listening to m_source, unless it already is. heard() follows
    // for as long as it works, lost() when it stops.
    void open();
    void close();

    Analyzer *m_analyzer;
    Platform *m_platform = nullptr;
    QString m_source;
    QTimer m_retry;
    QString m_error;
    QVariantList m_sources;
    bool m_wanted = false;
};
