#include "backend.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QSysInfo>
#include <QUrl>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStandardPaths>

#include "theme.h"

#include <algorithm>

namespace {

QString configPath(const QString &directory) {
    return directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/trance"
                               : directory;
}

const QStringList CodeKeys = {"init", "frame", "pixel", "shade"};

QString joined(const QJsonValue &value) {
    QStringList lines;
    for (const auto &line : value.toArray())
        lines << line.toString();
    return lines.join('\n');
}

} // namespace

Backend::Backend(const QString &configDirectory, bool listen, QObject *parent)
    : QObject(parent), m_library(configPath(configDirectory) + "/presets"),
      m_rotation(m_library, configPath(configDirectory) + "/lists.json"),
      m_settings(std::make_unique<QSettings>(configPath(configDirectory) + "/settings.ini", QSettings::IniFormat)) {
    m_sensitivity = qBound(0.2, m_settings->value("sensitivity", 1.0).toDouble(), 4.0);
    m_cycleSeconds = qBound(0, m_settings->value("cycleSeconds", 30).toInt(), 3600);
    m_shuffle = m_settings->value("shuffle", true).toBool();
    m_locked = m_settings->value("locked", false).toBool();
    m_paletteOverride = m_settings->value("paletteOverride").toString();
    if (!presets::paletteNames().contains(m_paletteOverride))
        m_paletteOverride.clear();
    m_quality = qBound(0.25, m_settings->value("quality", 1.0).toDouble(), 1.0);
    m_maxLines = m_settings->value("maxLines", 1440).toInt() >= 2160 ? 2160 : 1440;
    m_glass = m_settings->value("glass", false).toBool();
    m_glassOpacity = qBound(0.0, m_settings->value("glassOpacity", 0.0).toDouble(), 1.0);
    // Older settings only said whether titles were shown at all.
    m_titleStyle = m_settings->value("titleStyle", m_settings->value("showTitles", true).toBool() ? "corner" : "off")
                       .toString();
    if (!QStringList{"corner", "picture", "off"}.contains(m_titleStyle))
        m_titleStyle = "corner";
    m_source = m_settings->value("source", "system").toString();
    // The demo track is for a try: Trance doesn't start out playing music.
    if (!AudioCapture::isSource(m_source) || m_source == "demo")
        m_source = "system";
    m_blendSeconds = qBound(0.0, m_settings->value("blendSeconds", 4.0).toDouble(), 20.0);
    m_transition = m_settings->value("transition", "mixed").toString();
    if (!presets::transitionStyles().contains(m_transition))
        m_transition = "mixed";
    m_mashups = m_settings->value("mashups", false).toBool();
    m_visitors = m_settings->value("visitors", true).toBool();
    m_colorDrift = m_settings->value("colorDrift", false).toBool();
    m_rotation.setColor(m_settings->value("filterColor").toString());
    m_rotation.setFeatures(m_settings->value("filterFeatures").toStringList());
    m_rotation.setList(m_settings->value("filterList").toString());
    m_rotation.setKeep(m_settings->value("keepToFilter", false).toBool());
    m_showNames = m_settings->value("showNames", false).toBool();
    m_debug = m_settings->value("debug", false).toBool();
    for (const QJsonObject &visitor : Library::visitors()) {
        CompiledPreset compiled = presets::compile(visitor);
        if (compiled.ok())
            m_visitorPool.append(compiled);
    }
    m_sinceDrift.start();
    m_analyzer.setSensitivity(m_sensitivity);

    if (listen) {
        m_capture = new AudioCapture(&m_analyzer, this);
        connect(m_capture, &AudioCapture::errorChanged, this, &Backend::audioChanged);
        connect(m_capture, &AudioCapture::sourcesChanged, this, &Backend::sourcesChanged);
        m_capture->start(m_source);
        // An application or a device is called by its name from the start,
        // without the settings having been opened first. Everything and the
        // microphone need no name, so PipeWire isn't asked for one.
        if (m_source != "system" && m_source != "microphone")
            m_capture->refreshSources();
    }

    m_tick.setInterval(50);
    connect(&m_tick, &QTimer::timeout, this, &Backend::tick);
    m_tick.start();
    m_sinceChange.start();

    int index = m_library.indexOf(m_settings->value("lastPreset").toString());
    if (index < 0)
        index = qMax(0, m_library.indexOf("Neon Tunnel"));
    if (!m_library.presets().isEmpty())
        load(index, 0);
    m_starting = false;
}

Backend::~Backend() {
    // The capture goes first, by hand. Left to QObject it would outlive the
    // analyzer it feeds (members go before children), and stopping pw-cat
    // delivers whatever sound was still in the pipe.
    delete m_capture;
    m_capture = nullptr;
}

void Backend::setTheme(Theme *theme) {
    m_theme = theme;
    if (!theme)
        return;
    connect(theme, &Theme::changed, this, [this] {
        ++m_themeRevision;
        ++m_revision;
        emit paletteChanged();
        emit presetChanged();
    });
    ++m_themeRevision;
}

ThemeColors Backend::themeColors() const { return m_theme ? m_theme->colors() : ThemeColors(); }

void Backend::store(const QString &key, const QVariant &value) {
    // Showing the remembered preset at startup is not a change worth writing.
    if (!m_starting)
        m_settings->setValue(key, value);
}

// --------------------------------------------------------------- the music

const AudioFrame &Backend::audioFrame() {
    if (!m_sinceAnalysis.isValid()) {
        m_sinceAnalysis.start();
        return m_analyzer.analyze(1.0 / 60);
    }
    // Two frames asked for almost at once share one analysis.
    const double dt = m_sinceAnalysis.nsecsElapsed() / 1e9;
    if (dt < 0.003)
        return m_analyzer.frame();
    m_sinceAnalysis.restart();
    return m_analyzer.analyze(dt);
}

