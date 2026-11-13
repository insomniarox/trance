// trance — a music visualizer for the Omarchy desktop: MilkDrop-style
// feedback and SoundSpectrum-style flow, in the colors of your theme.

#include <QDBusConnection>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "nowplaying.h"
#include "theme.h"
#include "translator.h"
#include "visualizeritem.h"

// What `trance next` and friends reach over D-Bus, so a key bound in
// Hyprland can steer the window.
class Remote : public QObject {
    Q_OBJECT

public:
    explicit Remote(Backend *backend) : QObject(backend), m_backend(backend) {}

public slots:
    void Next() { m_backend->next(); }
    void Previous() { m_backend->previous(); }
    void Random() { m_backend->random(); }
    bool Select(const QString &name) { return m_backend->select(name); }
    void ToggleLock() { m_backend->setLocked(!m_backend->locked()); }
    QString Current() { return m_backend->currentName(); }
    bool Mashup() { return m_backend->mashup(); }
    // Listens to something else: an id as `trance sources` prints them, or
    // an application's name.
    bool Source(const QString &source) {
        QString wanted = source;
        if (!AudioCapture::isSource(wanted))
            wanted = "app:" + source;
        if (source.trimmed().isEmpty())
            return false;
        m_backend->setSource(wanted);
        return true;
    }

private:
    Backend *m_backend;
};

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // Commands that need no window run without a display.
    if (isCliCommand(command)) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        app.setApplicationName("trance");
        return runCli(app.arguments());
    }

    // The engine is plain OpenGL 3.3, so Qt Quick has to draw with OpenGL
    // too, and the window carries an alpha channel for glass mode.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickWindow::setDefaultAlphaBuffer(true);

    QGuiApplication app(argc, argv);
    app.setApplicationName("trance");
    app.setApplicationVersion("0.1.0");

    // Matches the window to trance.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("trance");
    app.setWindowIcon(QIcon::fromTheme("trance"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    QStringList arguments = app.arguments().mid(1);
    if (!arguments.isEmpty() && arguments.first() == "open")
        arguments.removeFirst();
    if (!arguments.isEmpty() && arguments.first().startsWith('-')) {
        fprintf(stderr, "Unknown option %s\n\nUsage: trance <command> [options]\n\n%s\n", qPrintable(arguments.first()),
                qPrintable(cliSummary()));
        return 1;
    }

    Theme theme;
    Backend backend;
    backend.setTheme(&theme);
    NowPlaying nowPlaying;
    QObject::connect(&nowPlaying, &NowPlaying::changed, &backend,
                     [&] { backend.setTrack(nowPlaying.title(), nowPlaying.artist()); });
    VisualizerItem::setBackend(&backend);
    qmlRegisterType<VisualizerItem>("Trance", 1, 0, "Visualizer");

    if (!arguments.isEmpty()) {
        QString error;
        if (!backend.open(arguments.first(), &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
    }

    auto *remoteControl = new Remote(&backend);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(remote::WindowService))
        bus.registerObject(remote::Path, remote::Interface, remoteControl, QDBusConnection::ExportAllSlots);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("nowPlaying", &nowPlaying);

    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}

#include "main.moc"
