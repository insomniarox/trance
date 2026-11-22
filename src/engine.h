#pragma once

#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QString>
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "audio.h"
#include "preset.h"

// Draws presets with OpenGL. It knows nothing about windows: give it a
// current context and a framebuffer, and it renders one frame into it. The
// window and the headless `trance render` share it.
//
// Each frame, MilkDrop-style: last frame's picture is warped and faded into
// a new buffer, the layers (waves, bars, shapes, terrain, wireframes,
// particles) are drawn on top, and that buffer goes to the screen through the post pass.
//
// A change of preset is not a cut. For its length both presets run on the
// one picture: the motion and the look hand over place by place, the layers
// fade across, and a wave reshapes itself into the next preset's wave, the
// way SoundSpectrum's G-Force morphs between its WaveShapes.
// Somewhere else to have a preset's two shaders built, so that drawing
// doesn't stop for it. request() hands over the fragment shaders under a
// number; take() gives back one finished pair at a time, oldest first, and
// false when none is ready yet. Only the latest request has to be answered.
class ProgramBuilder {
public:
    struct Result {
        int id = 0;
        unsigned warp = 0, composite = 0; // both 0, with the error, if it could not be built
        QString error;
    };
    virtual ~ProgramBuilder() = default;
    virtual void request(int id, const QString &warp, const QString &composite) = 0;
    virtual bool take(Result *result) = 0;
};

class Engine : protected QOpenGLExtraFunctions {
public:
    Engine();
    ~Engine(); // The context must be current.

    // Takes effect on the next frame, changing over in that many seconds.
    // With 0 seconds the preset is swapped in place and keeps what it can of
    // the running state, which is what editing wants.
    void setPreset(const CompiledPreset &preset, double blendSeconds);
    // With a builder, a new preset's shaders are made there and the preset
    // comes on a few frames later, when they are ready; the one showing
    // carries on in the meantime. Without one they are built on the spot,
    // which is what a render to a file wants. The builder must outlive the
    // engine's use of it.
    void setProgramBuilder(ProgramBuilder *builder) { m_builder = builder; }
    // Compiles and links one program in whatever context is current: 0 and
    // the driver's message if it can't.
    static unsigned buildProgram(QOpenGLExtraFunctions *gl, bool es, const QString &vertex, const QString &fragment,
                                 QString *error);
    // The vertex shader every preset's two shaders go with.
    static QString screenVertexShader();
    void setTheme(const ThemeColors &theme);
    // A palette name to use instead of each preset's own; empty to stop.
    void setPaletteOverride(const QString &name);
    // Where the new preset arrives first during a change: an index into
    // presets::transitionStyles(), 0 picking a different one every time.
    void setTransitionStyle(int style) { m_transitionStyle = style; }
    // Small extra scenes that drop in over any preset for a while and leave
    // again; only their layers and code are used. A rate of 0 keeps them away.
    void setVisitors(const QList<CompiledPreset> &visitors, double rate);
    // Writes this into the picture for a few seconds, for the flow to carry
    // off: white text on a clear ground. A null image writes nothing.
    void showText(const QImage &image);
    void setGlass(bool glass) { m_glass = glass; }
    // How solid the dark parts stay in glass mode: 0 lets the desktop
    // through wherever the picture is black, 1 lets none of it through.
    void setGlassOpacity(double opacity) { m_glassOpacity = opacity; }
    void setQuality(double quality) { m_quality = quality; }
    // The tallest picture worked out, in lines: 1440, or up to 2160 for a
    // 4K screen. A smaller window is worked out at its own size either way.
    void setMaxLines(int lines) { m_maxLines = std::clamp(lines, 1440, 2160); }
    void setProgress(double progress) { m_progress = progress; }

    void render(const AudioFrame &audio, double dt, unsigned targetFbo, int width, int height);

