#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

class Analyzer;

// Listens through PipeWire's pw-cat and feeds what it hears to the analyzer.
//
// What it listens to is the user's choice: everything the speakers play, one
// application's own sound, one output or input device, or the microphone.
// Listening to an application takes no rerouting: PipeWire lets a recorder
// tap a playing stream, which goes on to the speakers as before.
class AudioCapture : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(Analyzer *analyzer, QObject *parent = nullptr, const QString &program = {});
    ~AudioCapture() override;

    // "system" (what is playing), "microphone" (the default input),
    // "app:Spotify" (that application only), or "output:<node>" /
    // "input:<node>" for one device by its PipeWire node name.
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
    // Asks PipeWire again; sourcesChanged() follows if the answer differs.
    void refreshSources();

    struct Node {
        QString mediaClass, application, name, description;
        // What to call a device in a list: "Speaker" rather than the sound
        // card's whole name in front of it.
        QString label;
        qint64 serial = -1;
        // False for a socket with nothing in it (an HDMI port without a
        // screen, a headphone jack without headphones).
        bool available = true;
    };
    // The audio nodes in pw-dump's output, without Trance's own.
    static QList<Node> parseNodes(const QByteArray &dump);
    static QVariantList describe(const QList<Node> &nodes, const QString &current);

signals:
    void errorChanged();
    void sourcesChanged();

private:
    void launch();
    void capture(const QStringList &target);
    void read();
    void setError(const QString &error);
    void dumped();

    Analyzer *m_analyzer;
    QString m_program, m_dumpProgram, m_source;
    QProcess m_process, m_dump;
    QByteArray m_pending;
    QTimer m_retry;
    QString m_error;
    QVariantList m_sources;
    bool m_wanted = false;
    bool m_resolving = false; // the listing in progress is to find the application to tap
};