QVariantList Backend::levels() const {
    const AudioFrame &frame = m_analyzer.frame();
    return {qBound(0.0, frame.bassAtt / 2.0, 1.0), qBound(0.0, frame.midAtt / 2.0, 1.0),
            qBound(0.0, frame.trebAtt / 2.0, 1.0), double(frame.beat)};
}

QVariantList Backend::bands() const {
    const AudioFrame &frame = m_analyzer.frame();
    QVariantList bands;
    for (const float value : frame.bandAtt)
        bands << double(value);
    return bands;
}

double Backend::progress() const {
    return m_cycleSeconds > 0 ? qBound(0.0, m_sinceChange.elapsed() / 1000.0 / m_cycleSeconds, 1.0) : 0.0;
}

void Backend::tick() {
    const AudioFrame &frame = m_analyzer.frame();
    if (frame.beats != m_lastBeats) {
        m_lastBeats = frame.beats;
        if (!m_clock.isValid())
            m_clock.start();
        m_beatTimes << m_clock.elapsed();
        while (m_beatTimes.size() > 9)
            m_beatTimes.removeFirst();
    }
    if (frame.silent != m_silent) {
        m_silent = frame.silent;
        emit audioChanged();
    }
    if (m_metering)
        emit levelsChanged();
    // The debug overlay names what is being listened to and says whether it
    // is there, so while it is up that is looked into now and then.
    if (m_debug && m_capture && (!m_sinceSources.isValid() || m_sinceSources.elapsed() > 5000)) {
        m_sinceSources.restart();
        m_capture->refreshSources();
    }
    // Colors drifting on their own, the way G-Force cycles its ColorMaps
    // apart from everything else: the engine fades each new palette in.
    if (m_colorDrift && m_sinceDrift.elapsed() / 1000.0 > m_driftAfter) {
        m_sinceDrift.restart();
        m_driftAfter = 22 + QRandomGenerator::global()->bounded(20.0);
        // Kept to a family of colors, the drift stays in it too.
        m_driftPalette =
            presets::driftPalette(m_driftPalette, m_rotation.keep() ? m_rotation.color() : QString());
        emit paletteChanged();
    }
    // Moving on: when the time is up, wait for the next beat (a few seconds
    // at most) so the change lands with the music. Never while a preset is
    // being edited or has unsaved work.
    if (m_cycleSeconds <= 0 || m_locked || m_editing || dirty())
        return;
    const double elapsed = m_sinceChange.elapsed() / 1000.0;
    if (elapsed >= m_cycleSeconds && (frame.beat > 0.75f || frame.silent || elapsed >= m_cycleSeconds + 4) &&
        m_rotation.pool().size() >= 2)
        advance(true);
}

// ----------------------------------------------------------- which preset

QVariantList Backend::presets() const {
    QVariantList list;
    for (const auto &preset : m_library.presets())
        list << QVariantMap{{"name", preset.name}, {"author", preset.author}, {"builtin", preset.builtin},
                            {"shadowsBuiltin", preset.shadowsBuiltin}, {"family", preset.family},
                            {"features", preset.features}, {"matches", m_rotation.matches(preset)},
                            {"favorite", m_rotation.contains(Rotation::favorites(), preset.name)},
                            {"skipped", m_rotation.contains(Rotation::skipped(), preset.name)},
                            {"lists", m_rotation.listsOf(preset.name)}};
    return list;
}

QVariantList Backend::presetLists() const {
    QVariantList lists;
    for (const Rotation::List &list : m_rotation.lists()) {
        int count = 0;
        for (const QString &name : list.presets)
            count += m_library.indexOf(name) >= 0;
        lists << QVariantMap{{"name", list.name}, {"count", count}};
    }
    return lists;
}

// ------------------------------------------------------------ the filters

QVariantList Backend::colorFamilies() const {
    // Each family's swatch, with every hue round the wheel for many and the
    // desktop's own colors for the theme.
    static const QList<std::pair<QString, QStringList>> labels = {
        {"Reds and pinks", {"#ff4d6d"}},     {"Oranges and yellows", {"#ffa62b"}},
        {"Greens", {"#3ccf6e"}},             {"Blues and teals", {"#2f8cff"}},
        {"Violets", {"#9b6bff"}},            {"Many colors", {"#ff0040", "#ffe600", "#00e5ff", "#b400ff"}},
        {"Grays", {"#9aa3ad"}},              {"The desktop theme's colors", {}}};
    QVariantList list;
    const QStringList names = presets::colorFamilies();
    for (int i = 0; i < names.size(); ++i) {
        QStringList colors = labels.value(i).second;
        if (names[i] == "theme")
            for (const QString &stop : presets::paletteStops("theme").mid(0, 4))
                colors << presets::resolveColor(stop, themeColors()).name();
        list << QVariantMap{{"name", names[i]}, {"label", labels.value(i).first}, {"colors", colors}};
    }
    return list;
}

QVariantList Backend::featureList() const {
    QVariantList list;
    for (const QString &feature : presets::featureNames())
        list << QVariantMap{{"name", feature},
                            {"label", feature == "ranges" ? QString("Six ranges") : presets::fieldDoc("type", feature).label}};
    return list;
}

// ------------------------------------------------------------------ about

