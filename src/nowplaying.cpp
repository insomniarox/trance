#include "nowplaying.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingReply>

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

// Asks every player what it is doing and takes the first one that is
// actually playing. A paused player keeps the last title on screen.
void NowPlaying::ask(const QStringList &players) {
    const int generation = ++m_generation;
    for (const auto &player : players) {
        QDBusMessage message = QDBusMessage::createMethodCall(player, PlayerPath, Properties, "GetAll");
        message << QString::fromLatin1(PlayerInterface);
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError() || generation != m_generation)
                return;
            const QVariantMap properties = reply.value();
            if (properties.value("PlaybackStatus").toString() != "Playing")
                return;
            const QVariantMap metadata = qdbus_cast<QVariantMap>(properties.value("Metadata"));
            const QString title = metadata.value("xesam:title").toString().trimmed();
            const QString artist = metadata.value("xesam:artist").toStringList().join(", ").trimmed();
            if (title.isEmpty() || (title == m_title && artist == m_artist))
                return;
            m_title = title;
            m_artist = artist;
            emit changed();
        });
    }
}
