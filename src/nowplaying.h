#pragma once

#include <QObject>
#include <QString>

// The track that is playing, from whichever media player says so (Spotify, a
// browser, mpv, ...), so its title can be shown when it changes.
//
// Who is asked differs by system, and so does the file the asking is in:
// nowplaying_mpris.cpp on Linux, where players say it over MPRIS.
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

private:
    // For the system's side: what is playing now. A track without a title,
    // or the one already showing, changes nothing.
    void take(const QString &title, const QString &artist) {
        if (title.isEmpty() || (title == m_title && artist == m_artist))
            return;
        m_title = title;
        m_artist = artist;
        emit changed();
    }

    QString m_title, m_artist;
};