namespace {

QString compiler() {
#if defined(_MSC_VER) && !defined(__clang__)
    return QString("MSVC %1.%2.%3").arg(_MSC_VER / 100).arg(_MSC_VER % 100).arg(_MSC_FULL_VER % 100000);
#elif defined(__clang__)
    return QString("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QString("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return QString("an unknown compiler");
#endif
}

} // namespace

QVariantMap Backend::about() const {
    int yours = 0;
    for (const auto &preset : m_library.presets())
        yours += preset.builtin ? 0 : 1;
#ifdef Q_OS_WIN
    const QString sound = "WASAPI", titles = "Windows media controls";
#else
    const QString sound = "PipeWire", titles = "MPRIS";
#endif
    return {{"version", QCoreApplication::applicationVersion()},
            {"qt", QString::fromLatin1(qVersion())},
            {"qtBuilt", QString::fromLatin1(QT_VERSION_STR)},
            {"compiler", compiler()},
            {"system", QSysInfo::prettyProductName()},
            {"kernel", QSysInfo::kernelVersion()},
            {"architecture", QSysInfo::currentCpuArchitecture()},
            {"display", QGuiApplication::platformName()},
            {"sound", sound},
            {"titles", titles},
            {"builtinPresets", int(m_library.presets().size()) - yours},
            {"yourPresets", yours},
            // theme and accent are the desktop theme's, and counted apart.
            {"palettes", int(presets::paletteNames().size()) - 2},
            {"visitors", int(m_visitorPool.size())},
            {"presetsFolder", QDir::toNativeSeparators(m_library.userDirectory())},
            {"settingsFile", QDir::toNativeSeparators(m_settings->fileName())}};
}

void Backend::copyText(const QString &text) {
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

bool Backend::openFolder(const QString &which) {
    const QString folder = which == "presets"    ? m_library.userDirectory()
                           : which == "settings" ? QFileInfo(m_settings->fileName()).absolutePath()
                                                 : QString();
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void Backend::setFilterColor(const QString &family) {
    if (!m_rotation.setColor(family))
        return;
    store("filterColor", m_rotation.color());
    emit presetsChanged();
}

void Backend::setFilterFeatures(const QStringList &features) {
    if (!m_rotation.setFeatures(features))
        return;
    store("filterFeatures", m_rotation.features());
    emit presetsChanged();
}

void Backend::setFilterList(const QString &list) {
    if (!m_rotation.setList(list))
        return;
    store("filterList", m_rotation.list());
    emit presetsChanged();
}

bool Backend::toggleInList(const QString &list, const QString &preset) {
    QString error;
    if (!m_rotation.toggle(list, preset, &error)) {
        emit notice(error);
        return false;
    }
    emit presetsChanged();
    return true;
}

bool Backend::toggleFavorite(const QString &preset) {
    if (!preset.isEmpty())
        return toggleInList(Rotation::favorites(), preset);
    return toggleCurrentIn(Rotation::favorites());
}

bool Backend::toggleSkipped() {
    return toggleCurrentIn(Rotation::skipped()) && m_rotation.contains(Rotation::skipped(), m_currentName);
}

bool Backend::toggleCurrentIn(const QString &list) {
    if (m_library.indexOf(m_currentName) < 0) {
        emit notice(tr("Save %1 first to put it in a list").arg(m_currentName));
        return false;
    }
    if (!toggleInList(list, m_currentName))
        return false;
    emit notice(m_rotation.contains(list, m_currentName) ? tr("Added %1 to %2").arg(m_currentName, list)
                                                         : tr("Took %1 out of %2").arg(m_currentName, list));
    return true;
}

bool Backend::newList(const QString &name, const QString &preset) {
    QString error;
    if (!m_rotation.addList(name, &error) ||
        (!preset.isEmpty() && !m_rotation.toggle(name, preset, &error))) {
        emit notice(error);
        emit presetsChanged();
        return false;
    }
    emit presetsChanged();
    return true;
}

bool Backend::renameList(const QString &from, const QString &to) {
    QString error;
    if (!m_rotation.renameList(from, to, &error)) {
        emit notice(error);
        return false;
    }
    store("filterList", m_rotation.list());
    emit presetsChanged();
    return true;
}

bool Backend::deleteList(const QString &name) {
    QString error;
    if (!m_rotation.removeList(name, &error)) {
        emit notice(error);
        return false;
    }
    store("filterList", m_rotation.list());
    emit presetsChanged();
    emit notice(tr("Deleted the list %1").arg(name.trimmed()));
    return true;
}

void Backend::toggleFeature(const QString &feature) {
    QStringList features = m_rotation.features();
    if (!features.removeAll(feature))
        features << feature;
    setFilterFeatures(features);
}

void Backend::clearFilter() {
    setFilterColor({});
    setFilterFeatures({});
    setFilterList({});
}

void Backend::setKeepToFilter(bool keep) {
    if (!m_rotation.setKeep(keep))
        return;
    store("keepToFilter", keep);
    emit presetsChanged();
}

bool Backend::currentIsYours() const {
    const int index = m_library.indexOf(m_currentName);
    return index >= 0 && !m_library.presets()[index].builtin;
}

bool Backend::currentShadowsBuiltin() const {
    const int index = m_library.indexOf(m_currentName);
    return index >= 0 && m_library.presets()[index].shadowsBuiltin;
}

void Backend::load(int index, double blend) {
    if (index < 0 || index >= m_library.presets().size())
        return;
    const PresetInfo info = m_library.presets()[index];
    QString error;
    QJsonObject json = m_library.load(index, &error);
    if (json.isEmpty()) {
        emit notice(tr("Could not open %1: %2").arg(info.name, error));
        return;
    }
    if (json.value("name").toString().trimmed().isEmpty())
        json["name"] = info.name;
    adopt(json, info.name, true, blend);
}

void Backend::adopt(const QJsonObject &json, const QString &name, bool saved, double blend, bool remember) {
    m_json = json;
    m_saved = saved ? json : QJsonObject();
    m_currentName = name;
    m_mashupParts.clear();
    m_rotation.shown(name);
    m_undo.clear();
    m_redo.clear();
    m_lastEditKey.clear();
    m_engineError.clear();
    m_sinceChange.restart();
    ++m_revision;
    recompile(blend);
    if (saved && remember)
        store("lastPreset", name);
    emit currentChanged();
    emit presetChanged();
    emit layersChanged();
    emit paletteChanged();
}

bool Backend::open(const QString &nameOrPath, QString *error) {
    const int index = m_library.indexOf(nameOrPath);
    if (index >= 0) {
        load(index, 0);
        return true;
    }
    QString message;
    QJsonObject json = m_library.find(nameOrPath, &message);
    if (json.isEmpty()) {
        if (error)
            *error = message;
        return false;
    }
    if (json.value("name").toString().trimmed().isEmpty())
        json["name"] = QFileInfo(nameOrPath).completeBaseName();
    adopt(json, json.value("name").toString(), true, 0);
    return true;
}

// A change asked for by hand is quick; one that comes round by itself takes
// its time (G-Force keeps the same two speeds).
void Backend::next() { load(m_rotation.step(m_currentName, 1), quickBlend()); }

void Backend::previous() { load(m_rotation.step(m_currentName, -1), quickBlend()); }

void Backend::random() { load(m_rotation.random(m_currentName), quickBlend()); }

bool Backend::select(const QString &name) {
    const int index = m_library.indexOf(name);
    if (index < 0)
        return false;
    load(index, quickBlend());
    return true;
}

void Backend::advance(bool automatic) {
    const double blend = automatic ? m_blendSeconds : quickBlend();
    if (m_library.presets().isEmpty())
        return;
    if (m_mashups && QRandomGenerator::global()->bounded(100) < 40 && mashup(blend))
        return;
    load(m_shuffle ? m_rotation.random(m_currentName) : m_rotation.step(m_currentName, 1), blend);
}

bool Backend::mashup() { return mashup(quickBlend()); }

// MilkDrop's mash-up, with G-Force's idea that the parts of a picture are
// separate things: the motion comes from one preset, what is drawn from
// another, the colors and finish from a third, the background from a
// fourth. Parts that lean on each other's variables don't always fit, so a
// draw that doesn't compile is thrown back and another is tried.
bool Backend::mashup(double blend) {
    const auto &all = m_library.presets();
    // Kept to the filters, the parts come from the presets that match.
    const QList<int> from = m_rotation.pool();
    if (from.size() < 3)
        return false;
    auto *dice = QRandomGenerator::global();
    const auto draw = [&] { return from.at(dice->bounded(int(from.size()))); };
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int motionIndex = draw(), layersIndex = draw();
        const int lookIndex = draw(), backIndex = draw();
        if (motionIndex == layersIndex)
            continue;
        const QJsonObject motion = m_library.load(motionIndex), drawn = m_library.load(layersIndex);
        const QJsonObject look = m_library.load(lookIndex), back = m_library.load(backIndex);
        if (motion.isEmpty() || drawn.isEmpty() || look.isEmpty())
            continue;
        // Some presets move only through their layers (a pendulum, a train
        // of gears) and have a still picture underneath. Lending that
        // stillness to another preset's layers leaves them standing there,
        // under a name that promises the lender.
        if (!presets::moves(motion))
            continue;
        // A preset with nothing to draw leaves a mash-up of its layers black.
        if (!presets::draws(drawn))
            continue;
        const QString name = all[motionIndex].name + " × " + all[layersIndex].name;
        QJsonObject json{{"name", name}, {"description", tr("A mash-up. Save it to keep it.")}};
        const auto take = [&json](const QJsonObject &from, const QStringList &keys) {
            for (const auto &key : keys)
                if (from.contains(key))
                    json[key] = from.value(key);
        };
        take(motion, {"feedback", "pixel"});
        take(drawn, {"layers", "init", "frame"});
        take(look, {"post", "shade", "palette"});
        take(back, {"background"});
        // How much light builds up, how bright it comes out and whether it
        // is colored by brightness go with what is drawn: each preset sets
        // these against its own layers. Dense layers made for a quick fade,
        // under a slow one, burn out, a strong glow made for faint layers
        // burns out dense ones, and layers drawn in white for colorize stay
        // white without it.
        for (const auto &[section, key] : presets::lightSettings()) {
            QJsonObject part = json.value(section).toObject();
            const QJsonValue value = drawn.value(section).toObject().value(key);
            if (value.isUndefined())
                part.remove(key);
            else
                part[key] = value;
            if (part.isEmpty())
                json.remove(section);
            else
                json[section] = part;
        }
        const CompiledPreset compiled = presets::compile(json);
        if (!compiled.ok())
            continue;
        // Pixel code that sets the decay itself would bring the lender's
        // fade back with its motion, so such a preset lends no motion.
        if (compiled.pixel.assigns(compiled.vars.find("decay")))
            continue;
        adopt(json, name, true, blend, false);
        m_mashupParts = {all[motionIndex].name, all[layersIndex].name, all[lookIndex].name, all[backIndex].name};
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- editing

void Backend::recompile(double blend) {
    CompiledPreset result = presets::compile(m_json);
    m_problems = result.problems;
    // A preset with a mistake in it keeps showing its last good version, so
    // typing a formula never blanks the screen.
    if (result.ok()) {
        m_compiled = std::move(result);
        ++m_compiledRevision;
        m_blend = blend;
    }
    emit problemsChanged();
}

void Backend::setEngineError(const QString &error) {
    if (error == m_engineError)
        return;
    m_engineError = error;
    emit problemsChanged();
}

QVariantList Backend::problems() const {
    QVariantList list;
    for (const auto &problem : m_problems)
        list << QVariantMap{{"path", problem.path}, {"message", problem.message}, {"error", problem.error}};
    if (!m_engineError.isEmpty())
        list << QVariantMap{{"path", QString()}, {"message", m_engineError}, {"error", true}};
    return list;
}

QString Backend::problemAt(const QString &path) const {
    for (const auto &problem : m_problems)
        if (problem.path == path)
            return problem.message;
    return {};
}

// Snapshots the preset before a change. Changes to the same setting in
// quick succession (dragging a slider, typing) share one undo step.
void Backend::pushUndo(const QString &key) {
    const bool sameEdit = !key.isEmpty() && key == m_lastEditKey && m_lastEdit.isValid() && m_lastEdit.elapsed() < 1000;
    m_lastEditKey = key;
    m_lastEdit.restart();
    if (sameEdit)
        return;
    m_undo.append(m_json);
    if (m_undo.size() > 200)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::changed(bool structure) {
    ++m_revision;
    recompile(0);
    emit presetChanged();
    if (structure)
        emit layersChanged();
    emit paletteChanged();
}

const Field *Backend::fieldAt(const QStringList &parts) const {
    QString section, key;
    if (parts.size() == 2 && (parts[0] == "feedback" || parts[0] == "post" || parts[0] == "background")) {
        section = parts[0];
        key = parts[1];
    } else if (parts.size() == 3 && parts[0] == "layers") {
        section = m_json.value("layers").toArray().at(parts[1].toInt()).toObject().value("type").toString();
        key = parts[2];
    } else {
        return nullptr;
    }
    for (const Field &field : presets::fields(section))
        if (QLatin1String(field.key) == key)
            return &field;
    return nullptr;
}

QVariant Backend::param(const QString &path) const {
    const QStringList parts = path.split('.');
    QJsonValue value;
    if (parts.size() == 1) {
        value = m_json.value(path);
        if (path == "palette") {
            if (value.isArray()) {
                QStringList stops;
                for (const auto &stop : value.toArray())
                    stops << stop.toString();
                return stops.join(", ");
            }
            return value.isString() ? value.toString() : QString("theme");
        }
        return value.isArray() ? joined(value) : value.toString();
    }
    if (parts[0] == "layers" && parts.size() == 3)
        value = m_json.value("layers").toArray().at(parts[1].toInt()).toObject().value(parts[2]);
    else if (parts.size() == 2)
        value = m_json.value(parts[0]).toObject().value(parts[1]);
    const Field *field = fieldAt(parts);
    if (value.isArray())
        return joined(value);
    if (!value.isUndefined() && !value.isNull())
        return value.toVariant();
    if (!field)
        return {};
    switch (field->type) {
    case Field::Number:
    case Field::Integer: return field->def;
    case Field::Toggle: return field->def != 0;
    case Field::Choice:
    case Field::Color: return field->text;
    case Field::Code: return QString();
    }
    return {};
}

bool Backend::isSet(const QString &path) const {
    const QStringList parts = path.split('.');
    if (parts.size() == 1)
        return m_json.contains(path);
    if (parts[0] == "layers" && parts.size() == 3)
        return m_json.value("layers").toArray().at(parts[1].toInt()).toObject().contains(parts[2]);
    return parts.size() == 2 && m_json.value(parts[0]).toObject().contains(parts[1]);
}

bool Backend::isEdited(const QString &path) const {
    // A preset that has never been saved has nothing to differ from.
    if (m_saved.isEmpty())
        return false;
    const QStringList parts = path.split('.');
    const auto at = [&parts](const QJsonObject &preset) {
        QJsonValue value = preset.value(parts[0]);
        for (int i = 1; i < parts.size(); ++i)
            value = value.isArray() ? value.toArray().at(parts[i].toInt()) : value.toObject().value(parts[i]);
        return value;
    };
    return at(m_json) != at(m_saved);
}

void Backend::setParam(const QString &path, const QVariant &value) {
    const QStringList parts = path.split('.');
    const Field *field = fieldAt(parts);
    const bool isText = value.typeId() == QMetaType::QString;
    const QString text = value.toString().trimmed();

    // What goes in the file: a number when it is one, a formula otherwise;
    // nothing at all (so the default applies) when the box is emptied.
    QJsonValue json;
    if (parts.size() == 1) {
        if (path == "palette" && text.contains(',')) {
            QJsonArray stops;
            for (const auto &stop : text.split(',', Qt::SkipEmptyParts))
                stops.append(stop.trimmed());
            json = stops;
        } else if (CodeKeys.contains(path)) {
            json = value.toString().trimmed().isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(value.toString());
        } else if (!text.isEmpty() || path == "name") {
            json = text;
        } else {
            json = QJsonValue(QJsonValue::Undefined);
        }
    } else if (!field) {
        return;
    } else if (field->type == Field::Code) {
        json = text.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(value.toString());
    } else if (field->type == Field::Choice || field->type == Field::Color) {
        json = text.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(text);
    } else if (isText) {
        bool isNumber = false;
        const double number = text.toDouble(&isNumber);
        if (text.isEmpty())
            json = QJsonValue(QJsonValue::Undefined);
        else if (field->type == Field::Toggle && (text == "true" || text == "false"))
            json = text == "true";
        else if (isNumber)
            json = field->type == Field::Integer ? double(qRound64(number)) : number;
        else
            json = text;
    } else if (field->type == Field::Toggle) {
        json = value.toBool();
    } else {
        json = field->type == Field::Integer ? double(qRound64(value.toDouble())) : value.toDouble();
    }

    auto assign = [&](QJsonObject object, const QString &key) {
        if (json.isUndefined())
            object.remove(key);
        else
            object[key] = json;
        return object;
    };
    QJsonObject updated = m_json;
    if (parts.size() == 1) {
        updated = assign(updated, path);
    } else if (parts[0] == "layers") {
        QJsonArray layers = updated.value("layers").toArray();
        const int index = parts[1].toInt();
        if (index < 0 || index >= layers.size())
            return;
        layers[index] = assign(layers[index].toObject(), parts[2]);
        updated["layers"] = layers;
    } else {
        updated[parts[0]] = assign(updated.value(parts[0]).toObject(), parts[1]);
    }
    if (updated == m_json)
        return;
    pushUndo(path);
    m_json = updated;
    changed(parts.value(2) == "enabled" || parts.value(2) == "name");
}

void Backend::resetParam(const QString &path) {
    if (isSet(path) && path != "name")
        setParam(path, QString());
}

QString Backend::colorPreview(const QString &color) const {
    const QString text = color.trimmed();
    if (text.startsWith('#') || text == "white" || text == "black")
        return QColor(text).isValid() ? QColor(text).name() : QString();
    if (presets::themeColorNames().contains(text))
        return presets::resolveColor(text, themeColors()).name();
    return {};
}

QStringList Backend::paletteColors() const {
    const QString palette = effectivePalette();
    QStringList stops = palette.isEmpty() ? m_compiled.palette : presets::paletteStops(palette);
    QStringList colors;
    for (const auto &stop : stops)
        colors << presets::resolveColor(stop, themeColors()).name();
    return colors;
}

QVariantList Backend::layers() const {
    QVariantList list;
    for (const auto &value : m_json.value("layers").toArray()) {
        const QJsonObject layer = value.toObject();
        list << QVariantMap{{"type", layer.value("type").toString()}, {"name", layer.value("name").toString()}};
    }
    return list;
}

void Backend::addLayer(const QString &type) {
    if (!presets::layerTypes().contains(type))
        return;
    QJsonArray layers = m_json.value("layers").toArray();
    if (layers.size() >= 24) {
        emit notice(tr("A preset can have at most 24 layers"));
        return;
    }
    pushUndo({});
    layers.append(presets::starterLayer(type));
    m_json["layers"] = layers;
    changed(true);
}

void Backend::removeLayer(int index) {
    QJsonArray layers = m_json.value("layers").toArray();
    if (index < 0 || index >= layers.size())
        return;
    pushUndo({});
    layers.removeAt(index);
    m_json["layers"] = layers;
    changed(true);
}

void Backend::moveLayer(int index, int delta) {
    QJsonArray layers = m_json.value("layers").toArray();
    const int target = index + delta;
    if (index < 0 || index >= layers.size() || target < 0 || target >= layers.size())
        return;
    pushUndo({});
    const QJsonValue moved = layers.takeAt(index);
    layers.insert(target, moved);
    m_json["layers"] = layers;
    changed(true);
}

void Backend::duplicateLayer(int index) {
    QJsonArray layers = m_json.value("layers").toArray();
    if (index < 0 || index >= layers.size() || layers.size() >= 24)
        return;
    pushUndo({});
    layers.insert(index + 1, layers.at(index));
    m_json["layers"] = layers;
    changed(true);
}

QString Backend::presetText() const { return QString::fromUtf8(QJsonDocument(m_json).toJson(QJsonDocument::Indented)); }

bool Backend::setPresetText(const QString &text) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        const int line = int(text.toUtf8().left(error.offset).count('\n')) + 1;
        m_problems = {Problem{"json",
                              error.error != QJsonParseError::NoError
                                  ? tr("%1 (line %2)").arg(error.errorString()).arg(line)
                                  : tr("A preset is a JSON object"),
                              true}};
        emit problemsChanged();
        return false;
    }
    if (document.object() == m_json) {
        recompile(0);
        return true;
    }
    pushUndo("json");
    m_json = document.object();
    changed(true);
    return true;
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_json);
    m_json = m_undo.takeLast();
    m_lastEditKey.clear();
    changed(true);
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_json);
    m_json = m_redo.takeLast();
    m_lastEditKey.clear();
    changed(true);
}

// ------------------------------------------------------------- keeping it

bool Backend::save() { return write(true); }

bool Backend::saveAs(const QString &name) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return false;
    if (takenByAnother(wanted)) {
        emit notice(tr("There is already a preset called %1").arg(wanted));
        return false;
    }
    if (m_json.value("name").toString() != wanted) {
        pushUndo({});
        m_json["name"] = wanted;
        m_json.remove("author");
        ++m_revision;
        recompile(0);
    }
    return write(false);
}

