// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <cmath>
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

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprland)
    : QObject(parent), m_directory(directory.isEmpty() ? defaultDirectory() : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask Hyprland rather than reading its config:
    // whenever its events start arriving, and again each time one says the
    // config was reloaded. A change made at runtime sends no event, so it is
    // also asked each time Trance's window becomes active, which is when the
    // corners are looked at again. A connection that fails or drops is tried
    // again, after a second at first and then ever more rarely.
    m_hyprland = hyprland;
    if (m_hyprland.isEmpty() && directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
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
    reload();
}

QString Theme::defaultDirectory() { return QDir::homePath() + "/.local/state/omarchy/current"; }

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

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// colors.toml is flat `name = "#rrggbb"` lines, which is all this reads.
QHash<QString, QColor> Theme::readColors(const QString &path) {
    QHash<QString, QColor> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression expression(R"re(^\s*([a-z_0-9]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                               QRegularExpression::MultilineOption);
    auto matches = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        colors.insert(match.captured(1), QColor(match.captured(2)));
    }
    return colors;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto colors = readColors(m_directory + "/theme/colors.toml");
    const QString accent = readAccent(m_directory + "/theme/colors.toml");
    if (colors != m_colors || accent != m_accent) {
        m_colors = colors;
        m_accent = accent;
        emit changed();
    }
}
