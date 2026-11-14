#pragma once

#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

// The track that is playing, from whichever media player says so over MPRIS
// (Spotify, a browser, mpv, ...), so its title can be shown when it changes.
class NowPlaying : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)

public:
    explicit NowPlaying(QObject *parent = nullptr);

    QString title() const { return m_title; }
    QString artist() const { return m_artist; }

signals:
    void changed();

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void refresh();
    void ask(const QStringList &players);

    QTimer m_debounce;
    QString m_title, m_artist;
    QString m_player; // whose title is showing
    int m_generation = 0;
};
