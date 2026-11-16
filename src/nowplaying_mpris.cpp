// What is playing on Linux: asked of the media players over MPRIS, on D-Bus.
#include "nowplaying.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <vector>

namespace {

const char *const PlayerPath = "/org/mpris/MediaPlayer2";
const char *const Properties = "org.freedesktop.DBus.Properties";
const char *const PlayerInterface = "org.mpris.MediaPlayer2.Player";

// Listens for players announcing a change and then asks them all.
class Mpris : public QObject {
    Q_OBJECT

public:
    Mpris(std::function<void(const QString &, const QString &)> found, QObject *parent);

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void refresh();
    void ask(const QStringList &players);
    // The asking in progress has its answer, or has none to give.
    void done();

    std::function<void(const QString &, const QString &)> m_found;
    QTimer m_debounce;
    QElapsedTimer m_since; // since the first change the debounce is waiting out
    QString m_player;      // whose title is showing
    int m_generation = 0;
    // One asking at a time: a change announced meanwhile is asked about
    // once it is over, rather than starting again, which a player that
    // keeps announcing would do for ever.
    bool m_asking = false, m_again = false;
};

Mpris::Mpris(std::function<void(const QString &, const QString &)> found, QObject *parent)
    : QObject(parent), m_found(std::move(found)) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &Mpris::refresh);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    // Any player announcing a change is a reason to look again.
    bus.connect(QString(), PlayerPath, Properties, "PropertiesChanged", this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    m_since.start();
    m_debounce.start();
}

// A new track is announced in several steps: asked about once they have
// stopped coming for a moment, or a second after the first if they don't.
void Mpris::propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &) {
    if (interface != QLatin1String(PlayerInterface))
        return;
    if (!m_debounce.isActive())
        m_since.start();
    if (m_since.elapsed() < 1000)
        m_debounce.start();
}

void Mpris::refresh() {
    if (m_asking) {
        m_again = true;
        return;
    }
    m_asking = true;
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *watcher = new QDBusPendingCallWatcher(bus.interface()->asyncCall("ListNames"), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isError()) {
            done();
            return;
        }
        QStringList players;
        for (const auto &name : reply.value())
            if (name.startsWith("org.mpris.MediaPlayer2."))
                players << name;
        ask(players);
    });
}

void Mpris::done() {
    m_asking = false;
    if (m_again) {
        m_again = false;
        refresh();
    }
}

// Asks every player what it is doing and takes one that is playing a track
// with a title: the one already shown if it still is, so two playing at
// once don't take turns, or else the first. A paused player keeps the last
// title on screen. The answer is taken as soon as no player yet to answer
// could change it, so one that is slow to answer holds up only what it
// could.
void Mpris::ask(const QStringList &players) {
    enum Answer { Waiting, Not, Playing };
    struct Round {
        std::vector<Answer> answers;
        std::vector<QString> titles, artists;
        bool over = false;
    };
    const int generation = ++m_generation;
    auto round = std::make_shared<Round>();
    round->answers.assign(size_t(players.size()), Waiting);
    round->titles.resize(size_t(players.size()));
    round->artists.resize(size_t(players.size()));
    if (players.isEmpty()) {
        done();
        return;
    }
    for (int index = 0; index < players.size(); ++index) {
        QDBusMessage message = QDBusMessage::createMethodCall(players[index], PlayerPath, Properties, "GetAll");
        message << QString::fromLatin1(PlayerInterface);
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation, round, index, players](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            if (generation != m_generation || round->over)
                return;
            const QDBusPendingReply<QVariantMap> reply = *call;
            Answer &answer = round->answers[size_t(index)];
            answer = Not;
            if (!reply.isError() && reply.value().value("PlaybackStatus").toString() == "Playing") {
                const QVariantMap metadata = qdbus_cast<QVariantMap>(reply.value().value("Metadata"));
                const QString title = metadata.value("xesam:title").toString().trimmed();
                if (!title.isEmpty()) {
                    answer = Playing;
                    round->titles[size_t(index)] = title;
                    round->artists[size_t(index)] =
                        metadata.value("xesam:artist").toStringList().join(", ").trimmed();
                }
            }

            // The player shown if it is still a candidate, or else the first
            // that is. A candidate yet to answer has to be waited for.
            int chosen = int(players.indexOf(m_player));
            if (chosen < 0 || round->answers[size_t(chosen)] == Not) {
                chosen = -1;
                for (int i = 0; i < players.size() && chosen < 0; ++i)
                    if (round->answers[size_t(i)] != Not)
                        chosen = i;
            }
            if (chosen >= 0 && round->answers[size_t(chosen)] == Waiting)
                return;
            round->over = true;
            if (chosen >= 0) {
                m_player = players[chosen];
                m_found(round->titles[size_t(chosen)], round->artists[size_t(chosen)]);
            }
            done();
        });
    }
}

} // namespace

NowPlaying::NowPlaying(QObject *parent) : QObject(parent) {
    new Mpris([this](const QString &title, const QString &artist) { take(title, artist); }, this);
}

#include "nowplaying_mpris.moc"