bool Backend::takenByAnother(const QString &name) const {
    const int index = m_library.indexOf(name);
    return index >= 0 && !m_library.presets()[index].builtin &&
           name.trimmed().compare(m_currentName, Qt::CaseInsensitive) != 0;
}

bool Backend::write(bool rename) {
    const QString name = m_json.value("name").toString().trimmed();
    if (name.isEmpty()) {
        emit notice(tr("Give the preset a name first"));
        return false;
    }
    // The name typed may be another of the user's presets, whose file the
    // library would otherwise write this over.
    if (takenByAnother(name)) {
        emit notice(tr("There is already a preset called %1").arg(name));
        return false;
    }
    // One of the user's own presets under a new name: once it is written
    // under that, the file it was in goes, and a built-in it stood in for
    // comes back.
    const QString was =
        rename && currentIsYours() && name.compare(m_currentName, Qt::CaseInsensitive) != 0 ? m_currentName : QString();
    QString error;
    if (m_library.save(m_json, &error).isEmpty()) {
        emit notice(error);
        return false;
    }
    QString message = tr("Saved %1").arg(name);
    if (!was.isEmpty() && m_library.remove(was, &error)) {
        message = tr("Renamed %1 to %2").arg(was, name);
        // Its places in the lists go with it.
        m_rotation.renamed(was, name);
    } else if (!was.isEmpty()) {
        message = tr("Saved %1 (%2)").arg(name, error);
    }
    m_saved = m_json;
    m_currentName = name;
    store("lastPreset", name);
    emit presetsChanged();
    emit currentChanged();
    emit presetChanged();
    emit notice(message);
    return true;
}

