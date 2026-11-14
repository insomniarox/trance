#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <memory>

#include "audio.h"
#include "audiocapture.h"
#include "library.h"
#include "preset.h"

class Theme;

// The app's state: which preset is showing, the copy of it being edited,
// the settings, and the music coming in. QML reads and steers it as
// `backend`; the renderer reads the compiled preset and the audio from it.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(bool currentIsYours READ currentIsYours NOTIFY currentChanged)
    Q_PROPERTY(bool currentShadowsBuiltin READ currentShadowsBuiltin NOTIFY currentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY presetChanged)
    Q_PROPERTY(int revision READ revision NOTIFY presetChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(QString presetText READ presetText NOTIFY presetChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY presetChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY presetChanged)
    Q_PROPERTY(QVariantMap schema READ schema CONSTANT)
    Q_PROPERTY(QStringList paletteColors READ paletteColors NOTIFY paletteChanged)

    Q_PROPERTY(double sensitivity READ sensitivity WRITE setSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(int cycleSeconds READ cycleSeconds WRITE setCycleSeconds NOTIFY settingsChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY settingsChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY settingsChanged)
    Q_PROPERTY(QString paletteOverride READ paletteOverride WRITE setPaletteOverride NOTIFY settingsChanged)
    Q_PROPERTY(double quality READ quality WRITE setQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool glass READ glass WRITE setGlass NOTIFY settingsChanged)
    Q_PROPERTY(double glassOpacity READ glassOpacity WRITE setGlassOpacity NOTIFY settingsChanged)
    Q_PROPERTY(QString titleStyle READ titleStyle WRITE setTitleStyle NOTIFY settingsChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourcesChanged)
    Q_PROPERTY(double blendSeconds READ blendSeconds WRITE setBlendSeconds NOTIFY settingsChanged)
    Q_PROPERTY(QString transition READ transition WRITE setTransition NOTIFY settingsChanged)
    Q_PROPERTY(bool mashups READ mashups WRITE setMashups NOTIFY settingsChanged)
    Q_PROPERTY(bool visitors READ visitors WRITE setVisitors NOTIFY settingsChanged)
    Q_PROPERTY(bool colorDrift READ colorDrift WRITE setColorDrift NOTIFY settingsChanged)
    Q_PROPERTY(bool showNames READ showNames WRITE setShowNames NOTIFY settingsChanged)
    Q_PROPERTY(bool debug READ debug WRITE setDebug NOTIFY settingsChanged)
    Q_PROPERTY(QString activePalette READ effectivePalette NOTIFY paletteChanged)
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY settingsChanged)
    Q_PROPERTY(bool metering READ metering WRITE setMetering NOTIFY meteringChanged)

    Q_PROPERTY(QString audioError READ audioError NOTIFY audioChanged)
    Q_PROPERTY(bool silent READ silent NOTIFY audioChanged)
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    // The browser's filters: a family of colors (empty for any) and features
    // a preset must all have. Kept to, the changes move among those that
    // match, so the filters can set a mood for a while.
    Q_PROPERTY(QString filterColor READ filterColor WRITE setFilterColor NOTIFY presetsChanged)
    Q_PROPERTY(QStringList filterFeatures READ filterFeatures WRITE setFilterFeatures NOTIFY presetsChanged)
    Q_PROPERTY(bool keepToFilter READ keepToFilter WRITE setKeepToFilter NOTIFY presetsChanged)
    Q_PROPERTY(bool filtering READ filtering NOTIFY presetsChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList colorFamilies READ colorFamilies NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList featureList READ featureList CONSTANT)

public:
    // Tests pass a temporary folder and listen = false, so nothing touches
    // the real configuration or the sound system.
    explicit Backend(const QString &configDirectory = {}, bool listen = true, QObject *parent = nullptr);
    ~Backend() override;

    void setTheme(Theme *theme);
    // Shows the named preset or preset file; false with a message if it can't.
    bool open(const QString &nameOrPath, QString *error = nullptr);

    QVariantList presets() const;
    QString filterColor() const { return m_filterColor; }
    QStringList filterFeatures() const { return m_filterFeatures; }
    bool keepToFilter() const { return m_keepToFilter; }
    bool filtering() const { return !m_filterColor.isEmpty() || !m_filterFeatures.isEmpty(); }
    int matchCount() const;
    QVariantList colorFamilies() const;
    QVariantList featureList() const;
    void setFilterColor(const QString &family);
    void setFilterFeatures(const QStringList &features);
    void setKeepToFilter(bool keep);
    QString currentName() const { return m_currentName; }
    bool currentIsYours() const;
    // Yours, and standing in for a built-in of the same name, which is
    // hidden until yours is deleted.
    bool currentShadowsBuiltin() const;
    bool dirty() const { return m_json != m_saved; }
    int revision() const { return m_revision; }
    QVariantList layers() const;
    QVariantList problems() const;
    QString presetText() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QVariantMap schema() const { return presets::schema(); }
    QStringList paletteColors() const;
    QJsonObject json() const { return m_json; }

    double sensitivity() const { return m_sensitivity; }
    int cycleSeconds() const { return m_cycleSeconds; }
    bool shuffle() const { return m_shuffle; }
    bool locked() const { return m_locked; }
    QString paletteOverride() const { return m_paletteOverride; }
    double quality() const { return m_quality; }
    bool glass() const { return m_glass; }
    // How dark the glass is: 0 is clear wherever the picture is black, 1 is
    // solid, which looks the same as no glass at all.
    double glassOpacity() const { return m_glassOpacity; }
    // Where a new track's name appears: "corner" (over the picture),
    // "picture" (drawn into it, for the flow to carry off), or "off".
    QString titleStyle() const { return m_titleStyle; }
    QString source() const { return m_source; }
    QVariantList sources() const;
    // The name of what is being listened to, as the list of sources has it.
    QString sourceLabel() const;
    double blendSeconds() const { return m_blendSeconds; }
    QString transition() const { return m_transition; }
    bool mashups() const { return m_mashups; }
    bool visitors() const { return m_visitors; }
    bool colorDrift() const { return m_colorDrift; }
    // Whether a preset's name is shown when it comes on.
    bool showNames() const { return m_showNames; }
    // The overlay of what is running: preset, frame rate, sound.
    bool debug() const { return m_debug; }
    bool editing() const { return m_editing; }
    // While something on screen shows the levels, they are announced as
    // they change; otherwise nobody is told.
    bool metering() const { return m_metering; }
    void setSensitivity(double sensitivity);
    void setCycleSeconds(int seconds);
    void setShuffle(bool shuffle);
    void setLocked(bool locked);
    void setPaletteOverride(const QString &name);
    void setQuality(double quality);
    void setGlass(bool glass);
    void setGlassOpacity(double opacity);
    void setTitleStyle(const QString &style);
    void setSource(const QString &source);
    void setBlendSeconds(double seconds);
    void setTransition(const QString &style);
    void setMashups(bool on);
    void setVisitors(bool on);
    void setColorDrift(bool on);
    // The track now playing, for the title drawn into the picture.
    void setTrack(const QString &title, const QString &artist);
    void setShowNames(bool show);
    void setDebug(bool debug);
    void setEditing(bool editing);
    void setMetering(bool metering);

    QString audioError() const { return m_capture ? m_capture->error() : QString(); }
    bool silent() const { return m_silent; }
    QVariantList levels() const;

    // Changing preset.
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void random();
    Q_INVOKABLE bool select(const QString &name);
    // A new preset put together from parts of others: one's motion, another's
    // layers, a third's colors and finish. False if nothing workable came up.
    Q_INVOKABLE bool mashup();
    Q_INVOKABLE void toggleFeature(const QString &feature);
    Q_INVOKABLE void clearFilter();
    Q_INVOKABLE void refreshSources();
    Q_INVOKABLE QString cycleTitleStyle();
    // What is going on right now, for the debug overlay: when the preset
    // changes next and why not, the levels as formulas see them, and so on.
    Q_INVOKABLE QVariantMap stats() const;

    // Editing the working copy. A path is "name", "feedback.zoom",
    // "post.bloom" or "layers.2.color".
    Q_INVOKABLE QVariant param(const QString &path) const;
    Q_INVOKABLE bool isSet(const QString &path) const;
    // Whether this differs from the preset as it was last saved or opened:
    // a setting ("post.bloom", "layers.2.color"), a layer ("layers.2"), a
    // whole section ("feedback") or a code block ("pixel").
    Q_INVOKABLE bool isEdited(const QString &path) const;
    Q_INVOKABLE void setParam(const QString &path, const QVariant &value);
    Q_INVOKABLE void resetParam(const QString &path);
    Q_INVOKABLE QString problemAt(const QString &path) const;
    Q_INVOKABLE QString colorPreview(const QString &color) const;
    Q_INVOKABLE void addLayer(const QString &type);
    Q_INVOKABLE void removeLayer(int index);
    Q_INVOKABLE void moveLayer(int index, int delta);
    Q_INVOKABLE void duplicateLayer(int index);
    Q_INVOKABLE bool setPresetText(const QString &text);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Keeping it.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &name);
    Q_INVOKABLE void revert();
    Q_INVOKABLE bool newPreset(const QString &name);
    Q_INVOKABLE bool deleteCurrent();
    Q_INVOKABLE QString cyclePalette();

    // For the renderer, called while the window's thread is paused.
    const AudioFrame &audioFrame();
    const CompiledPreset &compiled() const { return m_compiled; }
    int compiledRevision() const { return m_compiledRevision; }
    double pendingBlend() const { return m_blend; }
    // The palette in force: the user's pick, or wherever the drift has got to.
    // The palette in place of the preset's own, if any. Not while the preset
    // is being edited, so its own palette shows as it is changed.
    QString effectivePalette() const {
        return m_editing ? QString() : m_colorDrift && !m_driftPalette.isNull() ? m_driftPalette : m_paletteOverride;
    }
    int transitionIndex() const { return qMax(0, int(presets::transitionStyles().indexOf(m_transition))); }
    const QList<CompiledPreset> &visitorPool() const { return m_visitorPool; }
    double visitorRate() const { return m_visitors ? 1.0 : 0.0; }
    QImage titleImage() const { return m_titleStyle == "picture" ? m_titleImage : QImage(); }
    int titleRevision() const { return m_titleRevision; }
    double progress() const;
    ThemeColors themeColors() const;
    int themeRevision() const { return m_themeRevision; }
    void setEngineError(const QString &error);

    Analyzer &analyzer() { return m_analyzer; }
    Library &library() { return m_library; }