    // Why the last preset could not be shown; empty when all is well.
    QString error() const { return m_error; }
    double variable(const QString &name) const;
    bool changing() const { return bool(m_old); }
    int visitorCount() const { return int(m_visitors.size()); }
    // For the debug overlay: how far a change of preset has come (1 when
    // none is under way) and the pattern it arrives in, who is visiting,
    // how many sparks are alive, how many vertices the last frame drew, and
    // whether a preset's shaders are still being built.
    double changeProgress() const { return m_old ? double(m_blend) : 1.0; }
    QString changePattern() const;
    QStringList visitorNames() const;
    int particleCount() const;
    int vertexCount() const { return m_lastVertices; }
    bool building() const { return m_awaitedId != 0; }
    // The settings that decide how the picture moves and how bright it
    // gets, as the preset showing worked them out for the last frame, so
    // formulas and frame code are taken into account. Pixel code may still
    // change them place by place, which pixelCode says.
    QVariantMap liveSettings() const;
    // Whether the picture is kept in half floats, or in 8 bits a channel
    // where the graphics card can't draw to half floats.
    bool wideBuffers() const { return m_wide; }
    // The size the picture is worked out at, which the quality setting and
    // the line limit make smaller than the window.
    int pictureWidth() const { return m_width; }
    int pictureHeight() const { return m_height; }

private:
    struct Target {
        GLuint fbo = 0, texture = 0;
        int width = 0, height = 0;
    };
    struct Point {
        float x, y, r, g, b, a;
    };
    struct Particle {
        float x, y, vx, vy, age, life, seed;
    };
    struct LayerState {
        std::vector<Particle> particles;
        double owed = 0; // fractional particles still to be born
        std::vector<float> history; // terrain and wireframe rows, newest first
        int rows = 0, cols = 0;
        double phase = 0;
        std::vector<float> peaks;
        Bins bins; // a wireframe's own reading of the spectrum
    };
    struct Slots {
        int time, dt, frame, fps, bass, mid, treb, vol, bassAtt, midAtt, trebAtt, volAtt, beat, beats, aspect,
            progress, t, i, v, v2, n, count, x, y, r, g, b, a, radius, angle, size, r2, g2, b2, a2, warpSpeed,
            rad, ang, zoom, zoomexp, rot, warp, warpScale, cx, cy, dx, dy, sx, sy, bgSpeed;
        int band[AudioFrame::Bands], bandAtt[AudioFrame::Bands];
        int z;
    };
    // How a layer's light meets what is under it: covering it, adding to
    // it, or keeping the brighter of the two, which never grows past the
    // layer's own color however often it is drawn there.
    enum class Blend : uint8_t { Over, Add, Max };
    // A wave as it is about to be drawn, kept apart from the drawing so two
    // of them can be blended into one.
    struct WaveShape {
        std::vector<Point> points;
        std::vector<float> levels; // how loud the sound is at each point
        bool closed = false, dots = false;
        Blend blend = Blend::Add;
        float halfWidth = 0;
        QString mirror;
    };
    // Where something loud is on screen, for particles to be born at.
    struct Spot {
        float x, y, level;
    };
    // One preset, running.
    struct Scene {
        CompiledPreset p;
        GLuint warp = 0, composite = 0, paletteTexture = 0;
        Slots s = {};
        std::vector<std::pair<int, const expr::Program *>> feedbackPrograms, postPrograms, backgroundPrograms;
        std::vector<LayerState> layers;
        bool ranInit = false, colorsDirty = true, hasColors = false;
        double warpTime = 0, backgroundTime = 0;
        float lut[256][3] = {}, lutFrom[256][3] = {}, lutTo[256][3] = {};
        float lutFade = 1, paletteSpan = 1;
        std::vector<Spot> waveSpots, barSpots;
        // Visitors only: when it came, how long it stays, how much it shows.
        double age = 0, life = 0;
        std::vector<double> scratch;
        // What flowAt worked out last and the settings it was from, as it is
        // asked point after point and they seldom differ from one to the
        // next. Not a number to begin with, which nothing equals.
        struct {
            double zoom = std::numeric_limits<double>::quiet_NaN(), sx = zoom, sy = zoom, rot = zoom, k = zoom;
            double warpTime = zoom;
            double zoomK = 0, sxK = 0, syK = 0, ca = 1, sa = 0;
            double f0 = 0, f1 = 0, f2 = 0, f3 = 0;
        } flow;
        bool pixelReadsAngle = true;
    };

    void initialize();
    GLuint build(const QString &vertex, const QString &fragment, QString *error);
    void ensureTarget(Target &target, int width, int height, bool wide, GLenum filter);
    void releaseTarget(Target &target);
    // With programs handed in, those are used instead of building them.
    std::unique_ptr<Scene> makeScene(const CompiledPreset &preset, bool shaders, QString *error, GLuint warp = 0,
                                     GLuint composite = 0);
    void destroyScene(std::unique_ptr<Scene> &scene);
    // Takes a scene off the picture and deletes it a few frames later, when
    // the card is done with the frames that used it.
    void retireScene(std::unique_ptr<Scene> &scene);
    void startPending();
    void collectBuilt();
    void adopt(std::unique_ptr<Scene> scene, double blendSeconds, const QString &error);
    void rebuildColors(Scene &scene);
    void uploadPalette(Scene &scene);
    void updateScene(Scene &scene, double progress);
    void manageVisitors();
    void warpScene(Scene &scene, const Target &from, const Target &to, float blend, GLuint other);
    void compositeScene(Scene &scene, GLuint fbo, int width, int height, float blend, GLuint snapshot, bool glass);
    void drawLayers(Scene &scene, bool stashWaves, bool morphWaves);
    void drawText();
    void sampleAt(double t, float out[3]) const;
    void evalColor(const ColorSpec *spec, float out[3]);
    void uploadVariables(GLuint program, const Scene &scene);
    // Where the picture at (x, y) is carried to by this frame's feedback.
    void flowAt(Scene &scene, float x, float y, float *toX, float *toY);

    void buildWave(const Block &layer, WaveShape &shape);
    void emitWave(const WaveShape &shape);
    void drawBars(const Block &layer, LayerState &state);
    void drawShape(const Block &layer);
    void drawTerrain(const Block &layer, LayerState &state);
    void drawWireframe(const Block &layer, LayerState &state);
    void drawParticles(const Block &layer, LayerState &state);
    void drawVectors(const Block &layer);
    void darkenCenter();

