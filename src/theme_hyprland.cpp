// The window's corners, from Hyprland: only where Hyprland can be, which is
// not Windows.
#include "theme.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

// One question to Hyprland, answered by its reply or by nothing. Hyprland
// takes a connection on the thread that runs the whole desktop and stops
// there until the question arrives, for up to five seconds, so the question
// has to go out in the same breath as the connection. Asked from Trance's
// window thread it would wait for that thread's next turn, and on a change
// of focus that thread may itself be waiting on the desktop: everything
// stood still until Hyprland gave up. This blocks, so it runs on a thread
// of its own, where nothing in Trance can hold it up.
QByteArray askHyprland(const QString &socketPath, const QByteArray &question, int timeoutMs) {
    const QByteArray path = QFile::encodeName(socketPath);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= qsizetype(sizeof(address.sun_path)))
        return {};
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};
    QByteArray answer;
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0 &&
        ::send(fd, question.constData(), size_t(question.size()), MSG_NOSIGNAL) == question.size()) {
        // Hyprland answers and hangs up; one that never does is given up on.
        pollfd wait{fd, POLLIN, 0};
        char buffer[4096];
        while (::poll(&wait, 1, timeoutMs) > 0) {
            const ssize_t got = ::read(fd, buffer, sizeof(buffer));
            if (got <= 0)
                break;
            answer.append(buffer, qsizetype(got));
        }
    }
    ::close(fd);
    return answer;
}

} // namespace

// directory is where Hyprland's sockets are. Left empty, it is looked for
// when find says so.
void Theme::followHyprland(const QString &directory, bool find) {
    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask Hyprland rather than reading its config:
    // whenever its events start arriving, and again each time one says the
    // config was reloaded. A change made at runtime sends no event, so it is
    // also asked each time Trance's window becomes active, which is when the
    // corners are looked at again. A connection that fails or drops is tried
    // again, after a second at first and then ever more rarely.
    m_hyprland = directory;
    if (m_hyprland.isEmpty() && find && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        m_hyprland = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    connect(&m_hyprlandEvents, &QLocalSocket::connected, this, [this] {
        m_retry.setInterval(1000);
        refreshRounding();
    });
    connect(&m_hyprlandEvents, &QLocalSocket::readyRead, this, [this] {
        while (m_hyprlandEvents.canReadLine())
            if (m_hyprlandEvents.readLine().startsWith("configreloaded>>"))
                refreshRounding();
    });
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    connect(&m_hyprlandEvents, &QLocalSocket::disconnected, &m_retry, qOverload<>(&QTimer::start));
    connect(&m_hyprlandEvents, &QLocalSocket::errorOccurred, &m_retry, qOverload<>(&QTimer::start));
    connect(&m_retry, &QTimer::timeout, this, [this] {
        m_retry.setInterval(qMin(m_retry.interval() * 2, 60000));
        listenToHyprland();
    });
    if (qGuiApp)
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive)
                refreshRounding();
        });
    listenToHyprland();
}

void Theme::listenToHyprland() {
    if (!m_hyprland.isEmpty() && m_hyprlandEvents.state() == QLocalSocket::UnconnectedState)
        m_hyprlandEvents.connectToServer(m_hyprland + "/.socket2.sock", QIODevice::ReadOnly);
}

// Asks Hyprland for the rounding, away from this thread (see askHyprland),
// and takes the answer here when it comes. One question at a time: a focus
// flickering back and forth needs no more than the one already asked.
void Theme::refreshRounding() {
    if (m_hyprland.isEmpty() || m_asking || !QCoreApplication::instance())
        return;
    m_asking = true;
    const QString socket = m_hyprland + "/.socket.sock";
    std::thread([self = QPointer<Theme>(this), socket] {
        const QByteArray reply = askHyprland(socket, "j/getoption decoration:rounding", 2000);
        // The application outlives every Theme, so the answer is handed to
        // it and only then checked against a Theme that may have gone.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, reply] {
            if (self)
                self->takeRounding(reply);
        }, Qt::QueuedConnection);
    }).detach();
}

void Theme::takeRounding(const QByteArray &reply) {
    m_asking = false;
    const auto value = QJsonDocument::fromJson(reply).object().value("int");
    if (!value.isDouble() || value.toInt(-1) < 0 || value.toInt() == m_radius)
        return;
    m_radius = value.toInt();
    emit changed();
}
