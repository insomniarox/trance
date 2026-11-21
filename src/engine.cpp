#include "engine.h"

#include <QOpenGLContext>
#include <algorithm>
#include <cmath>

namespace {

constexpr float Pi = 3.14159265358979f;
constexpr float Tau = 2 * Pi;

const char *const ScreenVertex = R"(
out vec2 v_uv;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  v_uv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *const PrimVertex = R"(
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_edge;
layout(location = 3) in vec2 a_tex;
uniform float u_aspect;
out vec4 v_color;
out vec2 v_edge;
out vec2 v_tex;
void main() {
  v_color = a_color;
  v_edge = a_edge;
  v_tex = a_tex;
  gl_Position = vec4(a_position.x / u_aspect, a_position.y, 0.0, 1.0);
}
)";

// a_edge runs to 1 at the rim of whatever is being drawn (across a line, out
// from a dot's middle), which is all it takes to antialias every primitive:
// 1/fwidth is the distance to the rim in pixels, and lines and dots are drawn
// a pixel fat so the fade sits just outside their true edge.
const char *const PrimFragment = R"(
uniform sampler2D u_tex;
in vec4 v_color;
in vec2 v_edge;
in vec2 v_tex;
out vec4 o_color;
void main() {
  float e = length(v_edge);
  float w = max(fwidth(e), 1e-5);
  vec4 c = v_color;
  c.a *= clamp((1.0 - e) / w - 0.5, 0.0, 1.0);
  if (v_tex.x > -0.5) {
    // A texture brings its own transparency (the picture has none; text does).
    vec4 t = texture(u_tex, v_tex);
    c.rgb *= t.rgb;
    c.a *= t.a;
  }
  o_color = c;
}
)";

const char *const BrightFragment = R"(
uniform sampler2D u_tex;
uniform vec2 u_texel;
uniform float u_threshold;
in vec2 v_uv;
out vec4 o_color;
void main() {
  vec3 c = texture(u_tex, v_uv + u_texel * vec2(-1.0, -1.0)).rgb + texture(u_tex, v_uv + u_texel * vec2(1.0, -1.0)).rgb +
           texture(u_tex, v_uv + u_texel * vec2(-1.0, 1.0)).rgb + texture(u_tex, v_uv + u_texel * vec2(1.0, 1.0)).rgb;
  c *= 0.25;
  float level = max(c.r, max(c.g, c.b));
  o_color = vec4(c * max(level - u_threshold, 0.0) / max(level, 1e-4), 1.0);
}
)";

const char *const BlurFragment = R"(
uniform sampler2D u_tex;
uniform vec2 u_step;
in vec2 v_uv;
out vec4 o_color;
void main() {
  vec3 c = texture(u_tex, v_uv).rgb * 0.227027;
  c += (texture(u_tex, v_uv + u_step * 1.3846).rgb + texture(u_tex, v_uv - u_step * 1.3846).rgb) * 0.316216;
  c += (texture(u_tex, v_uv + u_step * 3.2308).rgb + texture(u_tex, v_uv - u_step * 3.2308).rgb) * 0.070270;
  o_color = vec4(c, 1.0);
}
)";

void hsvToRgb(double h, double s, double v, float out[3]) {
    h = (h - std::floor(h)) * 6.0;
    s = std::min(1.0, std::max(0.0, s));
    const int sector = int(h) % 6;
    const double f = h - int(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const double rgb[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    for (int i = 0; i < 3; ++i)
        out[i] = float(rgb[sector][i]);
}

template <typename T> T clampTo(T value, T low, T high) { return std::min(std::max(value, low), high); }

// Shrinks `count` source values to `out.size()` points, keeping peaks when
// several fall on one point.
void resample(const float *source, int count, std::vector<float> &out, bool keepPeaks) {
    const int n = int(out.size());
    for (int i = 0; i < n; ++i) {
        const double from = double(i) * count / n, to = double(i + 1) * count / n;
        const int first = int(from), last = std::max(first + 1, std::min(count, int(std::ceil(to))));
        float value = 0.f;
        if (keepPeaks) {
            for (int k = first; k < last; ++k)
                value = std::max(value, source[k]);
        } else {
            for (int k = first; k < last; ++k)
                value += source[k];
            value /= float(last - first);
        }
        out[i] = value;
    }
}

} // namespace

Engine::Engine() = default;

Engine::~Engine() {
    if (!m_initialized || !QOpenGLContext::currentContext())
        return;
    destroyScene(m_scene);
    destroyScene(m_old);
    for (auto &visitor : m_visitors)
        destroyScene(visitor);
    for (GLuint program : {m_prim, m_bright, m_blur})
        if (program)
            glDeleteProgram(program);
    for (Target *target : {&m_buffers[0], &m_buffers[1], &m_bloom[0], &m_bloom[1], &m_snapshot, &m_other})
        releaseTarget(*target);
    glDeleteVertexArrays(1, &m_emptyVao);
    glDeleteVertexArrays(1, &m_primVao);
    glDeleteBuffers(1, &m_vbo);
    if (m_textTexture)
        glDeleteTextures(1, &m_textTexture);
}

void Engine::setPreset(const CompiledPreset &preset, double blendSeconds) {
    m_pending = preset;
    m_pendingBlend = blendSeconds;
    m_hasPending = true;
}

void Engine::setTheme(const ThemeColors &theme) {
    m_theme = theme;
    for (Scene *scene : {m_scene.get(), m_old.get()})
        if (scene)
            scene->colorsDirty = true;
    for (auto &visitor : m_visitors)
        visitor->colorsDirty = true;
}

void Engine::setPaletteOverride(const QString &name) {
    if (name == m_paletteOverride)
        return;
    m_paletteOverride = name;
    for (Scene *scene : {m_scene.get(), m_old.get()})
        if (scene)
            scene->colorsDirty = true;
}

void Engine::setVisitors(const QList<CompiledPreset> &visitors, double rate) {
    m_visitorPool = visitors;
    m_visitorRate = rate;
}

void Engine::showText(const QImage &image) {
    m_textImage = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
    m_textPending = !m_textImage.isNull();
    if (image.isNull())
        m_textAge = 1e9f;
}

double Engine::variable(const QString &name) const { return m_scene ? m_scene->p.vars.get(name) : 0.0; }

QString Engine::changePattern() const {
    // transitionStyles() starts with "mixed", which picks one of the rest.
    const QStringList styles = presets::transitionStyles();
    return m_old ? styles.value(m_pattern + 1) : QString();
}

QStringList Engine::visitorNames() const {
    QStringList names;
    for (const auto &visitor : m_visitors)
        names << visitor->p.name;
    return names;
}

int Engine::particleCount() const {
    size_t count = 0;
    const auto add = [&count](const Scene *scene) {
        if (scene)
            for (const LayerState &layer : scene->layers)
                count += layer.particles.size();
    };
    add(m_scene.get());
    add(m_old.get());
    for (const auto &visitor : m_visitors)
        add(visitor.get());
    return int(count);
}

float Engine::random() {
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return float(m_random & 0xFFFFFF) / float(0x1000000);
}

// --------------------------------------------------------------- GL plumbing

void Engine::initialize() {
    initializeOpenGLFunctions();
    QOpenGLContext *context = QOpenGLContext::currentContext();
    m_es = context->isOpenGLES();
    m_wide = !m_es || context->hasExtension("GL_EXT_color_buffer_half_float") ||
             context->hasExtension("GL_EXT_color_buffer_float");

    QString error;
    m_prim = build(PrimVertex, PrimFragment, &error);
    m_bright = build(ScreenVertex, BrightFragment, &error);
    m_blur = build(ScreenVertex, BlurFragment, &error);
    if (!error.isEmpty())
        m_error = error;

    glGenVertexArrays(1, &m_emptyVao);
    glGenVertexArrays(1, &m_primVao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_primVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = 10 * sizeof(float);
    const int sizes[4] = {2, 4, 2, 2};
    size_t offset = 0;
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(offset));
        offset += sizes[i] * sizeof(float);
    }
    glBindVertexArray(0);
    m_initialized = true;
}

QString Engine::screenVertexShader() { return QString::fromLatin1(ScreenVertex); }

GLuint Engine::build(const QString &vertex, const QString &fragment, QString *error) {
    return buildProgram(this, m_es, vertex, fragment, error);
}

unsigned Engine::buildProgram(QOpenGLExtraFunctions *gl, bool es, const QString &vertex, const QString &fragment,
                              QString *error) {
    const QByteArray header = es ? "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n"
                                   : "#version 330 core\n";
    auto compile = [&](GLenum type, const QString &source) -> GLuint {
        const GLuint shader = gl->glCreateShader(type);
        const QByteArray text = header + source.toUtf8();
        const char *data = text.constData();
        gl->glShaderSource(shader, 1, &data, nullptr);
        gl->glCompileShader(shader);
        GLint ok = 0;
        gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            gl->glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            *error = QString::fromUtf8(log).trimmed();
            gl->glDeleteShader(shader);
            return 0;
        }
        return shader;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fragment) : 0;
    if (!vs || !fs) {
        if (vs)
            gl->glDeleteShader(vs);
        return 0;
    }
    const GLuint program = gl->glCreateProgram();
    gl->glAttachShader(program, vs);
    gl->glAttachShader(program, fs);
    gl->glLinkProgram(program);
    gl->glDeleteShader(vs);
    gl->glDeleteShader(fs);
    GLint ok = 0;
    gl->glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        gl->glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        *error = QString::fromUtf8(log).trimmed();
        gl->glDeleteProgram(program);
        return 0;
    }
    return program;
}

