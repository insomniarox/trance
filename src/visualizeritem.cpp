#include "visualizeritem.h"

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <memory>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>

#include "backend.h"
#include "engine.h"
#include "shaderworker.h"

namespace {

Backend *sharedBackend = nullptr;
QOffscreenSurface *buildSurface = nullptr;

class VisualizerRenderer : public QQuickFramebufferObject::Renderer {
public:
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        m_size = size;
        return new QOpenGLFramebufferObject(size, QOpenGLFramebufferObject::NoAttachment);
    }

    // Runs with the window's thread paused, so reading the backend is safe;
    // everything the frame needs is copied across here.
    void synchronize(QQuickFramebufferObject *item) override {
        Backend *backend = sharedBackend;
        if (!backend)
            return;
        m_audio = backend->audioFrame();
        if (backend->compiledRevision() != m_presetRevision) {
            m_presetRevision = backend->compiledRevision();
            m_engine.setPreset(backend->compiled(), backend->pendingBlend());
        }
        if (backend->themeRevision() != m_themeRevision) {
            m_themeRevision = backend->themeRevision();
            m_engine.setTheme(backend->themeColors());
        }
        if (backend->effectivePalette() != m_palette) {
            m_palette = backend->effectivePalette();
            m_engine.setPaletteOverride(m_palette);
        }
        m_engine.setTransitionStyle(backend->transitionIndex());
        if (backend->visitorRate() != m_visitorRate) {
            m_visitorRate = backend->visitorRate();
            m_engine.setVisitors(backend->visitorPool(), m_visitorRate);
        }
        if (backend->titleRevision() != m_titleRevision) {
            m_titleRevision = backend->titleRevision();
            m_engine.showText(backend->titleImage());
        }
        m_engine.setGlass(backend->glass());
        m_engine.setGlassOpacity(backend->glassOpacity());
        m_engine.setQuality(backend->quality());
        m_engine.setMaxLines(backend->maxLines());
        m_engine.setProgress(backend->progress());
        // A shader the driver rejects is reported like any other problem.
        // This runs on the render thread, so both are handed over rather
        // than called: their signals reach QML.
        if (m_engine.error() != m_reportedError) {
            m_reportedError = m_engine.error();
            QMetaObject::invokeMethod(backend, [backend, error = m_reportedError] { backend->setEngineError(error); },
                                      Qt::QueuedConnection);
        }
        if (m_statsDue) {
            m_statsDue = false;
            auto *visualizer = static_cast<VisualizerItem *>(item);
            QMetaObject::invokeMethod(visualizer, [visualizer, stats = m_stats] { visualizer->setStats(stats); },
                                      Qt::QueuedConnection);
        }
    }

    void render() override {
        // The first time through, with the drawing context current: set up
        // the thread that gets the next preset's shaders ready.
        if (!m_workerTried) {
            m_workerTried = true;
            if (buildSurface) {
                m_worker = std::make_unique<ShaderWorker>(buildSurface);
                if (m_worker->usable())
                    m_engine.setProgramBuilder(m_worker.get());
                else
                    m_worker.reset();
            }
        }
        const double dt = m_clock.isValid() ? m_clock.nsecsElapsed() / 1e9 : 1.0 / 60;
        m_clock.start();
        // The time the engine takes to hand a frame to the graphics card;
        // the card's own work runs on after it.
        QElapsedTimer engineClock;
        engineClock.start();
        m_engine.render(m_audio, dt, framebufferObject()->handle(), m_size.width(), m_size.height());
        m_engineSeconds += engineClock.nsecsElapsed() / 1e9;
        QQuickOpenGLUtils::resetOpenGLState();

        if (m_stats.graphics.isEmpty()) {
            QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
            m_stats.graphics = QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
            m_glVersion = QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
        }

        ++m_frames;
        m_elapsed += dt;
        m_worst = qMax(m_worst, dt);
        if (m_elapsed >= 0.5) {
            m_stats.fps = m_frames / m_elapsed;
            m_stats.frameMs = 1000 * m_elapsed / m_frames;
            m_stats.worstMs = 1000 * m_worst;
            m_stats.width = m_size.width();
            m_stats.height = m_size.height();
            m_stats.pictureWidth = m_engine.pictureWidth();
            m_stats.pictureHeight = m_engine.pictureHeight();
            m_stats.blending = m_engine.changing();
            m_stats.visitors = m_engine.visitorCount();
            m_stats.details = QVariantMap{{"engineMs", 1000 * m_engineSeconds / m_frames},
                                          {"vertices", m_engine.vertexCount()},
                                          {"particles", m_engine.particleCount()},
                                          {"building", m_engine.building()},
                                          {"changeProgress", m_engine.changeProgress()},
                                          {"changePattern", m_engine.changePattern()},
                                          {"visitorNames", m_engine.visitorNames()},
                                          {"glVersion", m_glVersion}};
            m_engineSeconds = 0;
            m_frames = 0;
            m_elapsed = 0;
            m_worst = 0;
            m_statsDue = true;
        }
    }

private:
    // The engine goes last and the worker first, so nothing the engine was
    // handed is gone while it still stands.
    Engine m_engine;
    std::unique_ptr<ShaderWorker> m_worker;
    bool m_workerTried = false;
    AudioFrame m_audio;
    QSize m_size;
    QElapsedTimer m_clock;
    int m_presetRevision = -1, m_themeRevision = -1, m_titleRevision = 0;
    double m_visitorRate = -1;
    QString m_palette, m_reportedError;
    VisualizerItem::Stats m_stats;
    int m_frames = 0;
    double m_elapsed = 0, m_worst = 0, m_engineSeconds = 0;
    QString m_glVersion;
    bool m_statsDue = false;
};

} // namespace

VisualizerItem::VisualizerItem(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    // The engine draws with y up, as OpenGL does.
    setMirrorVertically(true);
    // This is an animation that never settles, so every frame asks for the
    // next. It has to be the item that asks: only an update of the item
    // makes Qt Quick call the renderer's synchronize(), which is where the
    // music, the preset and the settings cross over to the render thread. A
    // renderer that re-arms itself keeps drawing, but with what it was given
    // on the first frame.
    //
    // The update is queued until the frame has been synchronized. Asked for
    // at once, it does nothing when the item is already waiting for this
    // frame, as it is after a resize (a panel opening or closing), and then
    // nothing asks for the next frame and the picture stands still. After
    // the sync the item is never waiting, so the update always asks.
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        if (window)
            connect(window, &QQuickWindow::afterAnimating, this, &QQuickItem::update,
                    Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
    });
}

QQuickFramebufferObject::Renderer *VisualizerItem::createRenderer() const { return new VisualizerRenderer; }

void VisualizerItem::setStats(const Stats &stats) {
    m_stats = stats;
    emit statsChanged();
}

void VisualizerItem::setBackend(Backend *backend) { sharedBackend = backend; }
void VisualizerItem::setBuildSurface(QOffscreenSurface *surface) { buildSurface = surface; }
Backend *VisualizerItem::backend() { return sharedBackend; }
