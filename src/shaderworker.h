#pragma once

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <deque>

#include "engine.h"

class QOffscreenSurface;
class QOpenGLContext;

// Builds a preset's shaders on a thread of its own, so the picture keeps
// moving while the next preset is got ready. Compiling the two shaders of a
// preset takes a frame or two's worth of time, which on the thread that
// draws is a visible stumble at the start of every change.
//
// It has a context of its own that shares with the drawing one, so the
// programs it links can be used there. Make it on the drawing thread with
// that context current; the surface must have been created on the GUI
// thread and must outlive this.
class ShaderWorker : public QThread, public ProgramBuilder {
public:
    explicit ShaderWorker(QOffscreenSurface *surface);
    ~ShaderWorker() override;

    // False if the second context could not be had; the engine then builds
    // its shaders itself, as it does without a worker.
    bool usable() const { return m_usable; }

    // A request overtakes any before it that has not been started.
    void request(int id, const QString &warp, const QString &composite) override;
    bool take(Result *result) override;

protected:
    void run() override;

private:
    struct Job {
        int id;
        QString warp, composite;
    };

    QOffscreenSurface *m_surface;
    QOpenGLContext *m_context = nullptr;
    bool m_usable = false, m_started = false, m_quit = false;
    QMutex m_mutex;
    QWaitCondition m_wake, m_ready;
    std::deque<Job> m_jobs;
    std::deque<Result> m_results;
};
