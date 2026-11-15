#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// Follows the Omarchy theme live: the accent for the controls, every named
// color for the visuals, and Hyprland's window rounding for the corners.
// Without Omarchy the built-in colors stand in; without Hyprland the corners
// stay sharp.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    // hyprland is the directory its sockets are in, found when left empty.
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprland = {});

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    // Every color the theme names (accent, red, background, ...).
    QHash<QString, QColor> colors() const { return m_colors; }

    static QString readAccent(const QString &path);
    static QHash<QString, QColor> readColors(const QString &path);
    static QString defaultDirectory();

signals:
    void changed();

private:
    void reload();
    // Hyprland's side, in theme_hyprland.cpp.
    void followHyprland(const QString &directory, bool find);
    void listenToHyprland();
    void refreshRounding();
    void takeRounding(const QByteArray &reply);

    QString m_directory;
    QString m_accent = "#FFD60A";
    QHash<QString, QColor> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprland;
    QLocalSocket m_hyprlandEvents;
    QTimer m_retry;
    bool m_asking = false;
    int m_radius = 0;
};
