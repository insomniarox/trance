#include "remote.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QStandardPaths>

#include "audiocapture.h"
#include "backend.h"

namespace remote {

QString defaultName() {
#ifdef Q_OS_WIN
    // Pipes are named for the whole machine, so the user's name goes in.
    return "trance-" + qEnvironmentVariable("USERNAME", "user");
#else
    // The directory only its user can get into, and which goes at logout.
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/trance.sock";
#endif
}

Server::Server(Backend *backend, const QString &name) : QObject(backend), m_backend(backend) {
    // A window that is already there keeps the place. If nobody answers,
    // whatever is in the way was left behind by a window that crashed.
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(200))
        return;
    QLocalServer::removeServer(name);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(name))
        return;
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, client, [this, client] {
                if (!client->canReadLine())
                    return;
                const QString request = QString::fromUtf8(client->readLine()).trimmed();
                client->write(answer(request.section(' ', 0, 0), request.section(' ', 1)).toUtf8() + '\n');
                client->flush();
                client->disconnectFromServer();
            });
        }
    });
}

QString Server::answer(const QString &word, const QString &argument) {
    const auto yes = [](bool done) { return QString(done ? "ok" : "no"); };
    // Changing preset from a key binding would throw away what is being
    // edited without asking, as the window itself never does: refused, and
    // said why, so the command can pass it on.
    static const QStringList changes{"Next", "Previous", "Random", "Select", "Mashup"};
    if (changes.contains(word) && m_backend->dirty())
        return "unsaved";
    if (word == "Next")
        m_backend->next();
    else if (word == "Previous")
        m_backend->previous();
    else if (word == "Random")
        m_backend->random();
    else if (word == "ToggleLock")
        m_backend->setLocked(!m_backend->locked());
    else if (word == "Select")
        return yes(m_backend->select(argument));
    else if (word == "Mashup")
        return yes(m_backend->mashup());
    else if (word == "Current")
        return "ok " + m_backend->currentName();
    else if (word == "Source") {
        // Listens to something else: an id as `trance sources` prints them,
        // or an application's name.
        if (argument.trimmed().isEmpty())
            return "no";
        m_backend->setSource(AudioCapture::isSource(argument) ? argument : "app:" + argument);
    } else
        return "no";
    return "ok";
}

bool send(const QString &request, QString *reply, const QString &name, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(request.toUtf8() + '\n');
    QElapsedTimer waited;
    waited.start();
    while (!socket.canReadLine()) {
        const qint64 left = timeoutMs - waited.elapsed();
        if (left <= 0 || !socket.waitForReadyRead(int(left)))
            break;
    }
    if (!socket.canReadLine())
        return false;
    *reply = QString::fromUtf8(socket.readLine()).trimmed();
    return true;
}

} // namespace remote
