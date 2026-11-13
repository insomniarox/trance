#pragma once

#include <QQuickFramebufferObject>
#include <QString>

class Backend;

// The picture, as a Qt Quick item: `Visualizer { anchors.fill: parent }`.
// It draws continuously with the engine, into whatever size it is given.
class VisualizerItem : public QQuickFramebufferObject {
    Q_OBJECT
    // How the drawing is going, twice a second, for the debug overlay.
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(double frameMs READ frameMs NOTIFY statsChanged)
    Q_PROPERTY(double worstMs READ worstMs NOTIFY statsChanged)
    Q_PROPERTY(int pixelWidth READ pixelWidth NOTIFY statsChanged)
    Q_PROPERTY(int pixelHeight READ pixelHeight NOTIFY statsChanged)
    Q_PROPERTY(int pictureWidth READ pictureWidth NOTIFY statsChanged)
    Q_PROPERTY(int pictureHeight READ pictureHeight NOTIFY statsChanged)
    Q_PROPERTY(QString graphics READ graphics NOTIFY statsChanged)
    Q_PROPERTY(bool blending READ blending NOTIFY statsChanged)
    Q_PROPERTY(int visitors READ visitors NOTIFY statsChanged)

public:
    explicit VisualizerItem(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;
    struct Stats {
        double fps = 0, frameMs = 0, worstMs = 0;
        int width = 0, height = 0;               // the window, in pixels
        int pictureWidth = 0, pictureHeight = 0; // what the engine works at
        QString graphics; // the graphics card, as its driver names it
        bool blending = false; // two presets are on the picture, changing over
        int visitors = 0;
    };
    double fps() const { return m_stats.fps; }
    double frameMs() const { return m_stats.frameMs; }
    double worstMs() const { return m_stats.worstMs; }
    int pixelWidth() const { return m_stats.width; }
    int pixelHeight() const { return m_stats.height; }
    int pictureWidth() const { return m_stats.pictureWidth; }
    int pictureHeight() const { return m_stats.pictureHeight; }
    QString graphics() const { return m_stats.graphics; }
    bool blending() const { return m_stats.blending; }
    int visitors() const { return m_stats.visitors; }
    void setStats(const Stats &stats);

    // The one backend every visualizer in the process shows.
    static void setBackend(Backend *backend);
    static Backend *backend();

signals:
    void statsChanged();

private:
    Stats m_stats;
};