    // Tens of thousands of times a frame, so kept to plain stores, here
    // where every caller can have it inlined. rgba must not point into
    // m_vertices, which may move.
    void vertex(float x, float y, const float *rgba, float ex, float ey, float u = -1, float v = 0) {
        if (m_used + 10 > m_vertices.size())
            m_vertices.resize(std::max<size_t>(2 * m_vertices.size(), size_t(1) << 16));
        float *const into = m_vertices.data() + m_used;
        into[0] = x;
        into[1] = y;
        into[2] = rgba[0];
        into[3] = rgba[1];
        into[4] = rgba[2];
        // m_layerAlpha is how much of the scene being drawn shows: all of it
        // normally, part of it while presets change over or a visitor fades.
        into[5] = rgba[3] * m_layerAlpha;
        into[6] = ex;
        into[7] = ey;
        into[8] = u;
        into[9] = v;
        m_used += 10;
    }
    void polyline(const std::vector<Point> &points, bool closed, float halfWidth);
    void dot(const Point &point, float halfSize);
    void flush(Blend blend);
    float random();

    bool m_initialized = false;
    bool m_es = false;
    bool m_wide = false; // half-float buffers are available

    // The preset showing, the one it is taking over from (only during a
    // change), the one a change left until the card is done with it, and
    // whichever of them, or a visitor, is being worked on.
    std::unique_ptr<Scene> m_scene, m_old, m_retired;
    int m_retiredFrames = 0; // frames until m_retired is deleted
    Scene *m_c = nullptr;
    const Scene *m_colors = nullptr; // whose palette palette(...) reads
    CompiledPreset m_pending;
    bool m_hasPending = false;
    double m_pendingBlend = 0;
    QString m_error;

    // The change in progress: 0 to 1, how fast, and its pattern.
    float m_blend = 1, m_blendRate = 0, m_patternSeed = 0;
    int m_pattern = 0, m_transitionStyle = 0, m_lastPattern = 0;
    float m_layerAlpha = 1, m_mix = 1;
    // How a frame's layers make up for its length; see render().
    float m_coverPower = 1, m_addGain = 1;
    GLint m_primCover = -1, m_primGain = -1;
    std::vector<WaveShape> m_stash;
    size_t m_stashUsed = 0;
    WaveShape m_wave, m_morph;

    QList<CompiledPreset> m_visitorPool;
    std::vector<std::unique_ptr<Scene>> m_visitors;
    double m_visitorRate = 0, m_visitorCheck = 0;
    int m_lastVisitor = -1;

    QImage m_textImage;
    bool m_textPending = false;
    GLuint m_textTexture = 0;
    float m_textAge = 1e9f, m_textAspect = 1;

    ThemeColors m_theme;
    QString m_paletteOverride;

    GLuint m_prim = 0, m_bright = 0;
    GLuint m_emptyVao = 0, m_primVao = 0, m_vbo = 0;
    // Bloom's tuning, the same for every preset; a preset's bloom and
    // bloomThreshold work on top of it.
    // - Levels: how many sizes the glow is worked out at, halving from 360
    //   lines, half a 720-line picture. The last, 11 lines, sets how far it
    //   can reach.
    //   The levels are those lines at any size of picture, so the glow
    //   reaches the same share of the screen in each; a smaller one leaves
    //   out the levels finer than half its own height.
    // - Scatter: how much of the wider level each level takes on the way
    //   back up, from 0 to 1. Higher reaches further and is fainter close
    //   in; 0.7 reaches about a sixth of the screen past a bright edge.
    // - Glow: how bright all of it is. Above 1 gives back the light that
    //   scatter moves out of the near glow, without retuning any preset;
    //   1.5 brings the near glow of a 720-line picture back to what the
    //   single quarter-size blur that bloom used to be gave.
    static constexpr int BloomLevels = 6;
    static constexpr float BloomScatter = 0.7f, BloomGlow = 1.5f;
    Target m_buffers[2], m_bloom[BloomLevels], m_snapshot, m_other;
    int m_bloomTop = 0; // the finest level this size of picture has
    int m_current = 0;
    int m_width = 0, m_height = 0;
    float m_aspect = 1;

    // The triangles waiting to be drawn, ten floats a corner: the first
    // m_used of m_vertices, which only grows.
    std::vector<float> m_vertices, m_uniforms;
    size_t m_used = 0;
    int m_frameVertices = 0, m_lastVertices = 0;
    std::vector<Point> m_points;
    AudioFrame m_audio;

    double m_time = 0, m_dt = 1.0 / 60, m_fps = 60, m_progress = 0;
    qint64 m_frame = 0;
    // A preset whose shaders are being built elsewhere, and the number the
    // builder knows it by; anything older that comes back is thrown away.
    ProgramBuilder *m_builder = nullptr;
    CompiledPreset m_awaited;
    double m_awaitedBlend = 0;
    int m_awaitedId = 0, m_lastRequest = 0;
    bool m_glass = false;
    double m_glassOpacity = 0;
    double m_quality = 1;
    int m_maxLines = 1440;
    uint32_t m_random = 0x2545F491u;
};
