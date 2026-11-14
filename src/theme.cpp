// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprland)
    : QObject(parent), m_directory(directory.isEmpty() ? defaultDirectory() : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask Hyprland rather than reading its config:
    // once now, and again whenever it says its config was reloaded.
    m_hyprland = hyprland;
    if (m_hyprland.isEmpty() && directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        m_hyprland = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    connect(&m_hyprlandEvents, &QLocalSocket::readyRead, this, [this] {
        while (m_hyprlandEvents.canReadLine())
            if (m_hyprlandEvents.readLine().startsWith("configreloaded>>"))
                refreshRounding();
    });
    if (!m_hyprland.isEmpty()) {
        m_hyprlandEvents.connectToServer(m_hyprland + "/.socket2.sock", QIODevice::ReadOnly);
        refreshRounding();
    }
    reload();
}

QString Theme::defaultDirectory() { return QDir::homePath() + "/.local/state/omarchy/current"; }

// One question over Hyprland's request socket, which answers and hangs up.
void Theme::refreshRounding() {
    if (m_hyprland.isEmpty())
        return;
    auto *request = new QLocalSocket(this);
    connect(request, &QLocalSocket::connected, request, [request] { request->write("j/getoption decoration:rounding"); });
    connect(request, &QLocalSocket::errorOccurred, request, &QObject::deleteLater);
    connect(request, &QLocalSocket::disconnected, this, [this, request] {
        request->deleteLater();
        const auto value = QJsonDocument::fromJson(request->readAll()).object().value("int");
        if (!value.isDouble() || value.toInt(-1) < 0 || value.toInt() == m_radius)
            return;
        m_radius = value.toInt();
        emit changed();
    });
    request->connectToServer(m_hyprland + "/.socket.sock");
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