void Engine::ensureTarget(Target &target, int width, int height, bool wide, GLenum filter) {
    if (target.fbo && target.width == width && target.height == height)
        return;
    releaseTarget(target);
    target.width = width;
    target.height = height;
    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    // Half-float keeps slow fades smooth; 8 bits bands and leaves stains.
    if (wide)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    if (wide && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // The driver advertised float targets it can't render to.
        m_wide = false;
        releaseTarget(target);
        ensureTarget(target, width, height, false, filter);
        return;
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Engine::releaseTarget(Target &target) {
    if (target.fbo)
        glDeleteFramebuffers(1, &target.fbo);
    if (target.texture)
        glDeleteTextures(1, &target.texture);
    target = Target();
}

// ------------------------------------------------------------------- scenes

// Everything one preset needs to run. A visitor is a scene too, but only
// its layers are drawn, so it gets no shaders and no palette of its own.
std::unique_ptr<Engine::Scene> Engine::makeScene(const CompiledPreset &preset, bool shaders, QString *error,
                                                 GLuint warp, GLuint composite) {
    auto scene = std::make_unique<Scene>();
    scene->p = preset;
    if (shaders) {
        scene->warp = warp ? warp : build(ScreenVertex, scene->p.warpShader, error);
        scene->composite = composite ? composite
                                     : scene->warp ? build(ScreenVertex, scene->p.compositeShader, error) : 0;
        if (!scene->warp || !scene->composite) {
            destroyScene(scene);
            return nullptr;
        }
        glGenTextures(1, &scene->paletteTexture);
        glBindTexture(GL_TEXTURE_2D, scene->paletteTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // The palette is a loop, so reading past its end comes round again.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    scene->layers.assign(size_t(scene->p.layers.size()), LayerState());

    expr::Vars &vars = scene->p.vars;
    scene->s = Slots{vars.find("time"), vars.find("dt"), vars.find("frame"), vars.find("fps"), vars.find("bass"),
                     vars.find("mid"), vars.find("treb"), vars.find("vol"), vars.find("bass_att"),
                     vars.find("mid_att"), vars.find("treb_att"), vars.find("vol_att"), vars.find("beat"),
                     vars.find("beats"), vars.find("aspect"), vars.find("progress"), vars.find("t"), vars.find("i"),
                     vars.find("v"), vars.find("v2"), vars.find("n"), vars.find("count"), vars.find("x"),
                     vars.find("y"), vars.find("r"), vars.find("g"), vars.find("b"), vars.find("a"),
                     vars.find("radius"), vars.find("angle"), vars.find("size"), vars.find("r2"), vars.find("g2"),
                     vars.find("b2"), vars.find("a2"), vars.find("warpSpeed"), vars.find("rad"), vars.find("ang"),
                     vars.find("zoom"), vars.find("zoomexp"), vars.find("rot"), vars.find("warp"),
                     vars.find("warpScale"), vars.find("cx"), vars.find("cy"), vars.find("dx"), vars.find("dy"),
                     vars.find("sx"), vars.find("sy"), vars.find("bg.speed"), {}, {}, vars.find("z")};
    for (int band = 0; band < AudioFrame::Bands; ++band) {
        scene->s.band[band] = vars.find(bandInfo(band).name);
        scene->s.bandAtt[band] = vars.find(QString(bandInfo(band).name) + "_att");
    }
    scene->pixelReadsAngle = scene->p.pixel.reads(scene->s.ang);
    // The lists below point into these tables, which this scene now owns
    // alone and never changes again.
    scene->p.feedback.numbers.detach();
    scene->p.post.numbers.detach();
    scene->p.background.numbers.detach();
    const auto collect = [&](const Block &block, const QString &prefix,
                             std::vector<std::pair<int, const expr::Program *>> &into) {
        for (auto it = block.numbers.constBegin(); it != block.numbers.constEnd(); ++it) {
            const int slot = vars.find(prefix + QString::fromLatin1(it.key()));
            if (slot >= 0)
                into.emplace_back(slot, &it.value());
        }
    };
    collect(scene->p.feedback, "", scene->feedbackPrograms);
    collect(scene->p.post, "post.", scene->postPrograms);
    collect(scene->p.background, "bg.", scene->backgroundPrograms);
    return scene;
}

void Engine::destroyScene(std::unique_ptr<Scene> &scene) {
    if (!scene)
        return;
    if (scene->warp)
        glDeleteProgram(scene->warp);
    if (scene->composite)
        glDeleteProgram(scene->composite);
    if (scene->paletteTexture)
        glDeleteTextures(1, &scene->paletteTexture);
    if (m_c == scene.get())
        m_c = nullptr;
    if (m_colors == scene.get())
        m_colors = nullptr;
    scene.reset();
}

// Takes up the preset given to setPreset(). Its shaders are built here and
// now, or handed to the builder, in which case the preset comes on when
// collectBuilt() finds them ready.
void Engine::startPending() {
    m_hasPending = false;
    // The same preset with a number changed, as a slider sends many times a
    // second. Numbers live in the variable table, not in the shaders, so
    // those come out word for word the same and the ones already running
    // are handed on: nothing is built and the change shows this frame.
    if (m_scene && m_pendingBlend <= 0 && m_awaitedId == 0 && m_scene->p.name == m_pending.name &&
        m_scene->p.warpShader == m_pending.warpShader && m_scene->p.compositeShader == m_pending.compositeShader) {
        QString error;
        std::unique_ptr<Scene> scene = makeScene(m_pending, true, &error, m_scene->warp, m_scene->composite);
        m_scene->warp = m_scene->composite = 0;
        adopt(std::move(scene), 0, error);
        m_pending = CompiledPreset();
        return;
    }
    if (m_builder) {
        // An edit to the preset still being built keeps the change it came
        // with: a fade asked for does not become a cut.
        const bool waiting = m_awaitedId != 0;
        const double blend = waiting && m_pending.name == m_awaited.name ? std::max(m_pendingBlend, m_awaitedBlend)
                                                                          : m_pendingBlend;
        // With the same shaders, what is being built serves this one too. A
        // fresh request each slider tick would overtake every build before
        // it landed, and nothing would show until the dragging stopped.
        const bool sameShaders = waiting && m_awaited.warpShader == m_pending.warpShader &&
                                 m_awaited.compositeShader == m_pending.compositeShader;
        m_awaited = std::move(m_pending);
        m_awaitedBlend = blend;
        if (!sameShaders) {
            m_awaitedId = ++m_lastRequest;
            m_builder->request(m_awaitedId, m_awaited.warpShader, m_awaited.compositeShader);
        }
    } else {
        QString error;
        std::unique_ptr<Scene> scene = makeScene(m_pending, true, &error);
        adopt(std::move(scene), m_pendingBlend, error);
    }
    m_pending = CompiledPreset();
}

void Engine::collectBuilt() {
    ProgramBuilder::Result built;
    while (m_builder && m_builder->take(&built)) {
        if (built.id != m_awaitedId) {
            // Overtaken by a later preset before it was ready.
            if (built.warp)
                glDeleteProgram(built.warp);
            if (built.composite)
                glDeleteProgram(built.composite);
            continue;
        }
        m_awaitedId = 0;
        QString error = built.error;
        std::unique_ptr<Scene> scene =
            built.warp ? makeScene(m_awaited, true, &error, built.warp, built.composite) : nullptr;
        adopt(std::move(scene), m_awaitedBlend, error);
        m_awaited = CompiledPreset();
    }
}

// Brings a preset in: as the start of a change from the one showing, or,
// with no blend time, in its place.
void Engine::adopt(std::unique_ptr<Scene> scene, double blendSeconds, const QString &error) {
    if (!scene) {
        m_error = error;
        return;
    }
    m_error.clear();

    if (m_scene && blendSeconds > 0) {
        // A change that is cut short loses its older half: the preset that
        // was arriving becomes the one that is leaving.
        destroyScene(m_old);
        m_old = std::move(m_scene);
        m_blend = 0;
        m_blendRate = float(1.0 / blendSeconds);
        const int patterns = int(presets::transitionStyles().size()) - 1;
        if (m_transitionStyle >= 1 && m_transitionStyle <= patterns) {
            m_pattern = m_transitionStyle - 1;
        } else {
            m_pattern = int(random() * float(patterns)) % patterns;
            if (m_pattern == m_lastPattern)
                m_pattern = (m_pattern + 1) % patterns;
        }
        m_lastPattern = m_pattern;
        m_patternSeed = random();
    } else if (m_scene && m_scene->p.name == scene->p.name) {
        // The same preset with a setting changed, as the editor sends on
        // every keystroke. It carries on: its particles, its terrain, the
        // values its own code has built up, and its colors (so a new
        // palette fades in).
        Scene &was = *m_scene;
        for (size_t i = 0; i < scene->layers.size() && i < was.layers.size(); ++i)
            if (scene->p.layers.at(qsizetype(i)).type == was.p.layers.at(qsizetype(i)).type)
                scene->layers[i] = std::move(was.layers[i]);
        for (int slot = 0; slot < scene->p.vars.size(); ++slot) {
            const int before = was.p.vars.find(scene->p.vars.name(slot));
            if (before >= 0)
                scene->p.vars.values[size_t(slot)] = was.p.vars.values[size_t(before)];
        }
        scene->ranInit = was.ranInit && scene->p.initText == was.p.initText;
        scene->warpTime = was.warpTime;
        scene->backgroundTime = was.backgroundTime;
        if (was.hasColors) {
            std::copy(&was.lut[0][0], &was.lut[0][0] + 256 * 3, &scene->lut[0][0]);
            scene->hasColors = true;
        }
        destroyScene(m_scene);
    } else {
        destroyScene(m_scene);
    }
    if (!m_scene)
        m_scene = std::move(scene);
}

// Resolves everything that depends on the desktop theme: the palette lookup
// table and every color written as a theme name. A palette that replaces
// one already showing fades in rather than snapping.
void Engine::rebuildColors(Scene &scene) {
    scene.colorsDirty = false;
    if (scene.paletteTexture) {
        QStringList stops = m_paletteOverride.isEmpty() ? scene.p.palette : presets::paletteStops(m_paletteOverride);
        if (stops.isEmpty())
            stops = presets::paletteStops("theme");
        const int count = int(stops.size());
        std::vector<QColor> colors;
        for (const auto &stop : stops)
            colors.push_back(presets::resolveColor(stop, m_theme));
        // The table is a loop (the last stop blends back into the first), so
        // palette(t) can spin forever without a seam. Mapping brightness to
        // color uses all but that closing stretch.
        for (int i = 0; i < 256; ++i) {
            const double position = double(i) / 256 * count;
            const int index = int(position) % count;
            const double t = position - int(position);
            const QColor &a = colors[size_t(index)], &b = colors[size_t((index + 1) % count)];
            scene.lutTo[i][0] = float(a.redF() + (b.redF() - a.redF()) * t);
            scene.lutTo[i][1] = float(a.greenF() + (b.greenF() - a.greenF()) * t);
            scene.lutTo[i][2] = float(a.blueF() + (b.blueF() - a.blueF()) * t);
        }
        scene.paletteSpan = float(count - 1) / float(count);
        if (scene.hasColors) {
            std::copy(&scene.lut[0][0], &scene.lut[0][0] + 256 * 3, &scene.lutFrom[0][0]);
            scene.lutFade = 0;
        } else {
            std::copy(&scene.lutTo[0][0], &scene.lutTo[0][0] + 256 * 3, &scene.lut[0][0]);
            scene.lutFade = 1;
            scene.hasColors = true;
            uploadPalette(scene);
        }
    }

    for (auto &layer : scene.p.layers) {
        for (auto it = layer.colors.begin(); it != layer.colors.end(); ++it) {
            if (it->kind != ColorSpec::Theme)
                continue;
            const QColor color = presets::resolveColor(it->theme, m_theme);
            it->rgb[0] = color.redF();
            it->rgb[1] = color.greenF();
            it->rgb[2] = color.blueF();
        }
    }
}

void Engine::uploadPalette(Scene &scene) {
    unsigned char pixels[256 * 4];
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c)
            pixels[i * 4 + c] = static_cast<unsigned char>(clampTo(scene.lut[i][c], 0.f, 1.f) * 255.f + 0.5f);
        pixels[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, scene.paletteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void Engine::sampleAt(double t, float out[3]) const {
    if (!m_colors) {
        out[0] = out[1] = out[2] = 1;
        return;
    }
    const double position = (t - std::floor(t)) * 256.0;
    const int index = int(position) & 255;
    const float f = float(position - int(position));
    const float *a = m_colors->lut[index], *b = m_colors->lut[(index + 1) & 255];
    for (int c = 0; c < 3; ++c)
        out[c] = a[c] + (b[c] - a[c]) * f;
}

void Engine::evalColor(const ColorSpec *spec, float out[3]) {
    auto &values = m_c->p.vars.values;
    switch (spec->kind) {
    case ColorSpec::Constant:
    case ColorSpec::Theme:
        out[0] = spec->rgb[0];
        out[1] = spec->rgb[1];
        out[2] = spec->rgb[2];
        break;
    case ColorSpec::Palette:
        sampleAt(spec->a.run(values), out);
        break;
    case ColorSpec::Hsv:
        hsvToRgb(spec->a.run(values), spec->b.run(values), spec->c.run(values), out);
        break;
    case ColorSpec::Rgb:
        out[0] = float(spec->a.run(values));
        out[1] = float(spec->b.run(values));
        out[2] = float(spec->c.run(values));
        break;
    }
}

void Engine::uploadVariables(GLuint program, const Scene &scene) {
    const auto &values = scene.p.vars.values;
    m_uniforms.resize(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        m_uniforms[i] = float(values[i]);
    glUniform1fv(glGetUniformLocation(program, "V"), GLsizei(m_uniforms.size()), m_uniforms.data());
}

// The frame's variables for one scene: music first, then its own formulas.
void Engine::updateScene(Scene &scene, double progress) {
    if (scene.colorsDirty)
        rebuildColors(scene);
    if (scene.lutFade < 1 && scene.paletteTexture) {
        scene.lutFade = std::min(1.f, scene.lutFade + float(m_dt) / 2.5f);
        const float f = scene.lutFade * scene.lutFade * (3 - 2 * scene.lutFade);
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 3; ++c)
                scene.lut[i][c] = scene.lutFrom[i][c] + (scene.lutTo[i][c] - scene.lutFrom[i][c]) * f;
        uploadPalette(scene);
    }

    auto &V = scene.p.vars.values;
    const Slots &s = scene.s;
    V[s.time] = m_time;
    V[s.dt] = m_dt;
    V[s.frame] = double(m_frame);
    V[s.fps] = m_fps;
    V[s.bass] = m_audio.bass;
    V[s.mid] = m_audio.mid;
    V[s.treb] = m_audio.treb;
    V[s.vol] = m_audio.vol;
    V[s.bassAtt] = m_audio.bassAtt;
    V[s.midAtt] = m_audio.midAtt;
    V[s.trebAtt] = m_audio.trebAtt;
    V[s.volAtt] = m_audio.volAtt;
    for (int band = 0; band < AudioFrame::Bands; ++band) {
        V[s.band[band]] = m_audio.band[band];
        V[s.bandAtt[band]] = m_audio.bandAtt[band];
    }
    V[s.beat] = m_audio.beat;
    V[s.beats] = m_audio.beats;
    V[s.aspect] = m_aspect;
    V[s.progress] = progress;
    if (!scene.ranInit) {
        scene.p.init.run(V);
        scene.ranInit = true;
    }
    for (const auto &[slot, program] : scene.feedbackPrograms)
        V[slot] = program->run(V);
    scene.p.frame.run(V);
    for (const auto &[slot, program] : scene.postPrograms)
        V[slot] = program->run(V);
    for (const auto &[slot, program] : scene.backgroundPrograms)
        V[slot] = program->run(V);
    scene.warpTime += m_dt * V[s.warpSpeed];
    scene.backgroundTime += m_dt * V[s.bgSpeed];
    scene.waveSpots.clear();
    scene.barSpots.clear();
}

// Visitors come and go by chance, the way G-Force's particles do: once a
// second the dice are rolled, with worse odds the more of them are already
// here, and whoever turns up stays for a while and fades out again.
void Engine::manageVisitors() {
    for (size_t i = 0; i < m_visitors.size();) {
        m_visitors[i]->age += m_dt;
        if (m_visitors[i]->age >= m_visitors[i]->life || m_visitorRate <= 0) {
            destroyScene(m_visitors[i]);
            m_visitors.erase(m_visitors.begin() + long(i));
        } else {
            ++i;
        }
    }
    m_visitorCheck += m_dt;
    if (m_visitorCheck >= 1) {
        m_visitorCheck = std::fmod(m_visitorCheck, 1.0);
        const double here = double(m_visitors.size());
        const double chance = 0.07 * m_visitorRate / std::pow(here + 1, 1.66);
        if (!m_visitorPool.isEmpty() && m_visitors.size() < 3 && double(random()) < chance) {
            int index = int(random() * float(m_visitorPool.size())) % int(m_visitorPool.size());
            if (index == m_lastVisitor && m_visitorPool.size() > 1)
                index = (index + 1) % int(m_visitorPool.size());
            m_lastVisitor = index;
            if (auto visitor = makeScene(m_visitorPool.at(index), false, nullptr)) {
                visitor->life = 9 + 14 * double(random());
                m_visitors.push_back(std::move(visitor));
            }
        }
    }
    for (auto &visitor : m_visitors)
        updateScene(*visitor, visitor->age / visitor->life);
}

// ------------------------------------------------------------------ a frame

void Engine::render(const AudioFrame &audio, double dt, unsigned targetFbo, int width, int height) {
    if (!m_initialized)
        initialize();
    if (width < 1 || height < 1)
        return;
    dt = clampTo(dt, 1e-4, 0.1);
    m_lastVertices = m_frameVertices;
    m_frameVertices = 0;
    m_audio = audio;
    m_dt = dt;
    m_time += dt;
    ++m_frame;
    m_fps += (1.0 / dt - m_fps) * 0.05;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // The picture is computed at the window's size, but never taller than
    // 1440 lines: past that the cost climbs much faster than anything the
    // eye can see in a feedback image. The quality setting scales what is
    // left, so that it lightens the load on a large screen too.
    const double scale = std::min(1.0, 1440.0 / height) * clampTo(m_quality, 0.25, 1.0);
    const int innerWidth = std::max(16, int(std::lround(width * scale)));
    const int innerHeight = std::max(16, int(std::lround(height * scale)));
    const bool resized = innerWidth != m_width || innerHeight != m_height;
    m_width = innerWidth;
    m_height = innerHeight;
    m_aspect = float(innerWidth) / float(innerHeight);
    for (Target &buffer : m_buffers) {
        if (!buffer.fbo || !resized) {
            ensureTarget(buffer, m_width, m_height, m_wide, GL_LINEAR);
            continue;
        }
        // Resizing the window (or opening a panel beside the picture)
        // stretches what was there instead of starting over from black.
        Target old = buffer;
        buffer = Target();
        ensureTarget(buffer, m_width, m_height, m_wide, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, old.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer.fbo);
        glBlitFramebuffer(0, 0, old.width, old.height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        releaseTarget(old);
    }
    for (Target &bloom : m_bloom)
        ensureTarget(bloom, std::max(8, m_width / 4), std::max(8, m_height / 4), m_wide, GL_LINEAR);
    // The two buffers a change of preset needs are kept ready all along,
    // about 44 MB of graphics memory at 1440 lines. Making them when a
    // change begins is a pause exactly there.
    ensureTarget(m_other, m_width, m_height, m_wide, GL_LINEAR);
    ensureTarget(m_snapshot, m_width, m_height, false, GL_LINEAR);

    if (m_hasPending)
        startPending();
    collectBuilt();
    if (!m_scene) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, m_glass ? 0 : 1);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // How far a change of preset has come. It moves steadily at first and
    // glides into place at the end, which is G-Force's transition curve.
    float mix = 1;
    if (m_old) {
        m_blend += m_blendRate * float(dt);
        if (m_blend >= 1) {
            m_blend = 1;
            destroyScene(m_old);
        } else {
            // It sets off gently, moves steadily through the middle and
            // glides into place, so neither end of a slow change is a jolt.
            mix = m_blend * m_blend * (3 - 2 * m_blend);
        }
    }

    if (m_old)
        updateScene(*m_old, m_progress);
    updateScene(*m_scene, m_progress);
    manageVisitors();

    // 1. Warp last frame into this one. During a change the old preset
    // bends it its way into a side buffer, and the new preset's pass takes
    // from that wherever the old one still holds.
    const Target &previous = m_buffers[m_current];
    const Target &current = m_buffers[1 - m_current];
    if (m_old) {
        warpScene(*m_old, previous, m_other, 1, 0);
        warpScene(*m_scene, previous, current, mix, m_other.texture);
    } else {
        warpScene(*m_scene, previous, current, 1, 0);
    }

    // 2. Draw the layers onto it. Textured shapes sample last frame.
    glBindFramebuffer(GL_FRAMEBUFFER, current.fbo);
    glViewport(0, 0, m_width, m_height);
    glUseProgram(m_prim);
    glUniform1f(glGetUniformLocation(m_prim, "u_aspect"), m_aspect);
    glUniform1i(glGetUniformLocation(m_prim, "u_tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previous.texture);
    glBindVertexArray(m_primVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnable(GL_BLEND);
    m_stash.clear();
    m_stashUsed = 0;
    m_mix = mix;
    if (m_old) {
        // The old preset's waves are held back: each is drawn once, as a
        // blend of itself and the wave replacing it.
        m_layerAlpha = 1 - mix;
        drawLayers(*m_old, true, false);
    }
    m_layerAlpha = m_old ? mix : 1;
    if (m_scene->p.feedback.flag("darkenCenter", m_scene->p.vars.values))
        darkenCenter();
    drawLayers(*m_scene, false, bool(m_old));
    m_layerAlpha = 1 - mix;
    for (size_t i = m_stashUsed; i < m_stash.size(); ++i) {
        // Old waves the new preset has no wave for fade out as they were.
        m_c = m_old.get();
        emitWave(m_stash[i]);
        flush(m_stash[i].additive);
    }
    for (auto &visitor : m_visitors) {
        const double age = visitor->age, left = visitor->life - age;
        const float in = clampTo(float(age / 1.5), 0.f, 1.f), out = clampTo(float(left / 2.0), 0.f, 1.f);
        m_layerAlpha = in * in * (3 - 2 * in) * out * out * (3 - 2 * out);
        drawLayers(*visitor, false, false);
    }
    m_layerAlpha = 1;
    drawText();
    glDisable(GL_BLEND);
    glBindVertexArray(m_emptyVao);

    // 3. Bloom: keep what is brighter than the threshold, at quarter size,
    // and blur it twice, the second time wider.
    float threshold = float(m_scene->p.post.num("bloomThreshold", m_scene->p.vars.values));
    if (m_old)
        threshold += (float(m_old->p.post.num("bloomThreshold", m_old->p.vars.values)) - threshold) * (1 - mix);
    glViewport(0, 0, m_bloom[0].width, m_bloom[0].height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloom[0].fbo);
    glUseProgram(m_bright);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current.texture);
    glUniform1i(glGetUniformLocation(m_bright, "u_tex"), 0);
    glUniform2f(glGetUniformLocation(m_bright, "u_texel"), 1.f / m_width, 1.f / m_height);
    glUniform1f(glGetUniformLocation(m_bright, "u_threshold"), threshold);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(m_blur);
    glUniform1i(glGetUniformLocation(m_blur, "u_tex"), 0);
    for (int pass = 0; pass < 4; ++pass) {
        const Target &from = m_bloom[pass % 2], &to = m_bloom[1 - pass % 2];
        const float reach = pass < 2 ? 1.f : 2.2f;
        glBindFramebuffer(GL_FRAMEBUFFER, to.fbo);
        glBindTexture(GL_TEXTURE_2D, from.texture);
        glUniform2f(glGetUniformLocation(m_blur, "u_step"), pass % 2 == 0 ? reach / from.width : 0.f,
                    pass % 2 == 0 ? 0.f : reach / from.height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // 4. To the screen. During a change the old preset's look is rendered
    // aside first, and the new one's pass shows it wherever it still holds.
    m_current = 1 - m_current;
    if (m_old) {
        compositeScene(*m_old, m_snapshot.fbo, m_width, m_height, 1, 0, false);
        compositeScene(*m_scene, targetFbo, width, height, mix, m_snapshot.texture, m_glass);
    } else {
        compositeScene(*m_scene, targetFbo, width, height, 1, 0, m_glass);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
}

void Engine::warpScene(Scene &scene, const Target &from, const Target &to, float blend, GLuint other) {
    glBindFramebuffer(GL_FRAMEBUFFER, to.fbo);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_BLEND);
    glUseProgram(scene.warp);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, other ? other : from.texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, from.texture);
    // What lies beyond the screen's edge is this preset's choice.
    const QString edge = scene.p.feedback.choice("edge");
    const GLint mode = edge == "wrap" ? GL_REPEAT : edge == "mirror" ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
    glUniform1i(glGetUniformLocation(scene.warp, "u_prev"), 0);
    glUniform1i(glGetUniformLocation(scene.warp, "u_other"), 1);
    glUniform1f(glGetUniformLocation(scene.warp, "u_aspect"), m_aspect);
    glUniform1f(glGetUniformLocation(scene.warp, "u_k"), float(m_dt * 60.0));
    glUniform1f(glGetUniformLocation(scene.warp, "u_warpTime"), float(scene.warpTime));
    glUniform1f(glGetUniformLocation(scene.warp, "u_seed"), random());
    glUniform2f(glGetUniformLocation(scene.warp, "u_texel"), 1.f / m_width, 1.f / m_height);
    glUniform1f(glGetUniformLocation(scene.warp, "u_wide"), m_wide ? 1.f : 0.f);
    glUniform1f(glGetUniformLocation(scene.warp, "u_blend"), blend);
    glUniform1f(glGetUniformLocation(scene.warp, "u_pattern"), float(m_pattern));
    glUniform1f(glGetUniformLocation(scene.warp, "u_patternSeed"), m_patternSeed);
    uploadVariables(scene.warp, scene);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Engine::compositeScene(Scene &scene, GLuint fbo, int width, int height, float blend, GLuint snapshot,
                            bool glass) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glUseProgram(scene.composite);
    // The old preset's frame is only bound while it is needed; a sampler
    // must not be left pointing at the buffer being drawn to.
    const GLuint textures[4] = {m_buffers[m_current].texture, m_bloom[0].texture, scene.paletteTexture,
                                snapshot ? snapshot : scene.paletteTexture};
    const char *const names[4] = {"u_tex", "u_bloom", "u_palette", "u_snapshot"};
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glUniform1i(glGetUniformLocation(scene.composite, names[i]), i);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(glGetUniformLocation(scene.composite, "u_aspect"), m_aspect);
    glUniform1f(glGetUniformLocation(scene.composite, "u_seed"), random());
    glUniform1f(glGetUniformLocation(scene.composite, "u_blend"), blend);
    glUniform1f(glGetUniformLocation(scene.composite, "u_pattern"), float(m_pattern));
    glUniform1f(glGetUniformLocation(scene.composite, "u_patternSeed"), m_patternSeed);
    glUniform1f(glGetUniformLocation(scene.composite, "u_glass"), glass ? 1.f : 0.f);
    glUniform1f(glGetUniformLocation(scene.composite, "u_glassFloor"), clampTo(float(m_glassOpacity), 0.f, 1.f));
    glUniform1f(glGetUniformLocation(scene.composite, "u_paletteSpan"), scene.paletteSpan);
    glUniform1f(glGetUniformLocation(scene.composite, "u_bgTime"), float(scene.backgroundTime));
    uploadVariables(scene.composite, scene);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Blends two waves into one: the same number of points, each partway from
// where the old wave has it to where the new one does.
static void sampleWave(const std::vector<float> &source, int stride, double position, float *out) {
    const int count = int(source.size()) / stride;
    position = std::min(std::max(position, 0.0), double(count - 1));
    const int index = std::min(int(position), count - 2 < 0 ? 0 : count - 2);
    const float f = count > 1 ? float(position - index) : 0.f;
    const float *a = &source[size_t(index * stride)];
    const float *b = &source[size_t(std::min(index + 1, count - 1) * stride)];
    for (int c = 0; c < stride; ++c)
        out[c] = a[c] + (b[c] - a[c]) * f;
}

void Engine::drawLayers(Scene &scene, bool stashWaves, bool morphWaves) {
    m_c = &scene;
    m_colors = scene.paletteTexture || !m_scene ? &scene : m_scene.get();
    auto &V = scene.p.vars.values;
    const QList<Block> &layers = scene.p.layers;
    const Slots &s = scene.s;
    for (qsizetype i = 0; i < layers.size(); ++i) {
        const Block &layer = layers.at(i);
        // A layer's own settings are worked out once, before its points, so
        // these hold 0 there rather than what the last layer's points left.
        V[size_t(s.t)] = V[size_t(s.i)] = V[size_t(s.v)] = V[size_t(s.v2)] = 0;
        if (!layer.flag("enabled", V))
            continue;
        const bool additive = layer.flag("additive", V);
        LayerState &state = scene.layers[size_t(i)];
        if (layer.type == "wave") {
            buildWave(layer, m_wave);
            m_wave.additive = additive;
            if (stashWaves) {
                m_stash.push_back(m_wave);
                continue;
            }
            if (morphWaves && m_stashUsed < m_stash.size()) {
                const WaveShape &from = m_stash[m_stashUsed++];
                const float w = m_mix;
                // A ring and a line meet as two open curves, so the ring
                // unrolls instead of snapping open halfway.
                static thread_local std::vector<float> a, b;
                const auto flatten = [](const WaveShape &shape, bool close, std::vector<float> &into) {
                    into.clear();
                    for (const Point &p : shape.points)
                        into.insert(into.end(), {p.x, p.y, p.r, p.g, p.b, p.a});
                    if (close && !shape.points.empty()) {
                        const Point &p = shape.points.front();
                        into.insert(into.end(), {p.x, p.y, p.r, p.g, p.b, p.a});
                    }
                };
                const bool bothClosed = from.closed && m_wave.closed;
                flatten(from, from.closed && !bothClosed, a);
                flatten(m_wave, m_wave.closed && !bothClosed, b);
                if (a.size() >= 12 && b.size() >= 12) {
                    const int count = int(std::max(a.size(), b.size()) / 6);
                    m_morph.points.resize(size_t(count));
                    for (int k = 0; k < count; ++k) {
                        const double t = double(k) / double(count - 1);
                        float pa[6], pb[6];
                        sampleWave(a, 6, t * double(a.size() / 6 - 1), pa);
                        sampleWave(b, 6, t * double(b.size() / 6 - 1), pb);
                        float p[6];
                        for (int c = 0; c < 6; ++c)
                            p[c] = pa[c] + (pb[c] - pa[c]) * w;
                        m_morph.points[size_t(k)] = Point{p[0], p[1], p[2], p[3], p[4], p[5]};
                    }
                    const WaveShape &lead = w < 0.5f ? from : m_wave;
                    m_morph.closed = bothClosed;
                    m_morph.dots = lead.dots;
                    m_morph.mirror = lead.mirror;
                    m_morph.additive = lead.additive;
                    m_morph.halfWidth = from.halfWidth + (m_wave.halfWidth - from.halfWidth) * w;
                    const float keep = m_layerAlpha;
                    m_layerAlpha = 1;
                    emitWave(m_morph);
                    flush(m_morph.additive);
                    m_layerAlpha = keep;
                    continue;
                }
            }
            emitWave(m_wave);
        } else if (layer.type == "bars") {
            drawBars(layer, state);
        } else if (layer.type == "shape") {
            drawShape(layer);
        } else if (layer.type == "terrain") {
            drawTerrain(layer, state);
        } else if (layer.type == "wireframe") {
            drawWireframe(layer, state);
        } else if (layer.type == "particles") {
            drawParticles(layer, state);
        } else if (layer.type == "vectors") {
            drawVectors(layer);
        }
        flush(additive);
    }
}

// The track's name, written into the picture itself for a few seconds. The
// feedback takes hold of it like anything else drawn there, so the letters
// stream away the way the rest of the picture is moving.
void Engine::drawText() {
    if (m_textPending) {
        m_textPending = false;
        if (!m_textTexture)
            glGenTextures(1, &m_textTexture);
        glBindTexture(GL_TEXTURE_2D, m_textTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, int(m_textImage.bytesPerLine() / 4));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_textImage.width(), m_textImage.height(), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, m_textImage.constBits());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        m_textAspect = float(m_textImage.width()) / float(std::max(1, m_textImage.height()));
        m_textImage = QImage();
        m_textAge = 0;
        glBindTexture(GL_TEXTURE_2D, m_buffers[m_current].texture);
    }
    const float duration = 3.6f;
    if (m_textAge >= duration || !m_textTexture)
        return;
    const float in = clampTo(m_textAge / 0.35f, 0.f, 1.f), out = clampTo((duration - m_textAge) / 1.2f, 0.f, 1.f);
    m_textAge += float(m_dt);

    float height = 0.3f, width = height * m_textAspect;
    const float room = 1.8f * m_aspect;
    if (width > room) {
        height *= room / width;
        width = room;
    }
    float tint[3];
    m_colors = m_scene.get();
    sampleAt(m_time * 0.05, tint);
    const float rgba[4] = {0.55f + 0.45f * tint[0], 0.55f + 0.45f * tint[1], 0.55f + 0.45f * tint[2],
                           0.9f * in * out};
    const float left = -width / 2, right = width / 2, bottom = -height / 2, top = height / 2;
    // The image's first row is its top; a texture's is its bottom.
    vertex(left, bottom, rgba, 0, 0, 0, 1);
    vertex(right, bottom, rgba, 0, 0, 1, 1);
    vertex(right, top, rgba, 0, 0, 1, 0);
    vertex(left, bottom, rgba, 0, 0, 0, 1);
    vertex(right, top, rgba, 0, 0, 1, 0);
    vertex(left, top, rgba, 0, 0, 0, 0);
    glBindTexture(GL_TEXTURE_2D, m_textTexture);
    flush(false);
    glBindTexture(GL_TEXTURE_2D, m_buffers[m_current].texture);
}

// The feedback pass asks, for every pixel, where its color comes from. This
// asks the same of one point on the CPU and turns the answer round: if the
// color here came from over there, then what is here is headed the same
// distance the other way. Particles that follow it ride the picture.
void Engine::flowAt(Scene &scene, float x, float y, float *toX, float *toY) {
    const Slots &s = scene.s;
    const std::vector<double> *V = &scene.p.vars.values;
    double px = x, py = y;
    const double rad = std::hypot(double(x), double(y));
    if (!scene.p.pixel.isEmpty()) {
        scene.scratch = *V;
        scene.scratch[size_t(s.x)] = x;
        scene.scratch[size_t(s.y)] = y;
        scene.scratch[size_t(s.rad)] = rad;
        if (scene.pixelReadsAngle)
            scene.scratch[size_t(s.ang)] = (x == 0 && y == 0) ? 0.0 : std::atan2(double(y), double(x));
        scene.p.pixel.run(scene.scratch);
        V = &scene.scratch;
        px = (*V)[size_t(s.x)];
        py = (*V)[size_t(s.y)];
    }
    const auto value = [&](int slot) { return (*V)[size_t(slot)]; };
    const double k = m_dt * 60.0;
    // What follows from the settings alone, worked out again only when one
    // of them differs from the last point's.
    auto &memo = scene.flow;
    const double zoom = std::max(value(s.zoom), 0.01), zoomexp = std::max(value(s.zoomexp), 0.01);
    const double sx = std::max(value(s.sx), 0.01), sy = std::max(value(s.sy), 0.01), rot = value(s.rot);
    if (zoom != memo.zoom || sx != memo.sx || sy != memo.sy || rot != memo.rot || k != memo.k) {
        memo.zoom = zoom;
        memo.sx = sx;
        memo.sy = sy;
        memo.rot = rot;
        memo.k = k;
        memo.zoomK = std::pow(zoom, k);
        memo.sxK = std::pow(sx, k);
        memo.syK = std::pow(sy, k);
        const double a = -rot * k;
        memo.ca = std::cos(a);
        memo.sa = std::sin(a);
    }
    // Any power of 1 is exactly 1, so without zoomexp the zoom is the same
    // everywhere.
    const double z = zoomexp == 1 ? memo.zoomK : std::pow(zoom, std::pow(zoomexp, rad * 2 - 1) * k);
    const double cx = value(s.cx), cy = value(s.cy);
    double qx = (px - cx) / z / memo.sxK;
    double qy = (py - cy) / z / memo.syK;
    const double ca = memo.ca, sa = memo.sa;
    const double rx = qx * ca - qy * sa, ry = qx * sa + qy * ca;
    qx = rx + cx - value(s.dx) * k;
    qy = ry + cy - value(s.dy) * k;
    const double warp = value(s.warp);
    if (warp != 0) {
        const double w = warp * k * 0.007, t = scene.warpTime;
        if (t != memo.warpTime) {
            memo.warpTime = t;
            memo.f0 = 11.68 + 4.0 * std::cos(t * 1.413 + 10.0);
            memo.f1 = 8.77 + 3.0 * std::cos(t * 1.113 + 7.0);
            memo.f2 = 10.54 + 3.0 * std::cos(t * 1.233 + 3.0);
            memo.f3 = 11.49 + 4.0 * std::cos(t * 0.933 + 5.0);
        }
        const double f0 = memo.f0, f1 = memo.f1, f2 = memo.f2, f3 = memo.f3;
        const double scale = 0.5 / std::max(value(s.warpScale), 0.01), mx = px * scale, my = py * scale;
        qx += w * std::sin(t * 0.333 + (mx * f0 - my * f3));
        qy += w * std::cos(t * 0.375 - (mx * f2 + my * f1));
        qx += w * std::cos(t * 0.753 - (mx * f1 - my * f2));
        qy += w * std::sin(t * 0.825 + (mx * f0 + my * f3));
    }
    *toX = float(2 * double(x) - qx);
    *toY = float(2 * double(y) - qy);
}

// ----------------------------------------------------------------- geometry

void Engine::flush(bool additive) {
    if (!m_used)
        return;
    if (additive)
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    else
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    m_frameVertices += int(m_used / 10);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_used * sizeof(float)), m_vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(m_used / 10));
    m_used = 0;
}

// A line of even width with mitred corners, as triangles. Mitres mean no
// segment overlaps its neighbour, so see-through and additive lines don't
// get bright knots at every joint.
void Engine::polyline(const std::vector<Point> &points, bool closed, float halfWidth) {
    const int n = int(points.size());
    if (n < 2)
        return;
    halfWidth = std::max(halfWidth, 1.f / float(m_height)) + 2.f / float(m_height);
    static thread_local std::vector<float> offsets;
    offsets.resize(size_t(n) * 2);
    for (int i = 0; i < n; ++i) {
        const Point &here = points[i];
        const Point &before = points[i > 0 ? i - 1 : (closed ? n - 1 : 0)];
        const Point &after = points[i < n - 1 ? i + 1 : (closed ? 0 : n - 1)];
        float ax = here.x - before.x, ay = here.y - before.y;
        float bx = after.x - here.x, by = after.y - here.y;
        const float al = std::sqrt(ax * ax + ay * ay), bl = std::sqrt(bx * bx + by * by);
        if (al > 1e-7f) { ax /= al; ay /= al; } else { ax = ay = 0; }
        if (bl > 1e-7f) { bx /= bl; by /= bl; } else { bx = ax; by = ay; }
        if (al <= 1e-7f) { ax = bx; ay = by; }
        float tx = ax + bx, ty = ay + by;
        const float tl = std::sqrt(tx * tx + ty * ty);
        if (tl > 1e-5f) { tx /= tl; ty /= tl; } else { tx = -ay; ty = ax; }
        // How far the corner has to reach out to keep the line's width.
        const float cosine = tx * ax + ty * ay;
        const float reach = halfWidth / std::max(cosine, 0.4f);
        offsets[size_t(i) * 2] = -ty * reach;
        offsets[size_t(i) * 2 + 1] = tx * reach;
    }
    const int segments = closed ? n : n - 1;
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % n;
        const Point &a = points[i], &b = points[j];
        const float aox = offsets[size_t(i) * 2], aoy = offsets[size_t(i) * 2 + 1];
        const float box = offsets[size_t(j) * 2], boy = offsets[size_t(j) * 2 + 1];
        vertex(a.x + aox, a.y + aoy, &a.r, 1, 0);
        vertex(a.x - aox, a.y - aoy, &a.r, -1, 0);
        vertex(b.x + box, b.y + boy, &b.r, 1, 0);
        vertex(a.x - aox, a.y - aoy, &a.r, -1, 0);
        vertex(b.x - box, b.y - boy, &b.r, -1, 0);
        vertex(b.x + box, b.y + boy, &b.r, 1, 0);
    }
}

void Engine::dot(const Point &p, float halfSize) {
    halfSize = std::max(halfSize, 1.f / float(m_height)) + 2.f / float(m_height);
    const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (const auto &corner : corners)
        vertex(p.x + corner[0] * halfSize, p.y + corner[1] * halfSize, &p.r, corner[0], corner[1]);
}

// ------------------------------------------------------------------- layers

// Works a wave layer out into points, without drawing it yet: during a
// change of preset the points are blended with another wave's first.
void Engine::buildWave(const Block &layer, WaveShape &out) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const int count = clampTo(int(layer.num("samples", V)), 8, AudioFrame::WaveSize);
    const QString source = layer.choice("source"), shape = layer.choice("shape");
    const float scale = float(layer.num("scale", V)), smooth = clampTo(float(layer.num("smooth", V)), 0.f, 0.98f);
    const float x0 = float(layer.num("x", V)), y0 = float(layer.num("y", V));
    const float width = float(layer.num("width", V)), radius = float(layer.num("radius", V));
    const float angle = float(layer.num("angle", V)), alpha = float(layer.num("alpha", V));
    const ColorSpec *color = layer.color("color");
    const expr::Program *code = layer.program("point");
    out.halfWidth = float(layer.num("thickness", V)) / 1080.f;
    out.dots = layer.flag("dots", V);
    out.mirror = layer.choice("mirror");

    static thread_local std::vector<float> first, second;
    first.resize(size_t(count));
    second.resize(size_t(count));
    if (source == "spectrum") {
        resample(m_audio.spectrum, AudioFrame::SpectrumSize, first, true);
        second = first;
    } else {
        const int channel = source == "left" ? 1 : source == "right" ? 2 : 0;
        resample(m_audio.wave[shape == "xy" ? 1 : channel], AudioFrame::WaveSize, first, false);
        resample(m_audio.wave[shape == "xy" ? 2 : (channel == 1 ? 2 : 1)], AudioFrame::WaveSize, second, false);
    }
    if (smooth > 0) {
        // Forward then backward, so smoothing doesn't shift the wave sideways.
        for (auto *values : {&first, &second}) {
            for (int i = 1; i < count; ++i)
                (*values)[size_t(i)] = (*values)[size_t(i)] * (1 - smooth) + (*values)[size_t(i - 1)] * smooth;
            for (int i = count - 2; i >= 0; --i)
                (*values)[size_t(i)] = (*values)[size_t(i)] * (1 - smooth) + (*values)[size_t(i + 1)] * smooth;
        }
    }
    out.closed = shape == "circle";
    if (out.closed) {
        // Ease the end of the wave into its start so the ring has no step.
        const int blend = std::max(2, count / 10);
        for (int i = 0; i < blend; ++i) {
            const float f = float(i + 1) / float(blend + 1);
            first[size_t(count - blend + i)] += (first[0] - first[size_t(count - blend + i)]) * f;
        }
    }

    const float ca = std::cos(angle), sa = std::sin(angle);
    // Told apart once, not at every point.
    enum { Line, Circle, Xy, Still } const form =
        shape == "line" ? Line : shape == "circle" ? Circle : shape == "xy" ? Xy : Still;
    const bool mirrorX = out.mirror == "x" || out.mirror == "quad", mirrorY = out.mirror == "y" || out.mirror == "quad";
    const bool mirrorBoth = out.mirror == "quad";
    out.points.clear();
    out.levels.clear();
    for (int i = 0; i < count; ++i) {
        const float t = out.closed ? float(i) / float(count) : float(i) / float(count - 1);
        const float value = first[size_t(i)];
        float x = 0, y = 0;
        if (form == Line) {
            const float lx = (t * 2 - 1) * m_aspect * width, ly = value * scale;
            x = x0 + lx * ca - ly * sa;
            y = y0 + lx * sa + ly * ca;
        } else if (form == Circle) {
            const float r = radius + value * scale, a = angle + t * Tau;
            x = x0 + r * std::cos(a);
            y = y0 + r * std::sin(a);
        } else if (form == Xy) {
            const float lx = value * scale, ly = second[size_t(i)] * scale;
            x = x0 + lx * ca - ly * sa;
            y = y0 + lx * sa + ly * ca;
        } else {
            x = x0;
            y = y0;
        }
        V[m_s.t] = t;
        V[m_s.i] = i;
        V[m_s.v] = value;
        V[m_s.v2] = second[size_t(i)];
        float rgb[3];
        evalColor(color, rgb);
        Point point{x, y, rgb[0], rgb[1], rgb[2], alpha};
        if (code) {
            V[m_s.x] = x;
            V[m_s.y] = y;
            V[m_s.r] = rgb[0];
            V[m_s.g] = rgb[1];
            V[m_s.b] = rgb[2];
            V[m_s.a] = alpha;
            code->run(V);
            point = Point{float(V[m_s.x]), float(V[m_s.y]), float(V[m_s.r]), float(V[m_s.g]), float(V[m_s.b]),
                          float(V[m_s.a])};
        }
        out.points.push_back(point);
        out.levels.push_back(std::abs(value));
        // Particles born "at the wave" come from wherever it is right now.
        m_c->waveSpots.push_back(Spot{point.x, point.y, std::abs(value)});
        if (mirrorX)
            m_c->waveSpots.push_back(Spot{-point.x, point.y, std::abs(value)});
        if (mirrorY)
            m_c->waveSpots.push_back(Spot{point.x, -point.y, std::abs(value)});
        if (mirrorBoth)
            m_c->waveSpots.push_back(Spot{-point.x, -point.y, std::abs(value)});
    }
}

void Engine::emitWave(const WaveShape &shape) {
    auto draw = [&](float flipX, float flipY) {
        static thread_local std::vector<Point> flipped;
        const std::vector<Point> *points = &shape.points;
        if (flipX < 0 || flipY < 0) {
            flipped = shape.points;
            for (Point &p : flipped) {
                p.x *= flipX;
                p.y *= flipY;
            }
            points = &flipped;
        }
        if (shape.dots) {
            for (const Point &p : *points)
                dot(p, shape.halfWidth);
        } else {
            polyline(*points, shape.closed, shape.halfWidth);
        }
    };
    draw(1, 1);
    if (shape.mirror == "x" || shape.mirror == "quad")
        draw(-1, 1);
    if (shape.mirror == "y" || shape.mirror == "quad")
        draw(1, -1);
    if (shape.mirror == "quad")
        draw(-1, -1);
}

void Engine::drawBars(const Block &layer, LayerState &state) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const int count = clampTo(int(layer.num("count", V)), 4, 128);
    const QString layout = layer.choice("layout");
    const float x0 = float(layer.num("x", V)), y0 = float(layer.num("y", V));
    const float width = float(layer.num("width", V)), height = float(layer.num("height", V));
    const float gap = clampTo(float(layer.num("gap", V)), 0.f, 0.95f), radius = float(layer.num("radius", V));
    const float angle = float(layer.num("angle", V)), alpha = float(layer.num("alpha", V));
    const float fade = clampTo(float(layer.num("fade", V)), 0.f, 1.f);
    const bool peaks = layer.flag("peaks", V), symmetric = layer.flag("symmetric", V);
    const ColorSpec *color = layer.color("color");
    const expr::Program *code = layer.program("point");

    // Symmetric: bass in the middle, treble out to both sides.
    static thread_local std::vector<float> bands;
    bands.resize(size_t(symmetric ? (count + 1) / 2 : count));
    resample(m_audio.spectrum, AudioFrame::SpectrumSize, bands, true);
    if (int(state.peaks.size()) != count)
        state.peaks.assign(size_t(count), 0.f);

    const float span = 2 * m_aspect * width, slot = span / float(count);
    const float pixel = 2.f / float(m_height);
    for (int i = 0; i < count; ++i) {
        const int band = symmetric ? std::abs(i * 2 + 1 - count) / 2 : i;
        const float t = count > 1 ? float(i) / float(count - 1) : 0;
        V[m_s.t] = t;
        V[m_s.i] = i;
        V[m_s.v] = bands[size_t(std::min(band, int(bands.size()) - 1))];
        V[m_s.v2] = V[m_s.v];
        float rgb[3];
        evalColor(color, rgb);
        V[m_s.r] = rgb[0];
        V[m_s.g] = rgb[1];
        V[m_s.b] = rgb[2];
        V[m_s.a] = alpha;
        if (code)
            code->run(V);
        const float value = std::max(0.f, float(V[m_s.v]));
        const float tip[4] = {float(V[m_s.r]), float(V[m_s.g]), float(V[m_s.b]), float(V[m_s.a])};
        const float base[4] = {tip[0] * (1 - fade), tip[1] * (1 - fade), tip[2] * (1 - fade), tip[3]};
        float &held = state.peaks[size_t(i)];
        held = std::max(value, held - float(m_dt) * 0.45f);

        // Each bar is a quad from (ax, ay)-(bx, by) at its foot to
        // (cx, cy)-(dx, dy) at its tip; e runs -1..1 across it for soft sides.
        auto quad = [&](float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                        const float *foot, const float *head) {
            vertex(ax, ay, foot, -1, 0);
            vertex(bx, by, foot, 1, 0);
            vertex(dx, dy, head, 1, 0);
            vertex(ax, ay, foot, -1, 0);
            vertex(dx, dy, head, 1, 0);
            vertex(cx, cy, head, -1, 0);
        };
        if (layout == "radial") {
            const float a = angle + (float(i) + 0.5f) / float(count) * Tau;
            const float half = Pi / float(count) * (1 - gap);
            const float inner = radius, outer = radius + value * height;
            auto ring = [&](float from, float to, const float *foot, const float *head) {
                quad(x0 + from * std::cos(a - half), y0 + from * std::sin(a - half),
                     x0 + from * std::cos(a + half), y0 + from * std::sin(a + half),
                     x0 + to * std::cos(a - half), y0 + to * std::sin(a - half),
                     x0 + to * std::cos(a + half), y0 + to * std::sin(a + half), foot, head);
            };
            ring(inner, outer, base, tip);
            m_c->barSpots.push_back(Spot{x0 + outer * std::cos(a), y0 + outer * std::sin(a), value});
            if (peaks)
                ring(radius + held * height + pixel * 2, radius + held * height + pixel * 5, tip, tip);
        } else {
            const float left = x0 - span / 2 + float(i) * slot + slot * gap / 2, right = left + slot * (1 - gap);
            const float direction = layout == "top" ? -1.f : 1.f;
            const float ground = layout == "center" ? y0 : (layout == "top" ? 1.f + y0 : -1.f + y0);
            const float reach = layout == "center" ? height : height * 2;
            auto column = [&](float from, float to, const float *foot, const float *head) {
                quad(left, ground + from * direction, right, ground + from * direction, left,
                     ground + to * direction, right, ground + to * direction, foot, head);
                if (layout == "center")
                    quad(left, ground - from, right, ground - from, left, ground - to, right, ground - to, foot, head);
            };
            column(0, value * reach, base, tip);
            m_c->barSpots.push_back(Spot{(left + right) / 2, ground + value * reach * direction, value});
            if (peaks)
                column(held * reach + pixel * 2, held * reach + pixel * 5, tip, tip);
        }
    }
}

void Engine::drawShape(const Block &layer) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const int instances = clampTo(int(layer.num("instances", V)), 1, 512);
    const int sides = clampTo(int(layer.num("sides", V)), 3, 64);
    const bool textured = layer.flag("textured", V);
    const ColorSpec *color = layer.color("color"), *color2 = layer.color("color2"), *border = layer.color("border");
    const expr::Program *code = layer.program("code");
    // Even-sided shapes sit flat (a square is a square, not a diamond); odd
    // ones point up.
    const float rest = Pi / 2 - (sides % 2 == 0 ? Pi / float(sides) : 0.f);

    for (int n = 0; n < instances; ++n) {
        V[m_s.n] = n;
        V[m_s.i] = n;
        V[m_s.count] = instances;
        V[m_s.t] = instances > 1 ? double(n) / (instances - 1) : 0.0;
        // Each copy also hears its own slice of the music: v is the spectrum
        // and v2 the waveform at the copy's place along the row.
        V[m_s.v] = m_audio.spectrum[int(V[m_s.t] * (AudioFrame::SpectrumSize - 1))];
        V[m_s.v2] = m_audio.wave[0][int(V[m_s.t] * (AudioFrame::WaveSize - 1))];
        V[m_s.x] = layer.num("x", V);
        V[m_s.y] = layer.num("y", V);
        V[m_s.radius] = layer.num("radius", V);
        V[m_s.angle] = layer.num("angle", V);
        float inner[3], outer[3], edge[3];
        evalColor(color, inner);
        evalColor(color2, outer);
        evalColor(border, edge);
        V[m_s.r] = inner[0];
        V[m_s.g] = inner[1];
        V[m_s.b] = inner[2];
        V[m_s.a] = layer.num("alpha", V);
        V[m_s.r2] = outer[0];
        V[m_s.g2] = outer[1];
        V[m_s.b2] = outer[2];
        V[m_s.a2] = layer.num("alpha2", V);
        if (code)
            code->run(V);
        const float x = float(V[m_s.x]), y = float(V[m_s.y]), radius = float(V[m_s.radius]);
        const float angle = float(V[m_s.angle]);
        const float center[4] = {float(V[m_s.r]), float(V[m_s.g]), float(V[m_s.b]), float(V[m_s.a])};
        const float rim[4] = {float(V[m_s.r2]), float(V[m_s.g2]), float(V[m_s.b2]), float(V[m_s.a2])};
        const float texZoom = std::max(0.05f, float(layer.num("texZoom", V)));
        const float texAngle = float(layer.num("texAngle", V));

        m_points.clear();
        for (int k = 0; k < sides; ++k) {
            const float a = angle + rest + float(k) / float(sides) * Tau;
            m_points.push_back(Point{x + radius * std::cos(a), y + radius * std::sin(a), 0, 0, 0, 0});
        }
        if (center[3] > 0 || rim[3] > 0) {
            for (int k = 0; k < sides; ++k) {
                const Point &p = m_points[size_t(k)], &q = m_points[size_t((k + 1) % sides)];
                if (textured) {
                    // The shape shows last frame's picture, zoomed and turned.
                    auto tex = [&](int corner, float *u, float *v) {
                        const float a = rest + float(corner) / float(sides) * Tau + texAngle;
                        *u = 0.5f + 0.5f * std::cos(a) / texZoom / m_aspect;
                        *v = 0.5f + 0.5f * std::sin(a) / texZoom;
                    };
                    float u1, v1, u2, v2;
                    tex(k, &u1, &v1);
                    tex(k + 1, &u2, &v2);
                    vertex(x, y, center, 0, 0, 0.5f, 0.5f);
                    vertex(p.x, p.y, rim, 1, 0, u1, v1);
                    vertex(q.x, q.y, rim, 1, 0, u2, v2);
                } else {
                    vertex(x, y, center, 0, 0);
                    vertex(p.x, p.y, rim, 1, 0);
                    vertex(q.x, q.y, rim, 1, 0);
                }
            }
        }
        const float borderAlpha = float(layer.num("borderAlpha", V));
        if (borderAlpha > 0) {
            for (Point &p : m_points) {
                p.r = edge[0];
                p.g = edge[1];
                p.b = edge[2];
                p.a = borderAlpha;
            }
            polyline(m_points, true, float(layer.num("borderWidth", V)) / 1080.f);
        }
    }
}

// The spectrum as a landscape scrolling away to a horizon, newest row in
// front: SoundSpectrum's WhiteCap is the reference for this one.
void Engine::drawTerrain(const Block &layer, LayerState &state) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const int rows = clampTo(int(layer.num("rows", V)), 8, 64), cols = clampTo(int(layer.num("cols", V)), 16, 128);
    const QString style = layer.choice("style");
    const float height = float(layer.num("height", V)), width = float(layer.num("width", V));
    const float front = float(layer.num("y", V)), horizon = float(layer.num("horizon", V));
    const float depth = std::max(0.05f, float(layer.num("depth", V)));
    const float halfWidth = float(layer.num("thickness", V)) / 1080.f, alpha = float(layer.num("alpha", V));
    const ColorSpec *color = layer.color("color");

    if (state.rows != rows || state.cols != cols) {
        state.rows = rows;
        state.cols = cols;
        state.history.assign(size_t(rows) * size_t(cols), 0.f);
        state.phase = 0;
    }
    static thread_local std::vector<float> live;
    live.resize(size_t(cols));
    resample(m_audio.spectrum, AudioFrame::SpectrumSize, live, true);
    state.phase += m_dt * layer.num("speed", V);
    int pushes = 0;
    while (state.phase >= 1 && pushes++ < rows) {
        std::copy_backward(state.history.begin(), state.history.end() - cols, state.history.end());
        std::copy(live.begin(), live.end(), state.history.begin());
        state.phase -= 1;
    }
    state.phase = std::fmod(state.phase, 1.0);

    // Row 0 is the live spectrum at the front; history rows slide back from
    // it by the phase, so the scroll is smooth between pushes.
    static thread_local std::vector<Point> grid;
    grid.resize(size_t(rows + 1) * size_t(cols));
    for (int row = 0; row <= rows; ++row) {
        const float d = row == 0 ? 0.f : (float(row - 1) + float(state.phase)) / float(rows);
        const float s = 1.f / (1.f + d * depth);
        const float fadeOut = alpha * std::pow(std::max(0.f, 1.f - d), 1.4f);
        const float *values = row == 0 ? live.data() : &state.history[size_t(row - 1) * size_t(cols)];
        for (int col = 0; col < cols; ++col) {
            const float t = float(col) / float(cols - 1);
            V[m_s.t] = t;
            V[m_s.i] = d;
            V[m_s.v] = values[col];
            float rgb[3];
            evalColor(color, rgb);
            grid[size_t(row) * size_t(cols) + size_t(col)] =
                Point{(t * 2 - 1) * width * s, horizon + (front - horizon) * s + values[col] * height * s, rgb[0],
                      rgb[1], rgb[2], fadeOut};
        }
    }
    for (int row = 0; row <= rows; ++row) {
        m_points.assign(grid.begin() + row * cols, grid.begin() + (row + 1) * cols);
        if (style == "dots") {
            for (const Point &p : m_points)
                dot(p, halfWidth * 1.5f);
        } else {
            polyline(m_points, false, halfWidth);
        }
    }
    if (style == "mesh") {
        for (int col = 0; col < cols; col += 2) {
            m_points.clear();
            for (int row = 0; row <= rows; ++row)
                m_points.push_back(grid[size_t(row) * size_t(cols) + size_t(col)]);
            polyline(m_points, false, halfWidth * 0.7f);
        }
    }
}

// SoundSpectrum's WhiteCap: the last few seconds of the spectrum as a sheet
// of lines hung in space. Across the sheet runs pitch, along it runs time,
// and a form says where in space each point of it goes: laid flat, rolled
// into a tube, spread into a ring or wrapped round a ball. The sheet can be
// partway between two forms, the layer's own code can move any point, and a
// camera turns the whole of it. The heights come from Bins, not from the
// spectrum the other layers read.
void Engine::drawWireframe(const Block &layer, LayerState &state) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    enum Form { Sheet, Tube, Ring, Sphere };
    static const QStringList formNames = {"sheet", "tube", "ring", "sphere"};
    const int form = int(formNames.indexOf(layer.choice("form")));
    const int form2 = int(formNames.indexOf(layer.choice("form2")));
    const int across = clampTo(int(layer.num("bins", V)), 8, 128), rows = clampTo(int(layer.num("rows", V)), 4, 64);
    const QString style = layer.choice("style");
    const float morph = form == form2 ? 0.f : clampTo(float(layer.num("morph", V)), 0.f, 1.f);
    const float height = float(layer.num("height", V)), width = float(layer.num("width", V));
    const float depth = float(layer.num("depth", V)), radius = float(layer.num("radius", V));
    const float yaw = float(layer.num("yaw", V)), pitch = float(layer.num("pitch", V));
    const float roll = float(layer.num("roll", V));
    const float distance = std::max(0.2f, float(layer.num("distance", V))), size = float(layer.num("size", V));
    const float x0 = float(layer.num("x", V)), y0 = float(layer.num("y", V));
    const float halfWidth = float(layer.num("thickness", V)) / 1080.f, alpha = float(layer.num("alpha", V));
    const float fog = clampTo(float(layer.num("fog", V)), 0.f, 1.f);
    const ColorSpec *color = layer.color("color");
    const expr::Program *code = layer.program("point");

    // A form that closes on itself would show a step where the top of the
    // range meets the bottom, so for those the sheet is bass at both edges
    // and treble down the middle, and the two edges meet as equals.
    const bool folded = form != Sheet || form2 != Sheet;
    const int bins = folded ? across / 2 + 1 : across;
    if (state.rows != rows || state.cols != bins) {
        state.rows = rows;
        state.cols = bins;
        state.history.assign(size_t(rows) * size_t(bins), 0.f);
        state.phase = 0;
    }
    Bins::Settings reading;
    reading.count = bins;
    reading.low = layer.num("low", V);
    reading.high = layer.num("high", V);
    reading.tilt = layer.num("tilt", V);
    reading.smooth = layer.num("smooth", V);
    reading.fade = layer.num("fade", V);
    const std::vector<float> &live = state.bins.update(m_audio, reading, m_dt);
    state.phase += m_dt * layer.num("speed", V);
    int pushes = 0;
    while (state.phase >= 1 && pushes++ < rows) {
        std::copy_backward(state.history.begin(), state.history.end() - bins, state.history.end());
        std::copy(live.begin(), live.end(), state.history.begin());
        state.phase -= 1;
    }
    state.phase = std::fmod(state.phase, 1.0);

    // What each column of the sheet keeps from one row to the next: how
    // far across it is, where it reads the spectrum, and the sine and cosine
    // of its turn round a tube, ring or sphere.
    struct Column {
        float s, t, fraction, sinTurn, cosTurn;
        int below;
    };
    static thread_local std::vector<Column> columns;
    columns.resize(size_t(across));
    for (int col = 0; col < across; ++col) {
        Column &c = columns[size_t(col)];
        c.s = float(col) / float(across - 1);
        c.t = folded ? 1 - std::abs(c.s * 2 - 1) : c.s;
        const float at = c.t * float(bins - 1);
        c.below = std::min(int(at), bins - 2);
        c.fraction = at - float(c.below);
        const float turn = (c.s * 2 - 1) * Pi;
        c.sinTurn = std::sin(turn);
        c.cosTurn = std::cos(turn);
    }

    // Where a form puts the point in this column `age` of the way back in
    // time, standing `h` high.
    const auto place = [&](int which, const Column &c, float age, float h, float out[3]) {
        switch (which) {
        case Tube:
            out[0] = (radius + h) * c.sinTurn;
            out[1] = (radius + h) * c.cosTurn;
            out[2] = (age * 2 - 1) * depth;
            break;
        case Ring: {
            // The newest sample is the hole in the middle; it spreads outward.
            const float reach = radius * (0.15f + 0.85f * age);
            out[0] = reach * c.sinTurn;
            out[1] = h;
            out[2] = reach * c.cosTurn;
            break;
        }
        case Sphere: {
            // Pole to pole, leaving a small cap open at each.
            const float down = Pi * (0.08f + 0.84f * age), ring = (radius + h) * std::sin(down);
            out[0] = ring * c.sinTurn;
            out[1] = (radius + h) * std::cos(down);
            out[2] = ring * c.cosTurn;
            break;
        }
        default:
            out[0] = (c.s * 2 - 1) * width;
            out[1] = h;
            out[2] = (age * 2 - 1) * depth;
        }
    };

    // Every point as the camera sees it, before perspective: x right, y up,
    // z away. Row 0 is the live reading; the kept rows slide back from it by
    // the phase, so the sheet moves smoothly between one sample and the next.
    struct Seen {
        float x, y, z, r, g, b, a;
    };
    static thread_local std::vector<Seen> grid;
    grid.resize(size_t(rows + 1) * size_t(across));
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    float nearest = 1e9f, farthest = -1e9f;
    for (int row = 0; row <= rows; ++row) {
        const float age = row == 0 ? 0.f : (float(row - 1) + float(state.phase)) / float(rows);
        const float *values = row == 0 ? live.data() : &state.history[size_t(row - 1) * size_t(bins)];
        // The oldest row thins out as it leaves instead of vanishing.
        const float rowAlpha = row == rows ? alpha * (1 - float(state.phase)) : alpha;
        for (int col = 0; col < across; ++col) {
            const Column &column = columns[size_t(col)];
            const int below = column.below;
            const float value = values[below] + (values[below + 1] - values[below]) * column.fraction;
            V[m_s.t] = column.t;
            V[m_s.i] = age;
            V[m_s.v] = value;
            float rgb[3], p[3];
            evalColor(color, rgb);
            place(form, column, age, value * height, p);
            if (morph > 0) {
                float q[3];
                place(form2, column, age, value * height, q);
                for (int c = 0; c < 3; ++c)
                    p[c] += (q[c] - p[c]) * morph;
            }
            float a = rowAlpha;
            if (code) {
                V[m_s.x] = p[0];
                V[m_s.y] = p[1];
                V[m_s.z] = p[2];
                V[m_s.r] = rgb[0];
                V[m_s.g] = rgb[1];
                V[m_s.b] = rgb[2];
                V[m_s.a] = a;
                code->run(V);
                p[0] = float(V[m_s.x]);
                p[1] = float(V[m_s.y]);
                p[2] = float(V[m_s.z]);
                rgb[0] = float(V[m_s.r]);
                rgb[1] = float(V[m_s.g]);
                rgb[2] = float(V[m_s.b]);
                a = float(V[m_s.a]);
            }
            // Turned about the upright axis, tipped toward the viewer, rolled.
            const float x1 = p[0] * cy + p[2] * sy, z1 = p[2] * cy - p[0] * sy;
            const float y2 = p[1] * cp + z1 * sp, z2 = z1 * cp - p[1] * sp;
            grid[size_t(row) * size_t(across) + size_t(col)] =
                Seen{x1 * cr - y2 * sr, x1 * sr + y2 * cr, z2, rgb[0], rgb[1], rgb[2], a};
            nearest = std::min(nearest, z2);
            farthest = std::max(farthest, z2);
        }
    }

    // Onto the screen. What is nearer than the lens can see is cut off
    // there, and the far side of the form is dimmed by the fog.
    const float lens = 0.05f, span = std::max(farthest - nearest, 1e-4f);
    const auto project = [&](const Seen &p) {
        const float k = size * distance / (distance + p.z);
        const float dim = 1 - fog * (p.z - nearest) / span;
        return Point{x0 + p.x * k, y0 + p.y * k, p.r * dim, p.g * dim, p.b * dim, p.a};
    };
    const auto cut = [&](const Seen &a, const Seen &b) {
        const float f = (lens - distance - a.z) / (b.z - a.z);
        return Seen{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, lens - distance,       a.r + (b.r - a.r) * f,
                    a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f};
    };
    const auto line = [&](int first, int step, int count, float half) {
        m_points.clear();
        for (int k = 0; k < count; ++k) {
            const Seen &here = grid[size_t(first + k * step)];
            const bool shown = distance + here.z >= lens;
            if (k > 0) {
                const Seen &before = grid[size_t(first + (k - 1) * step)];
                if (shown != (distance + before.z >= lens))
                    m_points.push_back(project(shown ? cut(before, here) : cut(here, before)));
            }
            if (shown) {
                m_points.push_back(project(here));
            } else {
                polyline(m_points, false, half);
                m_points.clear();
            }
        }
        polyline(m_points, false, half);
    };
    if (style == "dots") {
        for (const Seen &p : grid)
            if (distance + p.z >= lens)
                dot(project(p), halfWidth * 1.5f * std::min(4.f, distance / (distance + p.z)));
    }
    if (style == "mesh" || style == "lines")
        for (int row = 0; row <= rows; ++row)
            line(row * across, 1, across, halfWidth);
    if (style == "mesh" || style == "strands")
        for (int col = 0; col < across; ++col)
            line(col, across, rows + 1, style == "mesh" ? halfWidth * 0.7f : halfWidth);

    // Particles born "at the wave" can be born along the newest sample too.
    for (int col = 0; col < across; ++col) {
        const Seen &p = grid[size_t(col)];
        if (distance + p.z < lens)
            continue;
        const Point on = project(p);
        const float s = float(col) / float(across - 1);
        const float at = (folded ? 1 - std::abs(s * 2 - 1) : s) * float(bins - 1);
        m_c->waveSpots.push_back(Spot{on.x, on.y, live[size_t(std::min(int(at + 0.5f), bins - 1))]});
    }
}

