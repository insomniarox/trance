// trance — a music visualizer for the Linux desktop: MilkDrop-style
// feedback and SoundSpectrum-style flow, in the colors of your theme.

#include <QGuiApplication>
#include <QIcon>
#include <QOffscreenSurface>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QUrl>
#include <cstdio>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "backend.h"
#include "cli.h"
#include "nowplaying.h"
#include "remote.h"
#include "theme.h"
#include "visualizeritem.h"

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

#ifdef Q_OS_WIN
    // What is printed is UTF-8, whatever the console was set to.
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Commands that need no window run without a display. On Windows there
    // always is one, and Qt's stand-in for a display has no OpenGL there to
    // render and check with, so the real one is used and nothing is shown.
    if (isCliCommand(command)) {
#ifndef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
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
    // and the launcher pick up the installed icon. On Windows the icon is in
    // the program itself, and its windows get it unasked.
    app.setDesktopFileName("trance");
#ifndef Q_OS_WIN
    app.setWindowIcon(QIcon::fromTheme("trance"));
#endif

    QQuickStyle::setStyle("Material");

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
    // The next preset's shaders are built on a thread of their own, which
    // needs a surface made here to work on. It is declared before the
    // window so that it outlasts it.
    QOffscreenSurface buildSurface;
    buildSurface.setFormat(format);
    buildSurface.create();
    VisualizerItem::setBuildSurface(&buildSurface);
    qmlRegisterType<VisualizerItem>("Trance", 1, 0, "Visualizer");

    if (!arguments.isEmpty()) {
        QString error;
        if (!backend.open(arguments.first(), &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
    }

    // What `trance next` and friends reach, so a key binding can steer the
    // window. It belongs to the backend, and goes with it.
    new remote::Server(&backend);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("nowPlaying", &nowPlaying);

    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