signals:
    void presetsChanged();
    void currentChanged();
    void presetChanged();
    void layersChanged();
    void problemsChanged();
    void paletteChanged();
    // One signal for every setting. They change only when the user changes
    // one, so the bindings that are worked out again for nothing cost nothing
    // that shows; a signal each would be twenty more of these.
    void settingsChanged();
    void audioChanged();
    void levelsChanged();
    void meteringChanged();
    void sourcesChanged();
    // A short message for the window to show and let fade.
    void notice(const QString &message);

private:
    void load(int index, double blend);
    void adopt(const QJsonObject &json, const QString &name, bool saved, double blend, bool remember = true);
    void advance(bool automatic);
    bool mashup(double blend);
    int randomIndex() const;
    bool matches(const PresetInfo &preset) const;
    // Where the changes come from: the presets that match when the filters
    // are kept to and any do, otherwise all of them. In library order.
    QList<int> pool() const;
    int step(int direction) const;
    double quickBlend() const { return qMin(m_blendSeconds, 1.2); }
    void recompile(double blend);
    void pushUndo(const QString &key);
    void changed(bool structure);
    void tick();
    void store(const QString &key, const QVariant &value);
    const Field *fieldAt(const QStringList &parts) const;

    Library m_library;
    std::unique_ptr<QSettings> m_settings;
    bool m_starting = true;
    Theme *m_theme = nullptr;
    int m_themeRevision = 0;

    QString m_currentName;
    QJsonObject m_json, m_saved;
    QList<QJsonObject> m_undo, m_redo;
    QString m_lastEditKey;
    QElapsedTimer m_lastEdit;
    int m_revision = 0;

    CompiledPreset m_compiled;
    int m_compiledRevision = 0;
    double m_blend = 0;
    QList<Problem> m_problems;
    QString m_engineError;

    double m_sensitivity = 1.0;
    int m_cycleSeconds = 30;
    bool m_shuffle = true;
    bool m_locked = false;
    QString m_paletteOverride;
    double m_quality = 1.0;
    bool m_glass = false;
    double m_glassOpacity = 0;
    QString m_titleStyle = "corner";
    QString m_source = "system";
    double m_blendSeconds = 4.0;
    QString m_transition = "mixed";
    bool m_mashups = false, m_visitors = true, m_colorDrift = false;
    QString m_filterColor;
    QStringList m_filterFeatures;
    bool m_keepToFilter = false;
    QString m_driftPalette; // null until the drift first moves
    QElapsedTimer m_sinceDrift, m_sinceSources;
    double m_driftAfter = 30;
    QList<CompiledPreset> m_visitorPool;
    QImage m_titleImage;
    int m_titleRevision = 0;
    bool m_showNames = false, m_debug = false;
    bool m_editing = false, m_metering = false;

    Analyzer m_analyzer;
    AudioCapture *m_capture = nullptr;
    QElapsedTimer m_sinceAnalysis, m_sinceChange;
    // When the last few beats landed, for a tempo in the debug overlay.
    QElapsedTimer m_clock;
    QList<qint64> m_beatTimes;
    int m_lastBeats = 0;
    bool m_silent = true;
    QTimer m_tick;
};
