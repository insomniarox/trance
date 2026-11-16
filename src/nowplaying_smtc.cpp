// What is playing on Windows: asked of the system's media controls (SMTC),
// which Spotify, the browsers and most players report to. It is the list the
// flyout over the volume keys shows.
//
// Windows says that something changed on threads of its own, and answers a
// question only after a wait. So the asking is done on a thread of its own,
// which waits there, and hands what it finds to the thread Trance lives on.
#include "nowplaying.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

namespace {

using namespace std::chrono_literals;
using winrt::Windows::Foundation::AsyncStatus;
using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using Status = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

// What Windows answers, or nothing when it takes too long over it: a player
// that has stopped responding must not keep Trance from closing.
template <typename Question>
auto answer(const Question &question, std::chrono::milliseconds patience = 1500ms) -> decltype(question.GetResults()) {
    if (question.wait_for(patience) != AsyncStatus::Completed) {
        question.Cancel();
        return nullptr;
    }
    return question.GetResults();
}

bool playing(const Session &session) {
    try {
        return session && session.GetPlaybackInfo().PlaybackStatus() == Status::Playing;
    } catch (const winrt::hresult_error &) {
        return false; // gone since it was listed
    }
}

QString text(const winrt::hstring &string) {
    return QString::fromWCharArray(string.c_str(), int(string.size())).trimmed();
}

// Rung by Windows, on its threads, when a player comes, goes or changes, and
// by Trance when it closes. It outlives the watching if Windows rings late.
struct Bell {
    std::mutex mutex;
    std::condition_variable rung;
    bool changed = true; // true to begin with: what is playing at the start is asked for
    bool closing = false;

    void ring(bool Bell::*what) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            this->*what = true;
        }
        rung.notify_all();
    }

    // Waits this long, or less when closing, and says whether it is. If not,
    // what is playing is to be asked for again.
    bool rest(std::chrono::seconds time) {
        std::unique_lock<std::mutex> lock(mutex);
        if (rung.wait_for(lock, time, [this] { return closing; }))
            return true;
        changed = true;
        return false;
    }
};

class Smtc : public QObject {
public:
    Smtc(std::function<void(const QString &, const QString &)> found, QObject *parent);
    ~Smtc() override;

private:
    // A player, and being told when its track or its playing changes.
    struct Player {
        Session session;
        Session::MediaPropertiesChanged_revoker track;
        Session::PlaybackInfoChanged_revoker playback;
    };

    // Until closing, which it says, or until the manager stops answering.
    // looked is set once it has.
    bool watch(const Manager &manager, bool *looked);
    void look(const Manager &manager);

    std::function<void(const QString &, const QString &)> m_found;
    std::shared_ptr<Bell> m_bell = std::make_shared<Bell>();
    std::vector<Player> m_players; // only touched while watching
    winrt::hstring m_player;       // whose title is showing: the id Windows gives its application
    std::thread m_watching;
};

Smtc::Smtc(std::function<void(const QString &, const QString &)> found, QObject *parent)
    : QObject(parent), m_found(std::move(found)) {
    m_watching = std::thread([this] {
        winrt::init_apartment();
        // Asked for again until closing: the first question of all can find
        // Windows still starting what answers it, as at login, and what
        // answers can restart. A little longer each time it fails, and from
        // the start again once it has worked.
        for (std::chrono::seconds pause = 1s;; pause = std::min<std::chrono::seconds>(2 * pause, 60s)) {
            bool looked = false;
            try {
                if (const Manager manager = answer(Manager::RequestAsync(), 5000ms))
                    if (watch(manager, &looked))
                        break;
            } catch (const winrt::hresult_error &) {
                // No media controls to ask, for now.
            }
            m_players.clear();
            if (looked)
                pause = 1s;
            if (m_bell->rest(pause))
                break;
        }
        m_players.clear();
        winrt::clear_factory_cache();
        winrt::uninit_apartment();
    });
}

Smtc::~Smtc() {
    m_bell->ring(&Bell::closing);
    m_watching.join();
}

bool Smtc::watch(const Manager &manager, bool *looked) {
    const std::shared_ptr<Bell> bell = m_bell;
    const auto ring = [bell](const auto &, const auto &) { bell->ring(&Bell::changed); };
    const auto listChanged = manager.SessionsChanged(winrt::auto_revoke, ring);
    const auto currentChanged = manager.CurrentSessionChanged(winrt::auto_revoke, ring);
    const auto stirred = [&] { return bell->changed || bell->closing; };
    std::unique_lock<std::mutex> lock(bell->mutex);
    for (;;) {
        // Looked at every so often even when nothing rings, as nothing would
        // if what answers had gone: the looking then fails, and says so.
        bell->rung.wait_for(lock, 30s, stirred);
        // A new track is announced in several steps: look once they have
        // stopped coming for a moment.
        while (bell->changed && !bell->closing) {
            bell->changed = false;
            bell->rung.wait_for(lock, 150ms, stirred);
        }
        if (bell->closing)
            return true;
        lock.unlock();
        try {
            look(manager);
        } catch (const winrt::hresult_error &) {
            // What answers has gone, or a player did while it was asked:
            // asked for again from the start, which covers both.
            return false;
        }
        *looked = true;
        lock.lock();
    }
}

// Asks every player what it is doing and takes one that is playing: the one
// already shown if it still is, so two playing at once don't take turns, or
// else the one Windows holds to be the current one, or else the first. A
// paused player keeps the last title on screen.
void Smtc::look(const Manager &manager) {
    const auto ring = [bell = m_bell](const auto &, const auto &) { bell->ring(&Bell::changed); };
    // Every player is listened to before any is asked, so that nothing
    // changes unheard in between.
    std::vector<Player> players;
    for (const Session &session : manager.GetSessions()) {
        try {
            players.push_back({session, session.MediaPropertiesChanged(winrt::auto_revoke, ring),
                               session.PlaybackInfoChanged(winrt::auto_revoke, ring)});
        } catch (const winrt::hresult_error &) {
            // gone since it was listed
        }
    }
    m_players = std::move(players);

    Session chosen = nullptr;
    for (const Player &player : m_players)
        if (player.session.SourceAppUserModelId() == m_player && playing(player.session)) {
            chosen = player.session;
            break;
        }
    if (!chosen) {
        const Session current = manager.GetCurrentSession();
        if (playing(current))
            chosen = current;
    }
    for (auto player = m_players.begin(); !chosen && player != m_players.end(); ++player)
        if (playing(player->session))
            chosen = player->session;
    if (!chosen)
        return;
    const auto track = answer(chosen.TryGetMediaPropertiesAsync());
    if (!track)
        return;
    m_player = chosen.SourceAppUserModelId();
    QMetaObject::invokeMethod(this, [this, title = text(track.Title()), artist = text(track.Artist())] {
        m_found(title, artist);
    }, Qt::QueuedConnection);
}

} // namespace

NowPlaying::NowPlaying(QObject *parent) : QObject(parent) {
    new Smtc([this](const QString &title, const QString &artist) { take(title, artist); }, this);
}