// Sparks. Besides flying on their own they can be born where the sound is
// (along a wave, on top of the bars), ride the picture's own flow, drift
// through a slowly changing field of currents, and be pulled toward or
// swung around a point.
void Engine::drawParticles(const Block &layer, LayerState &state) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const size_t limit = size_t(clampTo(int(layer.num("count", V)), 16, 4000));
    const QString origin = layer.choice("emit"), edge = layer.choice("edge");
    const float x0 = float(layer.num("x", V)), y0 = float(layer.num("y", V)), area = float(layer.num("area", V));
    const float speed = float(layer.num("speed", V)), angle = float(layer.num("angle", V));
    const float spread = float(layer.num("spread", V)), gravity = float(layer.num("gravity", V));
    const float drag = float(layer.num("drag", V)), life = std::max(0.05f, float(layer.num("life", V)));
    const float flow = float(layer.num("flow", V)), field = float(layer.num("field", V));
    const float fieldScale = float(layer.num("fieldScale", V)), fieldSpeed = float(layer.num("fieldSpeed", V));
    const float pull = float(layer.num("pull", V)), swirl = float(layer.num("swirl", V));
    const float streak = float(layer.num("streak", V));
    const float halfSize = float(layer.num("size", V)) / 1080.f, alpha = float(layer.num("alpha", V));
    const ColorSpec *color = layer.color("color");
    const float dt = float(m_dt);

    // A visitor has no wave or bars of its own to be born at, and no flow:
    // it borrows the preset's.
    Scene &host = m_scene ? *m_scene : *m_c;
    const std::vector<Spot> &ownSpots = origin == "bars" ? m_c->barSpots : m_c->waveSpots;
    const std::vector<Spot> &spots = !ownSpots.empty() ? ownSpots : origin == "bars" ? host.barSpots : host.waveSpots;
    const bool fromSpots = origin == "wave" || origin == "bars";

    state.owed += std::max(0.0, layer.num("rate", V)) * m_dt;
    int born = 0;
    while (state.owed >= 1 && born++ < 400) {
        state.owed -= 1;
        if (state.particles.size() >= limit)
            continue;
        float x = x0, y = y0, direction = angle + (random() - 0.5f) * spread;
        if (fromSpots) {
            if (spots.empty())
                continue;
            // The louder of two random places, so sparks gather on the peaks
            // without leaving the quiet stretches bare.
            const Spot &a = spots[size_t(random() * float(spots.size())) % spots.size()];
            const Spot &b = spots[size_t(random() * float(spots.size())) % spots.size()];
            const Spot &spot = a.level >= b.level ? a : b;
            x = spot.x;
            y = spot.y;
        } else if (origin == "edges") {
            const float along = random() * 2 - 1;
            const int side = int(random() * 4) % 4;
            x = side == 0 ? -m_aspect : side == 1 ? m_aspect : along * m_aspect;
            y = side == 2 ? -1.f : side == 3 ? 1.f : along;
            direction = std::atan2(y0 - y, x0 - x) + (random() - 0.5f) * spread;
        } else if (origin == "screen") {
            x = (random() * 2 - 1) * m_aspect;
            y = random() * 2 - 1;
        } else {
            const float offset = area * std::sqrt(random()), around = random() * Tau;
            x += offset * std::cos(around);
            y += offset * std::sin(around);
        }
        const float pace = speed * (0.55f + 0.9f * random());
        state.particles.push_back(Particle{x, y, pace * std::cos(direction), pace * std::sin(direction), 0.f,
                                           life * (0.6f + 0.8f * random()), random()});
    }
    state.owed = std::min(state.owed, 400.0);

    const float slow = std::exp(-drag * dt);
    const float edgeX = m_aspect + 0.05f, edgeY = 1.05f;
    const double fieldTime = m_time * fieldSpeed;
    size_t kept = 0;
    for (Particle &p : state.particles) {
        p.age += dt;
        if (p.age >= p.life)
            continue;
        if (field != 0) {
            // Two layers of smooth noise read as a heading: neighbours head
            // the same way, so the sparks trace out currents.
            const double nx = p.x * fieldScale, ny = p.y * fieldScale;
            const double heading = (expr::noise(nx + 31.7, ny + fieldTime) +
                                    0.5 * expr::noise(nx * 2.1 - fieldTime, ny * 2.1 + 5.3)) * double(Tau) * 1.6;
            p.vx += float(std::cos(heading)) * field * dt;
            p.vy += float(std::sin(heading)) * field * dt;
        }
        if (pull != 0 || swirl != 0) {
            const float tx = x0 - p.x, ty = y0 - p.y, distance = std::sqrt(tx * tx + ty * ty) + 0.05f;
            p.vx += (tx * pull - ty * swirl) / distance * dt;
            p.vy += (ty * pull + tx * swirl) / distance * dt;
        }
        p.vx *= slow;
        p.vy = p.vy * slow - gravity * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (flow != 0) {
            float fx, fy;
            flowAt(host, p.x, p.y, &fx, &fy);
            p.x += (fx - p.x) * flow;
            p.y += (fy - p.y) * flow;
        }
        if (edge == "wrap") {
            if (p.x > edgeX) p.x -= 2 * edgeX; else if (p.x < -edgeX) p.x += 2 * edgeX;
            if (p.y > edgeY) p.y -= 2 * edgeY; else if (p.y < -edgeY) p.y += 2 * edgeY;
        } else if (edge == "bounce") {
            if (std::abs(p.x) > m_aspect) { p.x = clampTo(p.x, -m_aspect, m_aspect); p.vx = -p.vx; }
            if (std::abs(p.y) > 1) { p.y = clampTo(p.y, -1.f, 1.f); p.vy = -p.vy; }
        }
        const float t = p.age / p.life;
        V[m_s.t] = t;
        V[m_s.i] = p.seed;
        V[m_s.v] = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        float rgb[3];
        evalColor(color, rgb);
        const float shown = alpha * (1 - t * t);
        if (streak > 0) {
            // A short line back along the way it came, fading to nothing.
            m_points.clear();
            m_points.push_back(Point{p.x - p.vx * streak, p.y - p.vy * streak, rgb[0], rgb[1], rgb[2], 0});
            m_points.push_back(Point{p.x, p.y, rgb[0], rgb[1], rgb[2], shown});
            polyline(m_points, false, halfSize);
        } else {
            dot(Point{p.x, p.y, rgb[0], rgb[1], rgb[2], shown}, halfSize);
        }
        state.particles[kept++] = p;
    }
    state.particles.resize(kept);
}