void Backend::revert() {
    if (!dirty() || m_saved.isEmpty())
        return;
    pushUndo({});
    m_json = m_saved;
    changed(true);
}

bool Backend::newPreset(const QString &name) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return false;
    if (m_library.indexOf(wanted) >= 0) {
        emit notice(tr("There is already a preset called %1").arg(wanted));
        return false;
    }
    adopt(presets::blank(wanted), wanted, false, m_blendSeconds);
    return true;
}

bool Backend::deletePreset(const QString &name) {
    const int index = m_library.indexOf(name);
    if (index < 0) {
        emit notice(tr("%1 isn't one of your presets, so there is nothing to delete").arg(name));
        return false;
    }
    if (m_library.presets()[index].builtin) {
        emit notice(tr("Built-in presets can't be deleted"));
        return false;
    }
    const QString deleted = m_library.presets()[index].name;
    const bool showing = index == m_library.indexOf(m_currentName);
    QString error;
    if (!m_library.remove(deleted, &error)) {
        emit notice(error);
        return false;
    }
    m_rotation.removed(deleted);
    emit presetsChanged();
    // A built-in the deleted preset stood in for is back.
    const int builtin = m_library.indexOf(deleted);
    emit notice(builtin >= 0 ? tr("Deleted your copy of %1; the built-in is back").arg(deleted)
                             : tr("Deleted %1").arg(deleted));
    // Showing, it makes way for that built-in, or otherwise whatever now
    // sits in its place in the list.
    if (showing)
        load(builtin >= 0 ? builtin : qMin(index, int(m_library.presets().size()) - 1), m_blendSeconds);
    else
        emit currentChanged();
    return true;
}

