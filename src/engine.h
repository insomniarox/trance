#pragma once

#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QString>
#include <memory>
#include <vector>

#include "audio.h"
#include "preset.h"

// Draws presets with OpenGL. It knows nothing about windows: give it a
// current context and a framebuffer, and it renders one frame into it. The
// window and the headless `trance render` share it.
//
// Each frame, MilkDrop-style: last frame's picture is warped and faded into
// a new buffer, the layers (waves, bars, shapes, terrain, particles) are
// drawn on top, and that buffer goes to the screen through the post pass.
//
// A change of preset is not a cut. For its length both presets run on the
// one picture: the motion and the look hand over place by place, the layers
// fade across, and a wave reshapes itself into the next preset's wave, the
// way SoundSpectrum's G-Force morphs between its WaveShapes.
class Engine : protected QOpenGLExtraFunctions {
public:
    Engine();
    ~Engine(); // The context must be current.

    // Takes effect on the next frame, changing over in that many seconds.
    // With 0 seconds the preset is swapped in place and keeps what it can of
    // the running state, which is what editing wants.
    void setPreset(const CompiledPreset &preset, double blendSeconds);
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
    void setProgress(double progress) { m_progress = progress; }

    void render(const AudioFrame &audio, double dt, unsigned targetFbo, int width, int height);

    // Why the last preset could not be shown; empty when all is well.
    QString error() const { return m_error; }
    double variable(const QString &name) const;
    bool changing() const { return bool(m_old); }
    int visitorCount() const { return int(m_visitors.size()); }
    // The size the picture is worked out at, which the quality setting and
    // the 1440-line limit make smaller than the window.
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
        std::vector<float> history; // terrain rows, newest first
        int rows = 0, cols = 0;
        double phase = 0;
        std::vector<float> peaks;
    };
    struct Slots {
        int time, dt, frame, fps, bass, mid, treb, vol, bassAtt, midAtt, trebAtt, volAtt, beat, beats, aspect,
            progress, t, i, v, v2, n, count, x, y, r, g, b, a, radius, angle, size, r2, g2, b2, a2, warpSpeed,
            rad, ang, zoom, zoomexp, rot, warp, warpScale, cx, cy, dx, dy, sx, sy, bgSpeed;
        int band[AudioFrame::Bands], bandAtt[AudioFrame::Bands];
    };
    // A wave as it is about to be drawn, kept apart from the drawing so two
    // of them can be blended into one.
    struct WaveShape {
        std::vector<Point> points;
        std::vector<float> levels; // how loud the sound is at each point
        bool closed = false, dots = false, additive = true;
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
    };

    void initialize();
    GLuint build(const QString &vertex, const QString &fragment, QString *error);
    void ensureTarget(Target &target, int width, int height, bool wide, GLenum filter);
    void releaseTarget(Target &target);
    std::unique_ptr<Scene> makeScene(const CompiledPreset &preset, bool shaders, QString *error);
    void destroyScene(std::unique_ptr<Scene> &scene);
    void adoptPending();
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
    void drawParticles(const Block &layer, LayerState &state);
    void drawVectors(const Block &layer);
    void darkenCenter();

    void vertex(float x, float y, const float *rgba, float ex, float ey, float u = -1, float v = 0);
    void polyline(const std::vector<Point> &points, bool closed, float halfWidth);
    void dot(const Point &point, float halfSize);
    void flush(bool additive);
    float random();

    bool m_initialized = false;
    bool m_es = false;
    bool m_wide = false; // half-float buffers are available

    // The preset showing, the one it is taking over from (only during a
    // change), and whichever of them, or a visitor, is being worked on.
    std::unique_ptr<Scene> m_scene, m_old;
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

    GLuint m_prim = 0, m_bright = 0, m_blur = 0;
    GLuint m_emptyVao = 0, m_primVao = 0, m_vbo = 0;
    Target m_buffers[2], m_bloom[2], m_snapshot, m_other;
    int m_current = 0;
    int m_width = 0, m_height = 0;
    float m_aspect = 1;

    std::vector<float> m_vertices, m_uniforms;
    std::vector<Point> m_points;
    AudioFrame m_audio;

    double m_time = 0, m_dt = 1.0 / 60, m_fps = 60, m_progress = 0;
    qint64 m_frame = 0;
    bool m_glass = false;
    double m_glassOpacity = 0;
    double m_quality = 1;
    uint32_t m_random = 0x2545F491u;
};
