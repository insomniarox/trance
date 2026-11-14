#include "shaderworker.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

ShaderWorker::ShaderWorker(QOffscreenSurface *surface) : m_surface(surface) {
    QOpenGLContext *drawing = QOpenGLContext::currentContext();
    if (!drawing || !surface || !surface->isValid())
        return;
    m_context = new QOpenGLContext;
    m_context->setFormat(drawing->format());
    m_context->setShareContext(drawing);
    if (!m_context->create() || !m_context->shareContext()) {
        delete m_context;
        m_context = nullptr;
        return;
    }
    m_context->moveToThread(this);
    start();
    // Whether the context can be made current over there is only known
    // once it has been tried.
    QMutexLocker lock(&m_mutex);
    while (!m_started)
        m_ready.wait(&m_mutex);
}

ShaderWorker::~ShaderWorker() {
    if (isRunning()) {
        {
            QMutexLocker lock(&m_mutex);
            m_quit = true;
            m_wake.wakeAll();
        }
        wait();
    }
    delete m_context;
}

void ShaderWorker::request(int id, const QString &warp, const QString &composite) {
    QMutexLocker lock(&m_mutex);
    m_jobs.push_back(Job{id, warp, composite});
    m_wake.wakeAll();
}

bool ShaderWorker::take(Result *result) {
    QMutexLocker lock(&m_mutex);
    if (m_results.empty())
        return false;
    *result = m_results.front();
    m_results.pop_front();
    return true;
}

void ShaderWorker::run() {
    const bool current = m_context->makeCurrent(m_surface);
    {
        QMutexLocker lock(&m_mutex);
        m_usable = current;
        m_started = true;
        m_ready.wakeAll();
    }
    if (current) {
        QOpenGLExtraFunctions *gl = m_context->extraFunctions();
        const bool es = m_context->isOpenGLES();
        for (;;) {
            Job job;
            {
                QMutexLocker lock(&m_mutex);
                while (m_jobs.empty() && !m_quit)
                    m_wake.wait(&m_mutex);
                if (m_quit)
                    break;
                // Only the latest is wanted: anything asked for before it
                // has been overtaken and would be thrown away unseen.
                job = m_jobs.back();
                m_jobs.clear();
            }
            Result result;
            result.id = job.id;
            result.warp = Engine::buildProgram(gl, es, Engine::screenVertexShader(), job.warp, &result.error);
            if (result.warp)
                result.composite =
                    Engine::buildProgram(gl, es, Engine::screenVertexShader(), job.composite, &result.error);
            if (result.warp && !result.composite) {
                gl->glDeleteProgram(result.warp);
                result.warp = 0;
            }
            // What was made here is only certain to be there for the other
            // context once this one has finished with it.
            gl->glFinish();
            QMutexLocker lock(&m_mutex);
            m_results.push_back(result);
        }
        // Whatever nobody came for.
        for (const Result &result : m_results) {
            if (result.warp)
                gl->glDeleteProgram(result.warp);
            if (result.composite)
                gl->glDeleteProgram(result.composite);
        }
        m_context->doneCurrent();
    }
    // The context goes back to whoever deletes this, which a context may
    // only be moved to from the thread it is on.
    m_context->moveToThread(nullptr);
}