// ---------------------------------------------------------------- settings

void Backend::setSensitivity(double sensitivity) {
    sensitivity = qBound(0.2, sensitivity, 4.0);
    if (qFuzzyCompare(sensitivity, m_sensitivity))
        return;
    m_sensitivity = sensitivity;
    m_analyzer.setSensitivity(sensitivity);
    store("sensitivity", sensitivity);
    emit settingsChanged();
}

void Backend::setCycleSeconds(int seconds) {
    seconds = qBound(0, seconds, 3600);
    if (seconds == m_cycleSeconds)
        return;
    m_cycleSeconds = seconds;
    m_sinceChange.restart();
    store("cycleSeconds", seconds);
    emit settingsChanged();
}

void Backend::setShuffle(bool shuffle) {
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    store("shuffle", shuffle);
    emit settingsChanged();
}

void Backend::setLocked(bool locked) {
    if (locked == m_locked)
        return;
    m_locked = locked;
    m_sinceChange.restart();
    store("locked", locked);
    emit settingsChanged();
}

void Backend::setPaletteOverride(const QString &name) {
    const QString wanted = presets::paletteNames().contains(name) ? name : QString();
    if (wanted == m_paletteOverride)
        return;
    m_paletteOverride = wanted;
    store("paletteOverride", wanted);
    emit settingsChanged();
    emit paletteChanged();
}

