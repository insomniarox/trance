#pragma once

#include <QLocalServer>
#include <QString>

class Backend;

// How `trance next` and friends reach a running window, so a key binding
// can steer it: over a local socket, which is a Unix socket on Linux and a
// named pipe on Windows. One line goes each way. The command sends a word
// and, where it takes one, a space and what it is about ("Select Neon
// Tunnel"); the window answers "ok", "ok" and a value, or "no".
namespace remote {

// Where a window listens: one place for each user.
QString defaultName();

// The window's side. The first window to open takes the place, and holds it
// until it closes; one opened beside it is not steered.
class Server : public QObject {
public:
    explicit Server(Backend *backend, const QString &name = defaultName());

    bool isListening() const { return m_server.isListening(); }

private:
    QString answer(const QString &word, const QString &argument);

    Backend *m_backend;
    QLocalServer m_server;
};

// The command's side: sends one request and waits for the answer. False
// when no window is there to give one.
bool send(const QString &request, QString *reply, const QString &name = defaultName(), int timeoutMs = 2000);

} // namespace remote
