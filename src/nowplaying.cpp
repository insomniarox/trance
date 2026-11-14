#include "nowplaying.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QHash>
#include <memory>

namespace {
const char *const PlayerPath = "/org/mpris/MediaPlayer2";
const char *const Properties = "org.freedesktop.DBus.Properties";
const char *const PlayerInterface = "org.mpris.MediaPlayer2.Player";
} // namespace

NowPlaying::NowPlaying(QObject *parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &NowPlaying::refresh);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    // Any player announcing a change is a reason to look again.
    bus.connect(QString(), PlayerPath, Properties, "PropertiesChanged", this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    m_debounce.start();
}

void NowPlaying::propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &) {
    if (interface == QLatin1String(PlayerInterface))
        m_debounce.start();
}

void NowPlaying::refresh() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *watcher = new QDBusPendingCallWatcher(bus.interface()->asyncCall("ListNames"), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isError())
            return;
        QStringList players;
        for (const auto &name : reply.value())
            if (name.startsWith("org.mpris.MediaPlayer2."))
                players << name;
        ask(players);
    });
}

// Asks every player what it is doing and, once all have answered, takes
// one that is playing: the one already shown if it still is, so two playing
// at once don't take turns, or else the first. A paused player keeps the
// last title on screen.
void NowPlaying::ask(const QStringList &players) {
    struct Round {
        int left = 0;
        QHash<QString, QVariantMap> playing;
    };
    const int generation = ++m_generation;
    auto round = std::make_shared<Round>();
    round->left = int(players.size());
    for (const auto &player : players) {
        QDBusMessage message = QDBusMessage::createMethodCall(player, PlayerPath, Properties, "GetAll");
        message << QString::fromLatin1(PlayerInterface);
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation, round, player, players](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            if (generation != m_generation)
                return;
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (!reply.isError() && reply.value().value("PlaybackStatus").toString() == "Playing")
                round->playing.insert(player, reply.value());
            if (--round->left > 0 || round->playing.isEmpty())
                return;
            QString chosen = m_player;
            if (!round->playing.contains(chosen))
                for (const auto &name : players)
                    if (round->playing.contains(name)) {
                        chosen = name;
                        break;
                    }
            const QVariantMap metadata = qdbus_cast<QVariantMap>(round->playing.value(chosen).value("Metadata"));
            const QString title = metadata.value("xesam:title").toString().trimmed();
            const QString artist = metadata.value("xesam:artist").toStringList().join(", ").trimmed();
            m_player = chosen;
            if (title.isEmpty() || (title == m_title && artist == m_artist))
                return;
            m_title = title;
            m_artist = artist;
            emit changed();
        });
    }
}