// A grid of strokes, each pointing where the picture at its foot is being
// carried: the flow itself, made visible. MilkDrop calls them motion vectors.
void Engine::drawVectors(const Block &layer) {
    auto &V = m_c->p.vars.values;
    const Slots &m_s = m_c->s;
    const int cols = clampTo(int(layer.num("cols", V)), 2, 64), rows = clampTo(int(layer.num("rows", V)), 2, 48);
    const float length = float(layer.num("length", V)), alpha = float(layer.num("alpha", V));
    const float halfWidth = float(layer.num("thickness", V)) / 1080.f;
    const bool dots = layer.flag("dots", V);
    const ColorSpec *color = layer.color("color");
    Scene &host = m_scene ? *m_scene : *m_c;
    const float cellX = 2 * m_aspect / float(cols), cellY = 2.f / float(rows);
    const float longest = 1.6f * std::max(cellX, cellY);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float x = -m_aspect + (float(col) + 0.5f) * cellX, y = -1 + (float(row) + 0.5f) * cellY;
            float tx, ty;
            flowAt(host, x, y, &tx, &ty);
            // One frame's movement is tiny; twelve of them make a stroke.
            float dx = (tx - x) * 12 * length, dy = (ty - y) * 12 * length;
            const float reach = std::sqrt(dx * dx + dy * dy);
            if (reach > longest) {
                dx *= longest / reach;
                dy *= longest / reach;
            }
            V[m_s.t] = double(col) / (cols - 1);
            V[m_s.i] = double(row) / (rows - 1);
            V[m_s.v] = double(reach) / std::max(0.01, double(length)) * 5;
            V[m_s.x] = x;
            V[m_s.y] = y;
            float rgb[3];
            evalColor(color, rgb);
            if (dots || reach < 2.f / float(m_height)) {
                dot(Point{x, y, rgb[0], rgb[1], rgb[2], alpha}, halfWidth * 1.4f);
            } else {
                m_points.clear();
                m_points.push_back(Point{x, y, rgb[0], rgb[1], rgb[2], 0});
                m_points.push_back(Point{x + dx, y + dy, rgb[0], rgb[1], rgb[2], alpha});
                polyline(m_points, false, halfWidth);
            }
        }
    }
}

// A soft dark spot in the middle, every frame. A zoom keeps feeding the
// centre of the picture into itself, and this keeps it from burning white:
// MilkDrop's darken_center.
void Engine::darkenCenter() {
    const float centre[4] = {0, 0, 0, 3.f / 32.f}, rim[4] = {0, 0, 0, 0};
    const int sides = 16;
    const float radius = 0.06f;
    for (int k = 0; k < sides; ++k) {
        const float a = float(k) / sides * Tau, b = float(k + 1) / sides * Tau;
        vertex(0, 0, centre, 0, 0);
        vertex(radius * std::cos(a), radius * std::sin(a), rim, 0, 0);
        vertex(radius * std::cos(b), radius * std::sin(b), rim, 0, 0);
    }
    flush(false);
}