// Steps through "each preset's own colors", then every named palette.
QString Backend::cyclePalette() {
    const QStringList names = presets::paletteNames();
    const int index = names.indexOf(m_paletteOverride);
    setPaletteOverride(index + 1 < names.size() ? names[index + 1] : QString());
    return m_paletteOverride;
}

void Backend::setQuality(double quality) {
    quality = qBound(0.25, quality, 1.0);
    if (qFuzzyCompare(quality, m_quality))
        return;
    m_quality = quality;
    store("quality", quality);
    emit settingsChanged();
}

void Backend::setMaxLines(int lines) {
    lines = lines >= 2160 ? 2160 : 1440;
    if (lines == m_maxLines)
        return;
    m_maxLines = lines;
    store("maxLines", lines);
    emit settingsChanged();
}

void Backend::setGlass(bool glass) {
    if (glass == m_glass)
        return;
    m_glass = glass;
    store("glass", glass);
    emit settingsChanged();
}

void Backend::setGlassOpacity(double opacity) {
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(opacity + 1, m_glassOpacity + 1))
        return;
    m_glassOpacity = opacity;
    store("glassOpacity", opacity);
    emit settingsChanged();
}

void Backend::setTitleStyle(const QString &style) {
    if (style == m_titleStyle || !QStringList{"corner", "picture", "off"}.contains(style))
        return;
    m_titleStyle = style;
    store("titleStyle", style);
    emit settingsChanged();
}

QString Backend::cycleTitleStyle() {
    static const QStringList order{"corner", "picture", "off"};
    setTitleStyle(order.at((order.indexOf(m_titleStyle) + 1) % order.size()));
    return m_titleStyle;
}

// The title as a picture: white letters on nothing, for the engine to tint
// and draw. Made here because fonts belong to the window's thread.
void Backend::setTrack(const QString &title, const QString &artist) {
    QImage image;
    if (!title.trimmed().isEmpty()) {
        QFont big = QGuiApplication::font();
        big.setPixelSize(120);
        big.setWeight(QFont::Bold);
        QFont small = QGuiApplication::font();
        small.setPixelSize(56);
        const int limit = 1900;
        const QFontMetrics bigMetrics(big), smallMetrics(small);
        const QString top = bigMetrics.elidedText(title.trimmed(), Qt::ElideRight, limit);
        const QString bottom = smallMetrics.elidedText(artist.trimmed(), Qt::ElideRight, limit);
        const int width = qMax(bigMetrics.horizontalAdvance(top), smallMetrics.horizontalAdvance(bottom)) + 48;
        const int height = bigMetrics.height() + (bottom.isEmpty() ? 0 : smallMetrics.height() + 8) + 32;
        image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(Qt::white);
        painter.setFont(big);
        painter.drawText(QRect(0, 16, width, bigMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, top);
        if (!bottom.isEmpty()) {
            painter.setFont(small);
            painter.drawText(QRect(0, 16 + bigMetrics.height() + 8, width, smallMetrics.height()),
                             Qt::AlignHCenter | Qt::AlignVCenter, bottom);
        }
    }
    m_titleImage = image;
    ++m_titleRevision;
}

void Backend::setSource(const QString &source) {
    const QString wanted = AudioCapture::isSource(source) ? source : QString("system");
    if (wanted == m_source)
        return;
    m_source = wanted;
    store("source", wanted);
    if (m_capture) {
        m_capture->start(wanted);
        m_capture->refreshSources();
    }
    emit settingsChanged();
    emit sourcesChanged();
}

QVariantList Backend::sources() const {
    QVariantList list = m_capture ? m_capture->sources() : QVariantList();
    if (list.isEmpty())
        list = AudioCapture::describe({}, m_source);
    return list;
}

void Backend::refreshSources() {
    if (m_capture)
        m_capture->refreshSources();
}

QString Backend::sourceLabel() const {
    // Until PipeWire has been asked there is no knowing whether a device is
    // plugged in or an application playing, so nothing is said about it.
    const QVariantList known = m_capture ? m_capture->sources() : QVariantList();
    for (const auto &entry : known.isEmpty() ? AudioCapture::describe({}, {}) : known)
        if (entry.toMap().value("id").toString() == m_source)
            return entry.toMap().value("label").toString();
    return m_source.section(':', 1);
}

void Backend::setTransition(const QString &style) {
    if (style == m_transition || !presets::transitionStyles().contains(style))
        return;
    m_transition = style;
    store("transition", style);
    emit settingsChanged();
}

void Backend::setMashups(bool on) {
    if (on == m_mashups)
        return;
    m_mashups = on;
    store("mashups", on);
    emit settingsChanged();
}

void Backend::setVisitors(bool on) {
    if (on == m_visitors)
        return;
    m_visitors = on;
    store("visitors", on);
    emit settingsChanged();
}

void Backend::setColorDrift(bool on) {
    if (on == m_colorDrift)
        return;
    m_colorDrift = on;
    m_driftPalette = QString();
    m_sinceDrift.restart();
    m_driftAfter = 12;
    store("colorDrift", on);
    emit settingsChanged();
    emit paletteChanged();
}

void Backend::setBlendSeconds(double seconds) {
    seconds = qBound(0.0, seconds, 20.0);
    if (qFuzzyCompare(seconds + 1, m_blendSeconds + 1))
        return;
    m_blendSeconds = seconds;
    store("blendSeconds", seconds);
    emit settingsChanged();
}

void Backend::setShowNames(bool show) {
    if (show == m_showNames)
        return;
    m_showNames = show;
    store("showNames", show);
    emit settingsChanged();
}

void Backend::setDebug(bool debug) {
    if (debug == m_debug)
        return;
    m_debug = debug;
    store("debug", debug);
    emit settingsChanged();
}

void Backend::setMetering(bool metering) {
    if (metering == m_metering)
        return;
    m_metering = metering;
    emit meteringChanged();
}

QVariantMap Backend::stats() const {
    const AudioFrame &frame = m_analyzer.frame();
    // Why the preset isn't about to change, in the order tick() checks.
    QString waiting;
    if (m_cycleSeconds <= 0)
        waiting = "never";
    else if (m_locked)
        waiting = "held";
    else if (m_editing)
        waiting = "editing";
    else if (dirty())
        waiting = "unsaved";
    else if (m_rotation.pool().size() < 2)
        waiting = "alone";
    QVariantList bands;
    for (int band = 0; band < AudioFrame::Bands; ++band)
        bands << QVariantMap{{"name", QString(bandInfo(band).name)}, {"level", frame.bandAtt[band]}};
    // A tempo from the gaps between the last few beats: the middle one, so
    // a missed or doubled beat doesn't throw it. Nothing until there are
    // enough of them, or once they have stopped for a while.
    double tempo = 0;
    if (m_beatTimes.size() >= 5 && m_clock.elapsed() - m_beatTimes.last() < 3000) {
        QList<qint64> gaps;
        for (int i = 1; i < m_beatTimes.size(); ++i)
            gaps << m_beatTimes[i] - m_beatTimes[i - 1];
        std::sort(gaps.begin(), gaps.end());
        const qint64 gap = gaps[gaps.size() / 2];
        if (gap > 0)
            tempo = 60000.0 / double(gap);
    }
    // What the preset is made of, kind by kind, with how each kind's light
    // meets what is under it: "wave ×6 add, bars over".
    QList<std::pair<QString, QString>> kinds;
    QList<int> counts;
    for (const auto &value : m_json.value("layers").toArray()) {
        const QJsonObject layer = value.toObject();
        const QString type = layer.value("type").toString();
        QString blend = layer.value("blend").toString();
        for (const Field &field : presets::fields(type))
            if (blend.isEmpty() && field.key == "blend")
                blend = field.text;
        const qsizetype at = kinds.indexOf(std::pair(type, blend));
        if (at < 0) {
            kinds.append({type, blend});
            counts.append(1);
        } else {
            ++counts[at];
        }
    }
    QStringList layerSummary;
    for (qsizetype i = 0; i < kinds.size(); ++i) {
        const QString many = counts[i] > 1 ? QString(" ×%1").arg(counts[i]) : QString();
        layerSummary << (kinds[i].first + many + " " + kinds[i].second).trimmed();
    }
    int errors = 0, warnings = 0;
    for (const auto &problem : m_problems)
        ++(problem.error ? errors : warnings);
    if (!m_engineError.isEmpty())
        ++errors;
    const int index = m_library.indexOf(m_currentName);
    const QString path = index >= 0 && !m_library.presets()[index].builtin ? m_library.presets()[index].path : QString();
    return {{"waiting", waiting},
            {"bands", bands},
            {"nextIn", qMax(0.0, m_cycleSeconds - m_sinceChange.elapsed() / 1000.0)},
            {"layers", int(m_json.value("layers").toArray().size())},
            {"sampleRate", m_analyzer.sampleRate()},
            {"bass", frame.bassAtt},
            {"mid", frame.midAtt},
            {"treb", frame.trebAtt},
            {"vol", frame.volAtt},
            {"beat", frame.beat},
            {"beats", frame.beats},
            {"tempo", tempo},
            {"layerSummary", layerSummary.join(", ")},
            {"parts", m_mashupParts},
            {"errors", errors},
            {"warnings", warnings},
            {"path", path},
            {"sensitivity", m_sensitivity},
            {"source", sourceLabel()}};
}

void Backend::setEditing(bool editing) {
    if (editing == m_editing)
        return;
    m_editing = editing;
    m_sinceChange.restart();
    emit settingsChanged();
    emit paletteChanged();
}
