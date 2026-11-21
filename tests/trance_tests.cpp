#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QMetaProperty>
#include <QLocalSocket>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cmath>
#include <thread>

#include "audio.h"
#include "audiocapture.h"
#include "backend.h"
#include "cli.h"
#include "engine.h"
#include "expr.h"
#include "library.h"
#include "preset.h"
#include "remote.h"
#include "theme.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <unknwn.h> // before C++/WinRT, which then takes COM's own interfaces too

#include <propsys.h>
#include <shellapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>

#include "nowplaying.h"

namespace {

// A media player as Windows sees one, for trying which titles Trance takes:
// a window that is never shown, saying what it plays to the system's media
// controls the way a browser does. Each has an application id of its own, as
// each real player has, which is what Windows tells them apart by.
class FakePlayer {
public:
    explicit FakePlayer(const wchar_t *id) {
        m_window = CreateWindowExW(0, L"STATIC", id, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
        // PKEY_AppUserModel_ID, as in propkey.h, which only declares it.
        static const PROPERTYKEY ApplicationId = {{0x9f4c2855, 0x9f79, 0x4b39, {0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3}}, 5};
        winrt::com_ptr<IPropertyStore> store;
        winrt::check_hresult(SHGetPropertyStoreForWindow(m_window, IID_PPV_ARGS(store.put())));
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_LPWSTR;
        value.pwszVal = const_cast<wchar_t *>(id);
        winrt::check_hresult(store->SetValue(ApplicationId, value));
        const auto controlsOf = winrt::get_activation_factory<winrt::Windows::Media::SystemMediaTransportControls,
                                                              ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(controlsOf->GetForWindow(
            m_window, winrt::guid_of<winrt::Windows::Media::SystemMediaTransportControls>(), winrt::put_abi(m_controls)));
        m_controls.IsEnabled(true);
        m_controls.IsPlayEnabled(true);
        m_controls.IsPauseEnabled(true);
    }
    ~FakePlayer() {
        if (m_controls)
            m_controls.IsEnabled(false);
        m_controls = nullptr;
        DestroyWindow(m_window);
    }

    void announce(const wchar_t *title, const wchar_t *artist) {
        const auto display = m_controls.DisplayUpdater();
        display.Type(winrt::Windows::Media::MediaPlaybackType::Music);
        display.MusicProperties().Title(title);
        display.MusicProperties().Artist(artist);
        display.Update();
    }
    void play(bool on) {
        m_controls.PlaybackStatus(on ? winrt::Windows::Media::MediaPlaybackStatus::Playing
                                     : winrt::Windows::Media::MediaPlaybackStatus::Paused);
    }

private:
    HWND m_window = nullptr;
    winrt::Windows::Media::SystemMediaTransportControls m_controls{nullptr};
};

QString fromWindows(const winrt::hstring &string) {
    return QString::fromWCharArray(string.c_str(), int(string.size()));
}

// The players Windows lists, each with whether it plays (4) or is paused (5)
// and its track: what a failed test has to go on. Asked on a thread of its
// own, since the answers are waited for and the main thread may not wait.
QStringList playersWindowsLists() {
    QStringList players;
    std::thread([&players] {
        winrt::init_apartment();
        try {
            using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
            const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            const auto current = manager.GetCurrentSession();
            players << "current: " + (current ? fromWindows(current.SourceAppUserModelId()) : QString("none"));
            for (const auto &session : manager.GetSessions()) {
                const auto track = session.TryGetMediaPropertiesAsync().get();
                players << QString("%1: status %2, \"%3\" by \"%4\"")
                               .arg(fromWindows(session.SourceAppUserModelId()))
                               .arg(int(session.GetPlaybackInfo().PlaybackStatus()))
                               .arg(fromWindows(track.Title()), fromWindows(track.Artist()));
            }
        } catch (const winrt::hresult_error &error) {
            players << "Windows said: " + fromWindows(error.message());
        }
        winrt::uninit_apartment();
    }).join();
    return players;
}

} // namespace
#endif

class TranceTests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ formulas

    void formulasFollowTheUsualRules()
    {
        QCOMPARE(evaluate("1 + 2 * 3"), 7.0);
        QCOMPARE(evaluate("(1 + 2) * 3"), 9.0);
        QCOMPARE(evaluate("-2 ^ 2"), -4.0);
        QCOMPARE(evaluate("2 ^ 3 ^ 2"), 512.0);
        QCOMPARE(evaluate("7 % 4"), 3.0);
        QCOMPARE(evaluate("3 > 2 ? 10 : 20"), 10.0);
        QCOMPARE(evaluate("1 < 2 && 2 < 1"), 0.0);
        QCOMPARE(evaluate("!0 + !5"), 1.0);
        QCOMPARE(evaluate("max(1, min(9, 4))"), 4.0);
        QCOMPARE(evaluate("if(above(3, 2), 5, 6)"), 5.0);
        QCOMPARE(evaluate("clamp(12, 0, 1) + mix(10, 20, 0.5)"), 16.0);
        QCOMPARE(evaluate("1e2 + .5"), 100.5);
        QVERIFY(std::abs(evaluate("sin(pi / 2) + cos(0)") - 2.0) < 1e-9);
    }

    void formulasNeverProduceInfinity()
    {
        QCOMPARE(evaluate("1 / 0"), 0.0);
        QCOMPARE(evaluate("5 % 0"), 0.0);
        QCOMPARE(evaluate("sqrt(-4)"), 2.0);
        QVERIFY(std::isfinite(evaluate("log(0) + exp(1000)")));
    }

    void statementsAssignAndCarryOver()
    {
        expr::Vars vars;
        expr::Program program;
        QString error;
        QVERIFY2(program.parse("a = 2; b = a * 3\n// a comment\nb += 1\nc = (a +\n  b)", vars, &error), qPrintable(error));
        program.run(vars.values);
        QCOMPARE(vars.get("b"), 7.0);
        QCOMPARE(vars.get("c"), 9.0);
        // The table keeps its values between runs, which is what lets a
        // preset count beats or ease toward a target.
        program.run(vars.values);
        QCOMPARE(vars.get("c"), 9.0);
        QVERIFY(vars.undefined().isEmpty());
    }

    void formulaMistakesAreExplained()
    {
        QCOMPARE(parseError("1 +"), QString("Unfinished expression"));
        QCOMPARE(parseError("sine(2)"), QString("Unknown function 'sine'"));
        QCOMPARE(parseError("lerp(1, 2, 0.5)"), QString("Unknown function 'lerp'; mix(a, b, t) does the same"));
        QCOMPARE(parseError("min(1)"), QString("min() takes 2 argument(s), not 1"));
        QCOMPARE(parseError("(1 + 2"), QString("Missing ')'"));
        QCOMPARE(parseError("2 $ 3"), QString("Unexpected '$'"));
        QCOMPARE(parseError("sin = 3"), QString("'sin' is a function and can't be assigned"));
        expr::Vars vars;
        expr::Program program;
        QVERIFY(program.parse("x = speeed * 2", vars, nullptr));
        QCOMPARE(vars.undefined(), QStringList{"speeed"});
    }

    void formulasBecomeShaderCode()
    {
        expr::Vars vars;
        vars.declare("bass");
        vars.declare("zoom", 1);
        expr::Program program;
        QVERIFY(program.parse("zoom = zoom + 0.1 * sin(rad * 10) * bass; glow = zoom / 2", vars, nullptr));
        expr::GlslScope scope;
        scope.predeclare("rad");
        const QString code = program.glsl(scope, vars);
        // The first assignment reads the frame's zoom and makes a local of it...
        QVERIFY2(code.contains(QString("float l_zoom = (V[%1]+").arg(vars.find("zoom"))), qPrintable(code));
        QVERIFY2(code.contains("sin((l_rad*10.0))"), qPrintable(code));
        // ...which later statements, and the shader around them, then use.
        QVERIFY2(code.contains("float l_glow = f_div(l_zoom,2.0);"), qPrintable(code));
        QCOMPARE(scope.ref("zoom", vars), QString("l_zoom"));
        QCOMPARE(scope.ref("bass", vars), QString("V[%1]").arg(vars.find("bass")));
    }

    // --------------------------------------------------------------- music

    void analyzerHearsTheBeat()
    {
        Analyzer analyzer;
        const AudioFrame frame = play(analyzer, 8.0, [](float *out, int frames, qint64 start) {
            synthesize(out, frames, start, 48000);
        });
        QVERIFY(!frame.silent);
        // The test loop is 120 beats a minute: about 16 in 8 seconds.
        QVERIFY2(frame.beats >= 12 && frame.beats <= 20, qPrintable(QString::number(frame.beats)));
        QVERIFY(frame.bassAtt > 0.2f);
        float peak = 0;
        for (float sample : frame.wave[0])
            peak = std::max(peak, std::abs(sample));
        QVERIFY2(peak > 0.2f && peak < 1.6f, qPrintable(QString::number(peak)));
    }

    void analyzerLevelsQuietAndLoudAlike()
    {
        for (const float volume : {0.02f, 0.9f}) {
            Analyzer analyzer;
            std::vector<float> chunk(800 * 2);
            float peak = 0;
            int beats = 0;
            for (int i = 0; i < 4 * 60; ++i) {
                synthesize(chunk.data(), 800, qint64(i) * 800, 48000);
                for (float &sample : chunk)
                    sample *= volume;
                analyzer.push(chunk.data(), 800);
                const AudioFrame &frame = analyzer.analyze(1.0 / 60);
                beats = frame.beats;
                // The loudest the waveform gets over the last second.
                if (i >= 3 * 60)
                    for (float sample : frame.wave[0])
                        peak = std::max(peak, std::abs(sample));
            }
            QVERIFY2(peak > 0.4f && peak < 1.6f, qPrintable(QString("volume %1 peak %2").arg(volume).arg(peak)));
            QVERIFY(beats > 4);
        }
    }

    void analyzerPlacesATone()
    {
        Analyzer analyzer;
        const AudioFrame frame = play(analyzer, 1.0, [](float *out, int frames, qint64 start) {
            for (int i = 0; i < frames; ++i)
                out[i * 2] = out[i * 2 + 1] = float(0.5 * std::sin(2 * M_PI * 1000.0 * double(start + i) / 48000.0));
        });
        int loudest = 0;
        for (int band = 0; band < AudioFrame::SpectrumSize; ++band)
            if (frame.spectrum[band] > frame.spectrum[loudest])
                loudest = band;
        // Bands are log-spaced from 30 Hz to 16 kHz.
        const double expected = AudioFrame::SpectrumSize * std::log(1000.0 / 30.0) / std::log(16000.0 / 30.0);
        QVERIFY2(std::abs(loudest - expected) <= 3, qPrintable(QString::number(loudest)));
        QVERIFY(frame.spectrum[loudest] > 0.8f);
        QVERIFY(frame.spectrum[10] < 0.2f);
    }

    // Each narrow range hears its own part of the sound and little of the
    // rest, so layers tied to different ones move apart.
    void analyzerTellsTheBandsApart()
    {
        // A tone that comes and goes in each range in turn, over a quiet
        // bed of all of them so every range has an average to stand out from.
        const double tones[AudioFrame::Bands] = {40, 110, 350, 1100, 3500, 9000};
        for (int loud = 0; loud < AudioFrame::Bands; ++loud) {
            Analyzer analyzer;
            const AudioFrame frame = play(analyzer, 3.0, [&](float *out, int frames, qint64 start) {
                for (int i = 0; i < frames; ++i) {
                    const double t = double(start + i) / 48000.0;
                    double sample = 0;
                    for (int band = 0; band < AudioFrame::Bands; ++band)
                        sample += (band == loud && t > 2.8 ? 0.4 : 0.04) * std::sin(2 * M_PI * tones[band] * t);
                    out[i * 2] = out[i * 2 + 1] = float(sample);
                }
            });
            for (int band = 0; band < AudioFrame::Bands; ++band) {
                if (band == loud)
                    QVERIFY2(frame.band[band] > 2.0f, qPrintable(QString("%1 is quiet: %2").arg(bandInfo(band).name).arg(frame.band[band])));
                else
                    QVERIFY2(frame.band[band] < 1.6f, qPrintable(QString("%1 heard %2: %3").arg(bandInfo(band).name, bandInfo(loud).name).arg(frame.band[band])));
            }
        }
        // Formulas know them by name, plain and smoothed.
        const CompiledPreset preset = presets::compile(fixture(
            "{ 'name': 'Bands', 'feedback': { 'zoom': '1 + 0.01*(sub + low_att + lowmid + highmid_att + presence + air_att)' } }"));
        QVERIFY2(preset.ok(), qPrintable(preset.firstError()));
    }

    // The wireframe's reading of the spectrum is a different measurement
    // from the one bars and terrain share, so the two don't move alike.
    void binsReadTheSpectrumTheirOwnWay()
    {
        // Two tones, the higher at half the strength of the lower.
        const auto tones = [](float *out, int frames, qint64 start) {
            for (int i = 0; i < frames; ++i) {
                const double t = double(start + i) / 48000.0;
                out[i * 2] = out[i * 2 + 1] =
                    float(0.4 * std::sin(2 * M_PI * 1050.0 * t) + 0.2 * std::sin(2 * M_PI * 3050.0 * t));
            }
        };
        Analyzer analyzer;
        Bins bins;
        Bins::Settings settings;
        settings.count = 50;
        settings.low = 0;
        settings.high = 5000;
        settings.tilt = 0;
        settings.smooth = 0;
        std::vector<float> chunk(800 * 2), heard;
        qint64 position = 0;
        const auto step = [&](int frames, bool sound) {
            for (int i = 0; i < frames; ++i) {
                if (sound)
                    tones(chunk.data(), 800, position);
                else
                    std::fill(chunk.begin(), chunk.end(), 0.f);
                position += 800;
                analyzer.push(chunk.data(), 800);
                heard = bins.update(analyzer.analyze(1.0 / 60), settings, 1.0 / 60);
            }
        };
        step(120, true);
        const AudioFrame frame = analyzer.frame();

        // The transform is handed over as it is: even steps from 0 Hz.
        int loudest = 0;
        for (int i = 0; i < AudioFrame::RawSize; ++i)
            if (frame.raw[i] > frame.raw[loudest])
                loudest = i;
        QVERIFY2(std::abs(loudest * frame.rawStep - 1050) < frame.rawStep, qPrintable(QString::number(loudest)));

        // Bins are equal widths of it, 100 Hz each here, and the loudest is 1.
        QCOMPARE(int(heard.size()), 50);
        for (int bin = 0; bin < 50; ++bin)
            QVERIFY2(bin == 10 || bin == 30 || heard[size_t(bin)] < 0.1f,
                     qPrintable(QString("bin %1: %2").arg(bin).arg(heard[size_t(bin)])));
        QVERIFY2(std::abs(heard[10] - 1.f) < 0.1f, qPrintable(QString::number(heard[10])));
        // Half the strength stands half as tall. The spectrum the other
        // layers read is in decibels, where it is nearly as tall.
        QVERIFY2(std::abs(heard[30] / heard[10] - 0.5f) < 0.08f, qPrintable(QString::number(heard[30])));
        const auto band = [](double hz) {
            return int(AudioFrame::SpectrumSize * std::log(hz / 30.0) / std::log(16000.0 / 30.0));
        };
        float lower = 0, higher = 0;
        for (int k = -2; k <= 2; ++k) {
            lower = std::max(lower, frame.spectrum[band(1050) + k]);
            higher = std::max(higher, frame.spectrum[band(3050) + k]);
        }
        QVERIFY2(higher / lower > 0.8f, qPrintable(QString("%1 %2").arg(lower).arg(higher)));
        // And the two are laid out differently: evenly here, by octave there.
        QVERIFY(band(1050) > AudioFrame::SpectrumSize / 2);

        // The top of the stretch can be weighted up.
        settings.tilt = 10;
        step(60, true);
        QVERIFY2(heard[30] > heard[10], qPrintable(QString("%1 %2").arg(heard[10]).arg(heard[30])));
        settings.tilt = 0;
        step(300, true);

        // A bin sinks back over its fall time instead of dropping.
        settings.fade = 0.5;
        step(1, true);
        const float before = heard[10];
        step(30, false);
        QVERIFY2(heard[10] > before * 0.25f && heard[10] < before * 0.6f, qPrintable(QString::number(heard[10])));
        step(240, false);
        QVERIFY(heard[10] < 0.02f);

        // Quiet and loud sources fill it alike, and silence leaves it flat.
        for (const float volume : {0.02f, 0.9f}) {
            Analyzer other;
            Bins reading;
            float peak = 0;
            for (int i = 0; i < 4 * 60; ++i) {
                synthesize(chunk.data(), 800, qint64(i) * 800, 48000);
                for (float &sample : chunk)
                    sample *= volume;
                other.push(chunk.data(), 800);
                const std::vector<float> &values = reading.update(other.analyze(1.0 / 60), Bins::Settings(), 1.0 / 60);
                if (i >= 3 * 60)
                    peak = std::max(peak, *std::max_element(values.begin(), values.end()));
            }
            QVERIFY2(peak > 0.6f && peak <= 2.f, qPrintable(QString("volume %1 peak %2").arg(volume).arg(peak)));
        }
        Bins quiet;
        const std::vector<float> &nothing = quiet.update(AudioFrame(), Bins::Settings(), 1.0 / 60);
        QCOMPARE(*std::max_element(nothing.begin(), nothing.end()), 0.f);
    }

    void analyzerKnowsSilence()
    {
        Analyzer analyzer;
        const AudioFrame frame = play(analyzer, 2.0, [](float *out, int frames, qint64) {
            std::fill(out, out + frames * 2, 0.f);
        });
        QVERIFY(frame.silent);
        QCOMPARE(frame.bass, 0.f);
        QCOMPARE(frame.beats, 0);
    }

    // ------------------------------------------------------------- presets

    void everyBuiltinPresetIsClean()
    {
        const Library library(QDir::tempPath() + "/trance-tests-no-such-folder");
        QVERIFY(library.presets().size() >= 25);
        for (const auto &info : library.presets()) {
            QString error;
            const QJsonObject json = library.load(library.indexOf(info.name), &error);
            QVERIFY2(!json.isEmpty(), qPrintable(info.path + ": " + error));
            const CompiledPreset compiled = presets::compile(json);
            for (const auto &problem : compiled.problems)
                QFAIL(qPrintable(info.name + ": " + problem.path + ": " + problem.message));
            QVERIFY2(!json.value("description").toString().isEmpty(), qPrintable(info.name));
            QCOMPARE(Library::slug(info.name) + ".json", QFileInfo(info.path).fileName());
        }
    }

    // The README's counts follow the presets and palettes as they are.
    void readmeCountsAreCurrent()
    {
        QFile file(TRANCE_ROOT "/README.md");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString readme = QString::fromUtf8(file.readAll()).simplified();
        const Library library(QDir::tempPath() + "/trance-tests-no-such-folder");
        const qsizetype presetCount = library.presets().size();
        QVERIFY2(readme.contains(QString("**%1 presets**").arg(presetCount)),
                 qPrintable(QString("The README should say %1 presets").arg(presetCount)));
        // theme and accent are the desktop theme's, which the README names apart.
        const qsizetype paletteCount = presets::paletteNames().size() - 2;
        QVERIFY2(readme.contains(QString("one of %1 palettes").arg(paletteCount)),
                 qPrintable(QString("The README should say %1 palettes").arg(paletteCount)));
        int wireframes = 0;
        for (const auto &info : library.presets())
            for (const Block &layer : presets::compile(library.load(library.indexOf(info.name))).layers)
                if (layer.type == "wireframe") {
                    ++wireframes;
                    break;
                }
        static const QStringList words{"", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine",
                                       "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen",
                                       "Seventeen", "Eighteen", "Nineteen", "Twenty"};
        const QString said = wireframes < words.size() ? words[wireframes] : QString::number(wireframes);
        QVERIFY2(readme.contains(said + " presets are built on it"),
                 qPrintable(QString("The README should say %1 presets are built on the wireframe").arg(said)));
    }

    // Every setting has a name and an explanation for the editor, and the
    // manual covers every one of them by the name it has in the file.
    void everySettingIsExplained()
    {
        QFile file(":/manual.md");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString manual = QString::fromUtf8(file.readAll());
        QStringList sections{"feedback", "post", "background"};
        sections << presets::layerTypes();
        for (const auto &section : sections) {
            for (const Field &field : presets::fields(section)) {
                const QString key = QString::fromLatin1(field.key);
                const FieldDoc doc = presets::fieldDoc(section, key);
                QVERIFY2(doc.label != key && !doc.help.isEmpty(), qPrintable(section + "." + key + " has no name"));
                QVERIFY2(manual.contains("`" + key + "`"), qPrintable("The manual leaves out " + section + "." + key));
                QVERIFY2(manual.contains(doc.label), qPrintable("The manual doesn't use the name " + doc.label));
            }
        }
        for (const auto &type : presets::layerTypes())
            QVERIFY(!presets::fieldDoc("type", type).help.isEmpty());
        for (const auto &name : presets::variableNames())
            QVERIFY2(manual.contains("`" + name + "`") || name.endsWith("_att"), qPrintable("The manual leaves out " + name));
    }

    void presetProblemsPointAtTheField()
    {
        const QJsonObject json = fixture(
            "{ 'name': 'Broken', 'palette': 'nope', 'glitter': 3, 'feedback': { 'zoom': '1 + basss', "
            "'edge': 'soft', 'rot': 'rad' }, 'layers': [ { 'type': 'wave', 'color': 'chartreuse', "
            "'thicknes': 2 }, { 'type': 'laser' } ], 'post': { 'bloom': 'min(1)' } } ");
        const CompiledPreset compiled = presets::compile(json);
        QVERIFY(!compiled.ok());
        QHash<QString, Problem> byPath;
        for (const auto &problem : compiled.problems)
            byPath.insert(problem.path, problem);
        QCOMPARE(byPath.value("feedback.zoom").message, QString("Unknown variable 'basss'"));
        // rad only exists per pixel; a per-frame setting can't read it.
        QCOMPARE(byPath.value("feedback.rot").message, QString("Unknown variable 'rad'"));
        QVERIFY(byPath.value("feedback.edge").message.startsWith("'soft' is not one of"));
        QVERIFY(byPath.value("layers.0.color").message.startsWith("Unknown color 'chartreuse'"));
        QVERIFY(byPath.value("layers.1.type").message.startsWith("'laser' is not a layer type"));
        QVERIFY(byPath.value("palette").message.startsWith("Unknown palette 'nope'"));
        QCOMPARE(byPath.value("post.bloom").message, QString("min() takes 2 argument(s), not 1"));
        // Misspelled settings are warnings: the preset would still run.
        QCOMPARE(byPath.value("layers.0.thicknes").error, false);
        QCOMPARE(byPath.value("glitter").error, false);
    }

    // Outside a shape's copies, t, i and v reach only the color and the
    // point code; anywhere else in a layer they are 0, so check says so.
    void pointNamesInLayerSettingsAreWarned()
    {
        const CompiledPreset compiled = presets::compile(fixture(
            "{ 'name': 'Points', 'layers': [ { 'type': 'terrain', 'alpha': '1 - 0.7*i', "
            "'color': 'palette(t + i)' }, { 'type': 'particles', 'size': 'v*2' }, "
            "{ 'type': 'shape', 'instances': 4, 'radius': '0.1*t' } ] }"));
        QVERIFY(compiled.ok());
        QHash<QString, Problem> byPath;
        for (const auto &problem : compiled.problems)
            byPath.insert(problem.path, problem);
        QVERIFY(byPath.value("layers.0.alpha").message.startsWith("'i' is always 0 here"));
        QCOMPARE(byPath.value("layers.0.alpha").error, false);
        QVERIFY(byPath.value("layers.1.size").message.startsWith("'v' is always 0 here"));
        QVERIFY(!byPath.contains("layers.0.color"));
        QVERIFY(!byPath.contains("layers.2.radius"));
    }

    void presetCodeReachesTheShaders()
    {
        const QJsonObject json = fixture(
            "{ 'name': 'Shaded', 'frame': 'swing = sin(time)', 'pixel': 'zoom = zoom + swing * rad', "
            "'shade': 'r = r * (1 - rad); glow = lum * swing', 'feedback': { 'edge': 'black' }, "
            "'post': { 'mirror': 'x', 'echoFlip': 'y' } } ");
        const CompiledPreset compiled = presets::compile(json);
        QVERIFY2(compiled.ok(), qPrintable(compiled.firstError()));
        const QString swing = QString("V[%1]").arg(compiled.vars.find("swing"));
        QVERIFY(compiled.warpShader.contains("float l_zoom = (V["));
        QVERIFY(compiled.warpShader.contains(swing + "*l_rad"));
        QVERIFY(compiled.warpShader.contains("float zoom = l_zoom"));
        QVERIFY(compiled.warpShader.contains("step(0.0, uv.x)"));
        QVERIFY(compiled.compositeShader.contains("l_r = (l_r*(1.0-l_rad));"));
        QVERIFY(compiled.compositeShader.contains("p.x = -abs(p.x);"));
        QVERIFY(compiled.compositeShader.contains("e.y = -e.y;"));
        QVERIFY(!compiled.warpShader.contains('%') && !compiled.compositeShader.contains('%'));
    }

    void paletteTakesNamesListsAndThemeColors()
    {
        auto palette = [](const QJsonValue &value) {
            return presets::compile(QJsonObject{{"name", "P"}, {"palette", value}}).palette;
        };
        QCOMPARE(palette("fire"), presets::paletteStops("fire"));
        QCOMPARE(palette("#ff0000, accent"), (QStringList{"#ff0000", "accent"}));
        QCOMPARE(palette(QJsonArray{"#000000", "#ffffff", "red"}), (QStringList{"#000000", "#ffffff", "red"}));
        QCOMPARE(palette(QJsonValue::Undefined), presets::paletteStops("theme"));
        // Every name the docs and the T key offer is there, each named once,
        // and compiles without a problem.
        const QStringList names = presets::paletteNames();
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());
        for (const QString &name : names) {
            QVERIFY2(presets::paletteStops(name).size() >= 2, qPrintable(name));
            const CompiledPreset compiled = presets::compile(QJsonObject{{"name", "P"}, {"palette", name}});
            QVERIFY2(compiled.problems.isEmpty(), qPrintable(name + ": " + compiled.firstError()));
        }
        QFile format(":/format.md");
        QVERIFY(format.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(format.readAll());
        for (const QString &name : names)
            QVERIFY2(text.contains("`" + name + "`"), qPrintable("The format leaves out the palette " + name));
        ThemeColors theme{{"accent", QColor("#123456")}};
        QCOMPARE(presets::resolveColor("accent", theme).name(), QString("#123456"));
        QCOMPARE(presets::resolveColor("red", theme), presets::defaultThemeColors().value("red"));
        QCOMPARE(presets::resolveColor("#abcdef", theme).name(), QString("#abcdef"));
    }

    // ------------------------------------------------------------- library

    void librarySavesBesideTheBuiltins()
    {
        QTemporaryDir dir;
        Library library(dir.path() + "/presets");
        const int builtins = library.presets().size();
        QString error;
        const QString path = library.save(presets::blank("My First"), &error);
        QCOMPARE(path, dir.path() + "/presets/my-first.json");
        QCOMPARE(library.presets().size(), builtins + 1);
        QVERIFY(!library.presets()[library.indexOf("my first")].builtin);

        // A changed built-in is saved as the user's copy and stands in for it.
        QJsonObject tunnel = library.find("Neon Tunnel", &error);
        tunnel["author"] = "Me";
        QVERIFY(!library.save(tunnel, &error).isEmpty());
        QCOMPARE(library.presets().size(), builtins + 1);
        const PresetInfo mine = library.presets()[library.indexOf("Neon Tunnel")];
        QVERIFY(!mine.builtin && mine.shadowsBuiltin);
        QCOMPARE(mine.author, QString("Me"));

        // Deleting it brings the built-in back; built-ins themselves stay.
        QVERIFY(library.remove("Neon Tunnel", &error));
        QVERIFY(library.presets()[library.indexOf("Neon Tunnel")].builtin);
        QVERIFY(!library.remove("Neon Tunnel", &error));
        QCOMPARE(error, QString("Built-in presets can't be deleted"));
    }

    // Names that come down to the same file name each get a file of their
    // own; saving the second never writes over the first.
    void librarySavesLookalikeNamesApart()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        QString error;
        const QString first = library.save(fixture("{ 'name': 'Night!', 'layers': [] }"), &error);
        const QString second = library.save(fixture("{ 'name': 'Night?', 'layers': [] }"), &error);
        QVERIFY2(!first.isEmpty() && !second.isEmpty(), qPrintable(error));
        QVERIFY(first != second);
        QVERIFY(library.indexOf("Night!") >= 0 && library.indexOf("Night?") >= 0);
        // Saving one of them again goes back to its own file.
        QCOMPARE(library.save(fixture("{ 'name': 'Night?', 'layers': [], 'palette': 'fire' }"), &error), second);
        QCOMPARE(library.find("Night!", &error).value("palette").toString(), QString());
    }

    void libraryExplainsABrokenFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/bad.json", "{\n  \"name\": \"Bad\",\n  oops\n}");
        QString error;
        QVERIFY(Library::readFile(dir.path() + "/bad.json", &error).isEmpty());
        QVERIFY2(error.endsWith("(line 3)"), qPrintable(error));
        QCOMPARE(Library::slug("  Ça va? Très BIEN!  "), QString("ça-va-très-bien"));
    }

    // ------------------------------------------------------------- filters

    // The family comes from the colors themselves, so a palette added later,
    // or a preset's own list of colors, is sorted with no table to update.
    void colorFamiliesComeFromTheColors()
    {
        const QHash<QString, QString> expected = {
            {"crimson", "red"},     {"rose", "red"},      {"fire", "orange"}, {"gold", "orange"},
            {"phosphor", "green"},  {"jade", "green"},    {"ocean", "blue"},  {"cobalt", "blue"},
            {"amethyst", "violet"}, {"rainbow", "many"},  {"neon", "many"},   {"mono", "gray"},
            {"steel", "gray"},      {"theme", "theme"},   {"accent", "theme"}};
        for (auto it = expected.begin(); it != expected.end(); ++it)
            QCOMPARE(presets::colorFamily(presets::paletteStops(it.key())), it.value());
        for (const QString &name : presets::paletteNames())
            QVERIFY2(presets::colorFamilies().contains(presets::colorFamily(presets::paletteStops(name))),
                     qPrintable(name));

        QCOMPARE(presets::colorFamily({"#000000", "#003300", "#22aa44", "#ccffdd"}), QString("green"));
        QCOMPARE(presets::colorFamily({"#000000", "#ffffff"}), QString("gray"));
        QCOMPARE(presets::colorFamily({"#000000", "accent"}), QString("theme"));
        QCOMPARE(presets::colorFamily({}), QString());
        QCOMPARE(presets::colorFamily(presets::presetPalette(fixture("{ 'palette': '#ff0000, #800000' }"))),
                 QString("red"));
        QCOMPARE(presets::colorFamily(presets::presetPalette(fixture("{ 'palette': ['#2040ff', '#001060'] }"))),
                 QString("blue"));
        QCOMPARE(presets::colorFamily(presets::presetPalette(fixture("{ 'name': 'No palette' }"))), QString("theme"));
    }

    void featuresAreReadFromThePreset()
    {
        QCOMPARE(presets::features(fixture(
                     "{ 'layers': [ { 'type': 'wireframe' }, { 'type': 'wave' }, { 'type': 'wave' } ] }")),
                 QStringList({"wave", "wireframe"}));
        // A range counts only where a formula or code reads it, not in the
        // words around it.
        QCOMPARE(presets::features(fixture("{ 'description': 'Light as air', 'frame': ['// sub and low', 'x = 1'],"
                                           " 'layers': [ { 'type': 'bars', 'lowest': 'lowmidst' } ] }")),
                 QStringList({"bars"}));
        QCOMPARE(presets::features(fixture("{ 'feedback': { 'zoom': '1 + 0.1*presence_att' } }")),
                 QStringList({"ranges"}));
        QCOMPARE(presets::features(fixture("{ 'layers': [ { 'type': 'shape', 'radius': '0.2 + sub' } ] }")),
                 QStringList({"shape", "ranges"}));

        // A preset of the user's is sorted as soon as it is saved.
        QTemporaryDir dir;
        Library library(dir.path());
        QString error;
        QVERIFY(!library.save(fixture("{ 'name': 'Mine', 'palette': ['#003300', '#33ff66'],"
                                      " 'layers': [ { 'type': 'wireframe', 'yaw': 'air_att' } ] }"), &error)
                     .isEmpty());
        const PresetInfo mine = library.presets()[library.indexOf("Mine")];
        QCOMPARE(mine.family, QString("green"));
        QCOMPARE(mine.features, QStringList({"wireframe", "ranges"}));
        const PresetInfo terraces = library.presets()[library.indexOf("Terraces")];
        QVERIFY(terraces.features.contains("wireframe") && terraces.features.contains("ranges"));
    }

    void backendKeepsToTheFilter()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        const auto &all = backend.library().presets();
        const auto wireframes = [&] {
            QStringList names;
            for (const PresetInfo &preset : all)
                if (preset.features.contains("wireframe") && preset.features.contains("ranges"))
                    names << preset.name;
            return names;
        }();
        QVERIFY(wireframes.size() >= 3);

        backend.setFilterFeatures({"ranges", "wireframe", "no such feature"});
        QCOMPARE(backend.filterFeatures(), QStringList({"wireframe", "ranges"}));
        QCOMPARE(backend.matchCount(), int(wireframes.size()));
        int shown = 0;
        for (const QVariant &preset : backend.presets())
            shown += preset.toMap().value("matches").toBool();
        QCOMPARE(shown, int(wireframes.size()));

        // Not kept to, the filters only narrow the list.
        QVERIFY(backend.select("Aurora"));
        backend.next();
        QCOMPARE(backend.currentName(), all[backend.library().indexOf("Aurora") + 1].name);

        // Kept to, every way of moving on stays among those that match,
        // and comes back to them from one that doesn't.
        backend.setKeepToFilter(true);
        QVERIFY(backend.select("Aurora"));
        backend.next();
        QVERIFY2(wireframes.contains(backend.currentName()), qPrintable(backend.currentName()));
        QStringList visited;
        for (int i = 0; i < int(wireframes.size()); ++i) {
            backend.next();
            visited << backend.currentName();
        }
        visited.sort();
        QCOMPARE(visited, wireframes);
        for (int i = 0; i < 20; ++i) {
            const QString before = backend.currentName();
            i % 2 ? backend.previous() : backend.random();
            QVERIFY2(wireframes.contains(backend.currentName()), qPrintable(backend.currentName()));
            QVERIFY(backend.currentName() != before);
        }

        // The filters and keeping to them are remembered.
        backend.setFilterColor("blue");
        backend.setFilterColor("no such color");
        QCOMPARE(backend.filterColor(), QString());
        backend.setFilterColor("violet");
        {
            Backend again(dir.path(), false);
            QCOMPARE(again.filterColor(), QString("violet"));
            QCOMPARE(again.filterFeatures(), QStringList({"wireframe", "ranges"}));
            QVERIFY(again.keepToFilter());
        }

        // When nothing matches, the changes fall back to every preset.
        backend.setFilterColor("gray");
        backend.setFilterFeatures({"terrain", "wireframe", "vectors"});
        QCOMPARE(backend.matchCount(), 0);
        QVERIFY(backend.select("Aurora"));
        backend.next();
        QVERIFY(backend.currentName() != "Aurora");
        backend.clearFilter();
        QVERIFY(!backend.filtering());
        QCOMPARE(backend.matchCount(), int(all.size()));
    }

    // The editor shows the preset's own palette, so one picked there is
    // the one on screen; the Colors setting comes back when it closes.
    void editorShowsThePresetsOwnPalette()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        QVERIFY(backend.select("Terraces"));
        backend.setPaletteOverride("fire");
        QCOMPARE(backend.effectivePalette(), QString("fire"));
        QSignalSpy changed(&backend, &Backend::paletteChanged);
        backend.setEditing(true);
        QCOMPARE(backend.effectivePalette(), QString());
        QVERIFY(!changed.isEmpty());
        backend.setParam("palette", "cobalt");
        QCOMPARE(backend.paletteColors(), presets::paletteStops("cobalt"));
        backend.setEditing(false);
        QCOMPARE(backend.effectivePalette(), QString("fire"));
    }

    // ------------------------------------------------------------- backend

    void backendEditsUndoesAndSaves()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        QVERIFY(backend.select("Skyline"));
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.param("feedback.decay").toDouble(), 0.9);
        QCOMPARE(backend.param("feedback.zoom").toDouble(), 1.0); // the default, not set in the file
        QVERIFY(!backend.isSet("feedback.zoom"));

        const int compiledBefore = backend.compiledRevision();
        backend.setParam("feedback.zoom", "1.02 + 0.1*bass");
        QVERIFY(backend.dirty());
        QCOMPARE(backend.param("feedback.zoom").toString(), QString("1.02 + 0.1*bass"));
        QVERIFY(backend.compiledRevision() > compiledBefore);
        QVERIFY(backend.problems().isEmpty());

        // A formula with a mistake is kept and reported, while the picture
        // goes on showing the last version that worked.
        const int compiledGood = backend.compiledRevision();
        backend.setParam("feedback.zoom", "1.02 + ");
        QCOMPARE(backend.problemAt("feedback.zoom"), QString("Unfinished expression"));
        QCOMPARE(backend.compiledRevision(), compiledGood);

        backend.setParam("feedback.zoom", "1.5"); // typed numbers are stored as numbers
        QCOMPARE(backend.json().value("feedback").toObject().value("zoom"), QJsonValue(1.5));
        backend.setParam("layers.0.count", 31.6);
        QCOMPARE(backend.param("layers.0.count").toDouble(), 32.0);
        backend.setParam("layers.0.peaks", false);
        QCOMPARE(backend.param("layers.0.peaks").toBool(), false);

        backend.undo();
        QCOMPARE(backend.param("layers.0.peaks").toBool(), true);
        backend.redo();
        QCOMPARE(backend.param("layers.0.peaks").toBool(), false);

        backend.resetParam("feedback.zoom");
        QVERIFY(!backend.isSet("feedback.zoom"));

        QSignalSpy notices(&backend, &Backend::notice);
        QVERIFY(backend.save());
        QVERIFY(!backend.dirty());
        QVERIFY(backend.currentIsYours());
        QVERIFY(QFile::exists(dir.path() + "/presets/skyline.json"));
        QCOMPARE(notices.last().first().toString(), QString("Saved Skyline"));

        // The saved copy is what a fresh start finds.
        Backend again(dir.path(), false);
        QCOMPARE(again.currentName(), QString("Skyline"));
        QCOMPARE(again.param("layers.0.count").toDouble(), 32.0);
    }

    // The editor marks what has been changed since the save, down to the
    // one setting, and stops marking it once it is saved or put back.
    void backendKnowsWhatWasEdited()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        QVERIFY(backend.select("Neon Tunnel"));
        QVERIFY(!backend.isEdited("feedback.decay"));
        QVERIFY(!backend.isEdited("feedback"));
        QVERIFY(!backend.isEdited("layers"));

        backend.setParam("feedback.decay", 0.5);
        QVERIFY(backend.isEdited("feedback.decay"));
        QVERIFY(backend.isEdited("feedback"));
        QVERIFY(!backend.isEdited("feedback.zoom"));
        QVERIFY(!backend.isEdited("post"));

        backend.setParam("layers.0.thickness", 9);
        QVERIFY(backend.isEdited("layers.0.thickness"));
        QVERIFY(backend.isEdited("layers.0"));
        QVERIFY(backend.isEdited("layers"));
        QVERIFY(!backend.isEdited("layers.0.color"));

        backend.setParam("pixel", "rot = 0.01");
        QVERIFY(backend.isEdited("pixel"));

        backend.revert();
        QVERIFY(!backend.isEdited("feedback.decay"));
        QVERIFY(!backend.isEdited("layers.0"));
        QVERIFY(!backend.isEdited("pixel"));

        backend.setParam("post.bloom", 2);
        QVERIFY(backend.isEdited("post.bloom"));
        QVERIFY(backend.save());
        QVERIFY(!backend.isEdited("post.bloom"));

        // A preset that was never saved has nothing to differ from.
        QVERIFY(backend.newPreset("Blank Slate"));
        backend.setParam("feedback.decay", 0.7);
        QVERIFY(!backend.isEdited("feedback.decay"));
    }

    void backendManagesLayersAndText()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        QVERIFY(backend.newPreset("Scratch"));
        QVERIFY(backend.dirty());
        QCOMPARE(backend.layers().size(), 1);
        backend.addLayer("particles");
        backend.addLayer("bars");
        QCOMPARE(backend.layers().size(), 3);
        backend.moveLayer(2, -2);
        QCOMPARE(backend.layers().first().toMap().value("type").toString(), QString("bars"));
        backend.duplicateLayer(0);
        backend.removeLayer(3);
        QCOMPARE(backend.layers().size(), 3);
        QCOMPARE(backend.layers().at(1).toMap().value("type").toString(), QString("bars"));
        QVERIFY(backend.problems().isEmpty());

        QVERIFY(!backend.setPresetText("{ not json"));
        QVERIFY(!backend.problemAt("json").isEmpty());
        QCOMPARE(backend.layers().size(), 3);
        QVERIFY(backend.setPresetText(
            QString("{'name': 'Scratch', 'palette': '#000000, accent', 'layers': []}").replace('\'', '"')));
        QCOMPARE(backend.layers().size(), 0);
        QCOMPARE(backend.param("palette").toString(), QString("#000000, accent"));
        QCOMPARE(backend.paletteColors().size(), 2);

        QVERIFY(backend.saveAs("Scratch Two"));
        QCOMPARE(backend.currentName(), QString("Scratch Two"));
        QVERIFY(backend.deleteCurrent());
        QVERIFY(backend.library().indexOf("Scratch Two") < 0);
        QVERIFY(!backend.deleteCurrent()); // back on a built-in
    }

    void backendMovesThroughPresets()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        const QString first = backend.currentName();
        backend.next();
        const QString second = backend.currentName();
        QVERIFY(first != second);
        backend.previous();
        QCOMPARE(backend.currentName(), first);
        backend.random();
        QVERIFY(backend.currentName() != first);
        QVERIFY(!backend.select("No Such Preset"));
        QCOMPARE(backend.cyclePalette(), QString("theme"));
        backend.setPaletteOverride("fire");
        QCOMPARE(backend.paletteColors().first(), QString("#000000"));
    }

    // -------------------------------------------------------------- engine

    void engineDrawsEveryPreset()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        const Library library(QDir::tempPath() + "/trance-tests-no-such-folder");
        Analyzer analyzer;
        std::vector<float> chunk(800 * 2);
        qint64 position = 0;
        QOpenGLFramebufferObject target(320, 200);
        Engine engine;
        engine.setTheme(presets::defaultThemeColors());
        for (const auto &info : library.presets()) {
            engine.setPreset(presets::compile(library.load(library.indexOf(info.name))), 0.5);
            for (int frame = 0; frame < 90; ++frame) {
                synthesize(chunk.data(), 800, position, 48000);
                position += 800;
                analyzer.push(chunk.data(), 800);
                engine.render(analyzer.analyze(1.0 / 60), 1.0 / 60, target.handle(), 320, 200);
            }
            QVERIFY2(engine.error().isEmpty(), qPrintable(info.name + ": " + engine.error()));
            // Every preset puts something on the screen within a second and a half.
            const QImage image = target.toImage();
            int lit = 0;
            for (int y = 0; y < image.height(); y += 2)
                for (int x = 0; x < image.width(); x += 2)
                    lit += qGray(image.pixel(x, y)) > 24;
            QVERIFY2(lit > 40, qPrintable(info.name + " is black"));
        }
    }

    void engineRunsThePresetsOwnCode()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        QOpenGLFramebufferObject target(64, 64);
        Engine engine;
        engine.setPreset(presets::compile(fixture(
            "{ 'name': 'Counter', 'init': 'count_frames = 100', 'frame': 'count_frames = count_frames "
            "+ 1', 'layers': [ { 'type': 'shape', 'sides': 4, 'radius': 3, 'color': '#ff0000', "
            "'color2': '#ff0000', 'alpha2': 1 } ], 'post': { 'bloom': 0, 'vignette': 0 }, 'shade': 'g "
            "= r; r = 0' } ")), 0);
        for (int frame = 0; frame < 5; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY2(engine.error().isEmpty(), qPrintable(engine.error()));
        QCOMPARE(engine.variable("count_frames"), 105.0);
        // A red square covers the screen, and the shade code turns it green.
        const QColor middle = target.toImage().pixelColor(32, 32);
        QVERIFY2(middle.green() > 200 && middle.red() < 30, qPrintable(middle.name()));

        // A preset that can't be compiled leaves the last one running.
        CompiledPreset broken = presets::compile(QJsonObject{{"name", "Broken"}});
        broken.warpShader = "this is not GLSL";
        engine.setPreset(broken, 0);
        engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY(!engine.error().isEmpty());
        QCOMPARE(engine.variable("count_frames"), 106.0);
    }

    // noise() is the same field on the CPU as in a shader, so particles and
    // motion vectors, which work the flow out on the CPU, follow the picture
    // when pixel code uses it.
    void noiseIsTheSameOnTheCpuAndTheGpu()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        QOpenGLFramebufferObject target(64, 64);
        Engine engine;
        engine.setPreset(presets::compile(fixture(
            "{ 'name': 'Noise', 'feedback': { 'decay': 0 }, 'shade': 'r = 0.6*noise(x*7 + 3.3, y*7 - 11.7); g = 0; b = 0', "
            "'post': { 'bloom': 0, 'vignette': 0 } }")), 0);
        engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY2(engine.error().isEmpty(), qPrintable(engine.error()));
        const QImage image = target.toImage();
        double worst = 0;
        for (int row = 2; row < 64; row += 7) {
            for (int col = 3; col < 64; col += 7) {
                // The image's first row is the top of the picture.
                const double x = (col + 0.5) / 64 * 2 - 1, y = 1 - (row + 0.5) / 64 * 2;
                const double expected = 0.6 * expr::noise(x * 7 + 3.3, y * 7 - 11.7);
                worst = std::max(worst, std::abs(image.pixelColor(col, row).redF() - expected));
            }
        }
        QVERIFY2(worst < 3.0 / 255, qPrintable(QString::number(worst * 255) + " levels apart"));
    }

    // Blur softens an edge as much in three seconds at 144 frames a second
    // as at 60, and by as much of the screen in a small picture as in a large
    // one.
    void blurSpreadsTheSameAtAnyRateAndSize()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        // A wide disc drawn on the first frame only, then left to the blur.
        const CompiledPreset disc = presets::compile(fixture(
            "{ 'name': 'Disc', 'feedback': { 'decay': 1, 'blur': 1 }, 'layers': [ { 'type': 'shape', 'sides': 64, "
            "'radius': 0.3, 'color': '#ffffff', 'color2': '#ffffff', 'alpha': 'below(frame, 1.5)', "
            "'alpha2': 'below(frame, 1.5)' } ], 'post': { 'bloom': 0, 'vignette': 0 } }"));
        // How wide the disc's rim has grown: the distance along the middle
        // row from where it is three quarters as bright as inside to where it
        // is a quarter. Where the rim sits, which a small picture draws a
        // fraction of a pixel off, and what the fade has taken from the whole
        // picture don't change it.
        auto rim = [&](int size, int rate) {
            QOpenGLFramebufferObject target(size, size);
            Engine engine;
            engine.setPreset(disc, 0);
            for (int frame = 0; frame < 3 * rate; ++frame)
                engine.render(AudioFrame(), 1.0 / rate, target.handle(), size, size);
            const QImage image = target.toImage();
            // Read between pixels, so that pictures of any size are read at
            // the same places.
            auto at = [&](double x) {
                const double col = (x * 0.5 + 0.5) * size - 0.5, row = size * 0.5 - 0.5;
                const int c = int(col), r = int(row);
                const double fc = col - c, fr = row - r;
                auto pixel = [&](int cc, int rr) { return image.pixelColor(cc, rr).redF(); };
                return (pixel(c, r) * (1 - fc) + pixel(c + 1, r) * fc) * (1 - fr) +
                       (pixel(c, r + 1) * (1 - fc) + pixel(c + 1, r + 1) * fc) * fr;
            };
            const double inside = at(0.1);
            double high = 0, low = 0;
            for (double x = 0.15; x < 0.5 && !low; x += 0.0005) {
                if (!high && at(x) < 0.75 * inside)
                    high = x;
                if (at(x) < 0.25 * inside)
                    low = x;
            }
            return low - high;
        };

        // Before blur kept to the second and to the screen, the rim grew
        // half as wide again at 144 frames a second and nearly twice as wide
        // at 270 lines. Half-float buffers that round down as they are written
        // take a little from every frame, which leaves the rim up to a fifth
        // narrower at 144, so the rate is held to a quarter.
        const double base = rim(540, 60);
        QVERIFY2(base > 0.02, qPrintable("the rim is only " + QString::number(base) + " wide"));
        const double fast = rim(540, 144), small = rim(270, 60);
        QVERIFY2(std::abs(fast / base - 1) < 0.25,
                 qPrintable(QString::number(fast / base) + " times as wide at 144 frames a second"));
        QVERIFY2(std::abs(small / base - 1) < 0.1, qPrintable(QString::number(small / base) + " times as wide at 270 lines"));
    }

    // --------------------------------------------------------------- theme

    // A change of preset runs both for its length: halfway through, the
    // picture holds some of each, and at the end only the new one is left.
    void engineChangesPresetsWithoutACut()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        const auto flood = [](const char *name, const char *color) {
            return presets::compile(fixture(QByteArray("{ 'name': '") + name +
                "', 'feedback': { 'decay': 0 }, 'layers': [ { 'type': 'shape', 'sides': 4, 'radius': 3, 'color': '" +
                color + "', 'color2': '" + color + "', 'alpha2': 1 }, { 'type': 'wave', 'shape': 'circle' }, "
                "{ 'type': 'particles', 'emit': 'wave', 'flow': 1, 'field': 1, 'streak': 0.05 }, "
                "{ 'type': 'vectors' } ], 'background': { 'style': 'nebula' }, 'post': { 'bloom': 0, "
                "'vignette': 0 } }"));
        };
        const CompiledPreset red = flood("Red", "#ff0000"), blue = flood("Blue", "#0000ff");
        QVERIFY2(red.ok(), qPrintable(red.firstError()));

        QOpenGLFramebufferObject target(64, 64);
        Engine engine;
        engine.setTransitionStyle(1); // an even dissolve, so every pixel is partway
        engine.setPreset(red, 0);
        for (int frame = 0; frame < 5; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY2(engine.error().isEmpty(), qPrintable(engine.error()));
        QVERIFY(!engine.changing());
        QColor corner = target.toImage().pixelColor(4, 4);
        QVERIFY2(corner.red() > 200 && corner.blue() < 30, qPrintable(corner.name()));

        engine.setPreset(blue, 1.0);
        for (int frame = 0; frame < 30; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY(engine.changing());
        corner = target.toImage().pixelColor(4, 4);
        QVERIFY2(corner.red() > 30 && corner.blue() > 30, qPrintable(corner.name()));
        for (int frame = 0; frame < 40; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QVERIFY(!engine.changing());
        corner = target.toImage().pixelColor(4, 4);
        QVERIFY2(corner.blue() > 200 && corner.red() < 30, qPrintable(corner.name()));

        // Visitors drop in over it, and a title is written into it.
        QList<CompiledPreset> pool;
        for (const QJsonObject &visitor : Library::visitors()) {
            const CompiledPreset compiled = presets::compile(visitor);
            QVERIFY2(compiled.ok(), qPrintable(compiled.name + ": " + compiled.firstError()));
            pool.append(compiled);
        }
        QVERIFY(pool.size() >= 5);
        engine.setVisitors(pool, 1000);
        QImage title(200, 40, QImage::Format_ARGB32_Premultiplied);
        title.fill(Qt::white);
        engine.showText(title);
        int most = 0;
        for (int frame = 0; frame < 400; ++frame) {
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
            most = qMax(most, engine.visitorCount());
        }
        QVERIFY2(engine.error().isEmpty(), qPrintable(engine.error()));
        QVERIFY(most >= 1);
        engine.setVisitors(pool, 0);
        engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
        QCOMPARE(engine.visitorCount(), 0);
    }

    // The wireframe layer: every form and style draws, a camera inside the
    // form is no trouble, and its code moves points in three dimensions.
    void engineDrawsWireframes()
    {
        const CompiledPreset starter = presets::compile(QJsonObject{
            {"name", "Starter"}, {"layers", QJsonArray{presets::starterLayer("wireframe")}}});
        QVERIFY2(starter.problems.isEmpty(), qPrintable(starter.firstError()));
        const CompiledPreset bad = presets::compile(fixture(
            "{ 'name': 'Bad', 'layers': [ { 'type': 'wireframe', 'form': 'cube', 'point': 'z = z + depthh' } ] }"));
        QStringList paths;
        for (const auto &problem : bad.problems)
            paths << problem.path;
        QVERIFY2(paths.contains("layers.0.form") && paths.contains("layers.0.point"), qPrintable(paths.join(' ')));

        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context;
        context.setFormat(format);
        QOffscreenSurface surface;
        if (!context.create())
            QSKIP("No OpenGL context here");
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            QSKIP("No OpenGL context here");

        QOpenGLFramebufferObject target(320, 200);
        const auto lit = [&] {
            const QImage image = target.toImage();
            int count = 0;
            for (int y = 0; y < image.height(); y += 2)
                for (int x = 0; x < image.width(); x += 2)
                    count += qGray(image.pixel(x, y)) > 24;
            return count;
        };
        const QStringList layers = {
            "{ 'type': 'wireframe', 'form': 'sheet', 'style': 'mesh', 'yaw': 'time' }",
            "{ 'type': 'wireframe', 'form': 'tube', 'style': 'lines', 'bins': 128, 'rows': 64 }",
            "{ 'type': 'wireframe', 'form': 'ring', 'style': 'strands', 'bins': 8, 'rows': 4 }",
            "{ 'type': 'wireframe', 'form': 'sphere', 'style': 'dots', 'thickness': 4 }",
            "{ 'type': 'wireframe', 'form': 'sheet', 'form2': 'sphere', 'morph': '0.5 + 0.5*sin(time*9)' }",
            // From inside: most of the tube is behind the camera.
            "{ 'type': 'wireframe', 'form': 'tube', 'depth': 4, 'radius': 1, 'pitch': 0, 'distance': 0.5 }",
            "{ 'type': 'wireframe', 'low': 9000, 'high': 100, 'speed': 60, 'point': 'z = z + 0.3*sin(x*4); a = a*(1 - i)' }",
        };
        Engine engine;
        engine.setTheme(presets::defaultThemeColors());
        for (const QString &layer : layers) {
            const CompiledPreset preset = presets::compile(fixture(
                ("{ 'name': 'Wire', 'feedback': { 'decay': 0 }, 'layers': [ " + layer +
                 ", { 'type': 'particles', 'emit': 'wave' } ], 'post': { 'bloom': 0 } }").toUtf8()));
            QVERIFY2(preset.problems.isEmpty(), qPrintable(layer + ": " + preset.firstError()));
            Analyzer analyzer;
            std::vector<float> chunk(800 * 2);
            engine.setPreset(preset, 0);
            for (int frame = 0; frame < 60; ++frame) {
                synthesize(chunk.data(), 800, qint64(frame) * 800, 48000);
                analyzer.push(chunk.data(), 800);
                engine.render(analyzer.analyze(1.0 / 60), 1.0 / 60, target.handle(), 320, 200);
            }
            QVERIFY2(engine.error().isEmpty(), qPrintable(engine.error()));
            QVERIFY2(lit() > 40, qPrintable(layer + " is black"));
        }

        // The form is there in silence too, lying flat, and its code can
        // lift it off the screen's plane: a sheet seen edge-on is one line
        // until z is turned into height.
        const auto edgeOn = [](const char *code) {
            return presets::compile(fixture(QByteArray(
                "{ 'name': 'Edge', 'feedback': { 'decay': 0 }, 'layers': [ { 'type': 'wireframe', 'pitch': 0, "
                "'fog': 0, 'color': 'white', 'point': '") + code + "' } ], 'post': { 'bloom': 0, 'vignette': 0 } }"));
        };
        engine.setPreset(edgeOn(""), 0);
        for (int frame = 0; frame < 3; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 320, 200);
        const int flat = lit();
        QVERIFY2(flat > 20 && flat < 600, qPrintable(QString::number(flat)));
        engine.setPreset(edgeOn("y = z*0.5"), 0);
        for (int frame = 0; frame < 3; ++frame)
            engine.render(AudioFrame(), 1.0 / 60, target.handle(), 320, 200);
        QVERIFY2(lit() > flat * 3, qPrintable(QString("%1 then %2").arg(flat).arg(lit())));
    }

    void presetsTakeBackgroundsAndInteractingLayers()
    {
        const CompiledPreset good = presets::compile(fixture(
            "{ 'name': 'Both', 'background': { 'style': 'stars', 'intensity': '0.3 + bass_att' }, 'feedback': { "
            "'darkenCenter': true }, 'layers': [ { 'type': 'bars' }, { 'type': 'particles', 'emit': 'bars', 'pull': "
            "'-2*beat', 'edge': 'wrap' }, { 'type': 'vectors', 'cols': 12 } ], 'post': { 'colorShift': 'time*0.1', "
            "'colorCurve': 2 } }"));
        QVERIFY2(good.ok(), qPrintable(good.firstError()));
        QVERIFY(good.compositeShader.contains("f_hash(cell"));
        QVERIFY(!good.compositeShader.contains("%bg_"));
        const CompiledPreset bad = presets::compile(fixture(
            "{ 'name': 'Bad', 'background': { 'style': 'lava' }, 'layers': [ { 'type': 'particles', 'emit': "
            "'nowhere' } ] }"));
        QStringList paths;
        for (const auto &problem : bad.problems)
            paths << problem.path;
        QVERIFY2(paths.contains("background.style") && paths.contains("layers.0.emit"), qPrintable(paths.join(' ')));
    }

    void mashupsHoldTogether()
    {
        QTemporaryDir directory;
        Backend backend(directory.path(), false);
        QVERIFY(backend.visitorPool().size() >= 5);
        for (int round = 0; round < 12; ++round) {
            QVERIFY(backend.mashup());
            QVERIFY(backend.currentName().contains(" × "));
            QVERIFY2(backend.problems().isEmpty() || !backend.problems().first().toMap().value("error").toBool(),
                     qPrintable(backend.currentName()));
            QVERIFY(!backend.dirty());
        }
        // A mash-up is nobody's until it is saved.
        QVERIFY(!backend.currentIsYours());
        QVERIFY(backend.save());
        QVERIFY(backend.currentIsYours());
    }

    // A mash-up takes its motion from one preset and its layers from
    // another. A preset that only moves through its layers has no motion to
    // lend, and is never asked for it.
    void mashupsBorrowMotionOnlyFromPresetsThatMove()
    {
        QVERIFY(presets::moves(fixture("{ 'feedback': { 'zoom': 1.02 } }")));
        QVERIFY(presets::moves(fixture("{ 'feedback': { 'rot': '0.01*sin(time)' } }")));
        QVERIFY(presets::moves(fixture("{ 'feedback': { 'decay': 0.9 }, 'pixel': 'dx = 0.01' }")));
        QVERIFY(!presets::moves(fixture("{ 'feedback': { 'decay': 0.9, 'blur': 0.3, 'zoom': 1 } }")));
        QVERIFY(!presets::moves(fixture("{ 'layers': [] }")));

        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        QStringList still;
        for (int i = 0; i < backend.library().presets().size(); ++i)
            if (!presets::moves(backend.library().load(i)))
                still << backend.library().presets()[i].name;
        QVERIFY(still.contains("Pendulum"));
        QVERIFY(still.contains("Clockwork"));
        for (int i = 0; i < 60; ++i) {
            if (!backend.mashup())
                continue;
            const QString lender = backend.currentName().section(" × ", 0, 0);
            QVERIFY2(!still.contains(lender), qPrintable(backend.currentName()));
        }
    }

    // With somewhere else to build shaders, a preset comes on when they are
    // ready and the one showing carries on until then; one overtaken before
    // it was ready never shows.
    void engineWaitsForShadersBuiltElsewhere()
    {
        QOffscreenSurface surface;
        surface.create();
        QOpenGLContext context;
        QVERIFY(context.create());
        QVERIFY(context.makeCurrent(&surface));
        struct Slow : ProgramBuilder, QOpenGLExtraFunctions {
            struct Asked { int id; QString warp, composite; };
            QList<Asked> asked;
            QList<int> ready;
            void request(int id, const QString &warp, const QString &composite) override { asked.append({id, warp, composite}); }
            bool take(Result *result) override {
                if (ready.isEmpty())
                    return false;
                const int id = ready.takeFirst();
                for (const Asked &job : asked) {
                    if (job.id != id)
                        continue;
                    result->id = id;
                    result->warp = Engine::buildProgram(this, false, Engine::screenVertexShader(), job.warp, &result->error);
                    result->composite = Engine::buildProgram(this, false, Engine::screenVertexShader(), job.composite, &result->error);
                }
                return true;
            }
        } builder;
        builder.initializeOpenGLFunctions();
        {
            Engine engine;
            engine.setProgramBuilder(&builder);
            QOpenGLFramebufferObject target(160, 90);
            Analyzer analyzer;
            auto draw = [&] { engine.render(analyzer.analyze(1.0 / 60), 1.0 / 60, target.handle(), 160, 90); };
            const auto preset = [](const char *name, const char *init) {
                return presets::compile(fixture(QByteArray("{ 'name': '") + name + "', 'init': '" + init +
                                                "', 'layers': [ { 'type': 'wave' } ] }"));
            };
            engine.setPreset(preset("First", "mark = 1"), 0);
            draw();
            draw();
            QCOMPARE(builder.asked.size(), 1);
            QCOMPARE(engine.variable("mark"), 0.0); // nothing showing yet
            builder.ready << builder.asked[0].id;
            draw();
            QCOMPARE(engine.variable("mark"), 1.0);

            // The same preset with only a number changed needs no shaders
            // built: it is there on the next frame, and what its code had
            // built up carries over.
            engine.setPreset(presets::compile(fixture(
                "{ 'name': 'First', 'init': 'mark = 1', 'feedback': { 'zoom': 1.05 }, 'layers': [ { 'type': 'wave' } ] }")), 0);
            draw();
            QCOMPARE(builder.asked.size(), 1);
            QCOMPARE(engine.variable("zoom"), 1.05);
            QCOMPARE(engine.variable("mark"), 1.0);

            // Two more in quick succession with different shaders: only the
            // later one arrives.
            engine.setPreset(preset("Second", "mark = 2"), 0);
            draw();
            engine.setPreset(presets::compile(fixture(
                "{ 'name': 'Third', 'init': 'mark = 3', 'pixel': 'dx = 0.01', 'layers': [ { 'type': 'wave' } ] }")), 0);
            draw();
            QCOMPARE(builder.asked.size(), 3);
            QCOMPARE(engine.variable("mark"), 1.0);
            builder.ready << builder.asked[1].id << builder.asked[2].id;
            draw();
            QCOMPARE(engine.variable("mark"), 3.0);
            QVERIFY(engine.error().isEmpty());

            // A slider dragged while a change is being built: each tick
            // joins the build under way instead of starting another, and
            // the change still fades in rather than cutting.
            engine.setPreset(preset("Fourth", "mark = 4"), 4);
            draw();
            QCOMPARE(builder.asked.size(), 4);
            for (const char *zoom : {"1.01", "1.02", "1.03"}) {
                engine.setPreset(presets::compile(fixture(QByteArray("{ 'name': 'Fourth', 'init': 'mark = 4', "
                                                                     "'feedback': { 'zoom': ") + zoom +
                                                          " }, 'layers': [ { 'type': 'wave' } ] }")), 0);
                draw();
            }
            QCOMPARE(builder.asked.size(), 4);
            QCOMPARE(engine.variable("mark"), 3.0);
            builder.ready << builder.asked[3].id;
            draw();
            QCOMPARE(engine.variable("mark"), 4.0);
            QCOMPARE(engine.variable("zoom"), 1.03);
            QVERIFY(engine.changing());
        }
        context.doneCurrent();
    }

    void captureListsWhatThereIsToHear()
    {
#ifdef Q_OS_UNIX
        const QByteArray dump = QByteArray(
            "[ { 'id': 80, 'type': 'PipeWire:Interface:Node', 'info': { 'state': 'running', 'props': { 'media.class': "
            "'Stream/Output/Audio', 'application.name': 'Spotify', 'node.name': 'spotify', 'object.serial': 292 } } "
            "}, { 'id': 81, 'type': 'PipeWire:Interface:Node', 'info': { 'props': { 'media.class': "
            "'Stream/Input/Audio', 'node.name': 'trance' } } }, { 'id': 58, 'type': 'PipeWire:Interface:Node', "
            "'info': { 'props': { 'media.class': 'Audio/Sink', 'node.name': 'alsa_output.speaker', "
            "'node.description': 'Speaker', 'object.serial': 58 } } }, { 'id': 60, 'type': "
            "'PipeWire:Interface:Node', 'info': { 'props': { 'media.class': 'Audio/Source', 'node.name': "
            "'alsa_input.mic', 'node.description': 'Digital Microphone' } } }, { 'id': 3, 'type': "
            "'PipeWire:Interface:Link', 'info': {} } ]").replace('\'', '"');
        const auto nodes = AudioCapture::parseNodes(dump);
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(nodes.first().application, QString("Spotify"));
        QCOMPARE(nodes.first().serial, 292);
        // A stream that is playing is told from one paused or idle.
        QVERIFY(nodes.first().running);
        QVERIFY(!nodes.at(1).running);
        QStringList ids;
        for (const auto &entry : AudioCapture::describe(nodes, "app:Firefox"))
            ids << entry.toMap().value("id").toString();
        // One output and one input are what "system" and "microphone" already
        // mean, so neither is listed a second time.
        QCOMPARE(ids, (QStringList{"system", "app:Spotify", "app:Firefox", "microphone"}));
        QVERIFY(AudioCapture::isSource("app:Spotify"));
        QVERIFY(AudioCapture::isSource("output:alsa_output.speaker"));
        QVERIFY(!AudioCapture::isSource("app:"));
        QVERIFY(!AudioCapture::isSource("speakers"));
        // An empty answer from PipeWire still leaves the two that always work.
        QCOMPARE(AudioCapture::describe(AudioCapture::parseNodes("not json"), "system").size(), 2);
#else
        QSKIP("What PipeWire says is only read where PipeWire is");
#endif
    }

    // A laptop's sound card as PipeWire reports it: a node for every socket,
    // most with nothing plugged in, all named after the chipset.
    void captureLeavesOutWhatIsNotPluggedIn()
    {
#ifdef Q_OS_UNIX
        const auto node = [](int id, const char *mediaClass, const char *name, const char *description, int socket) {
            return QString("{ 'id': %1, 'type': 'PipeWire:Interface:Node', 'info': { 'props': { 'media.class': '%2', "
                           "'node.name': '%3', 'node.description': 'Alder Lake Audio Controller %4', 'device.id': 48, "
                           "'card.profile.device': %5 } } }")
                .arg(id).arg(mediaClass, name, description).arg(socket);
        };
        const auto route = [](const char *direction, const char *available, int socket) {
            return QString("{ 'direction': '%1', 'available': '%2', 'devices': [ %3 ] }").arg(direction, available).arg(socket);
        };
        const QString card = QString("{ 'id': 48, 'type': 'PipeWire:Interface:Device', 'info': { 'props': { "
                                     "'device.description': 'Alder Lake Audio Controller', 'device.bus': 'pci' }, "
                                     "'params': { 'EnumRoute': [ %1 ] } } }")
                                 .arg(QStringList{route("Output", "no", 0), route("Output", "no", 1),
                                                  route("Output", "unknown", 3), route("Input", "no", 4),
                                                  route("Input", "unknown", 5), route("Output", "no", 6)}
                                          .join(", "));
        const QStringList laptop{node(55, "Audio/Sink", "hdmi2", "HDMI / DisplayPort 2 Output", 1),
                                 node(56, "Audio/Sink", "hdmi1", "HDMI / DisplayPort 1 Output", 0),
                                 node(58, "Audio/Sink", "speaker", "Speaker", 3),
                                 node(59, "Audio/Source", "mic2", "Stereo Microphone", 4),
                                 node(60, "Audio/Source", "mic1", "Digital Microphone", 5), card};
        const auto parse = [](const QStringList &objects) {
            return AudioCapture::parseNodes(("[ " + objects.join(", ") + " ]").replace('\'', '"').toUtf8());
        };
        const auto listed = [](const QVariantList &list) {
            QStringList lines;
            for (const auto &entry : list)
                lines << entry.toMap().value("id").toString() + "=" + entry.toMap().value("label").toString();
            return lines;
        };

        // Nothing plugged in: the speaker and the built-in microphone are
        // the only things there, and they need no entries of their own.
        auto nodes = parse(laptop);
        QCOMPARE(nodes.size(), 5);
        QVERIFY(!nodes[0].available);
        QVERIFY(nodes[2].available);
        QCOMPARE(nodes[2].label, QString("Speaker"));
        QCOMPARE(listed(AudioCapture::describe(nodes, "system")),
                 (QStringList{"system=Everything that's playing", "microphone=Microphone"}));

        // A device that was chosen stays on the list, by its short name, and
        // says so when it has been unplugged.
        QCOMPARE(listed(AudioCapture::describe(nodes, "output:speaker")),
                 (QStringList{"system=Everything that's playing", "output:speaker=Speaker", "microphone=Microphone"}));
        QCOMPARE(listed(AudioCapture::describe(nodes, "output:hdmi1")).at(1),
                 QString("output:hdmi1=HDMI / DisplayPort 1 Output (not connected)"));
        QCOMPARE(listed(AudioCapture::describe(nodes, "input:gone")).last(), QString("input:gone=gone (not connected)"));

        // A screen on HDMI 1 and a USB microphone make both a real choice.
        QStringList docked = laptop;
        docked.last().replace("'direction': 'Output', 'available': 'no', 'devices': [ 0 ]",
                              "'direction': 'Output', 'available': 'yes', 'devices': [ 0 ]");
        docked << QString("{ 'id': 70, 'type': 'PipeWire:Interface:Device', 'info': { 'props': { 'device.description': "
                          "'Yeti Nano', 'device.bus': 'usb' } } }")
               << QString("{ 'id': 71, 'type': 'PipeWire:Interface:Node', 'info': { 'props': { 'media.class': "
                          "'Audio/Source', 'node.name': 'yeti', 'node.description': 'Yeti Nano Analog Stereo', "
                          "'device.id': 70, 'card.profile.device': 0 } } }");
        nodes = parse(docked);
        const QVariantList choice = AudioCapture::describe(nodes, "system");
        QCOMPARE(listed(choice),
                 (QStringList{"system=Everything that's playing", "output:hdmi1=HDMI / DisplayPort 1 Output",
                              "output:speaker=Speaker", "microphone=Default microphone",
                              "input:mic1=Digital Microphone", "input:yeti=Yeti Nano"}));
        QCOMPARE(choice.at(1).toMap().value("section").toString(), QString("Outputs"));
        QCOMPARE(choice.at(3).toMap().value("section").toString(), QString("Inputs"));
        QVERIFY(choice.at(0).toMap().value("section").toString().isEmpty());
#else
        QSKIP("What PipeWire says is only read where PipeWire is");
#endif
    }

    // On Windows the listening is threads of its own talking to the sound
    // system. Without a sound card to hear, what can be checked is that each
    // kind of source starts, says what is wrong when something is, and stops
    // without waiting or falling over, however quickly it is asked to.
    void captureStartsAndStopsOnWindows()
    {
#ifdef Q_OS_WIN
        Analyzer analyzer;
        {
            AudioCapture capture(&analyzer);
            capture.start("app:no-such-application");
            QTRY_COMPARE_WITH_TIMEOUT(capture.error(), QString("Waiting for no-such-application to play something."),
                                      8000);
        }
        for (const QString &source : {QString("output:no-such-device"), QString("input:no-such-device")}) {
            AudioCapture capture(&analyzer);
            capture.start(source);
            QTRY_COMPARE_WITH_TIMEOUT(capture.error(), QString("Audio capture stopped: the device is not connected."),
                                      8000);
        }
        // Whether these two can be heard depends on the computer, so what
        // happened is only noted.
        for (const QString &source : {QString("system"), QString("microphone")}) {
            AudioCapture capture(&analyzer);
            capture.start(source);
            QTest::qWait(1500);
            qInfo().noquote() << source << "gives:"
                              << (capture.error().isEmpty() ? QString("sound, or silence") : capture.error());
            for (int i = 0; i < 20; ++i) {
                capture.start(source);
                QTest::qWait(i * 5);
            }
            capture.stop();
        }
        // The list always has everything and the microphone on it.
        AudioCapture capture(&analyzer);
        QSignalSpy listed(&capture, &AudioCapture::sourcesChanged);
        capture.refreshSources();
        capture.refreshSources();
        QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 8000);
        QVERIFY(capture.sources().size() >= 2);
        QList<AudioCapture::Node> nodes;
        QString error;
        QVERIFY2(AudioCapture::listNodes(&nodes, &error), qPrintable(error));
        for (const auto &entry : AudioCapture::describe(nodes, {}))
            qInfo().noquote() << "source:" << entry.toMap().value("id").toString() << "="
                              << entry.toMap().value("label").toString();
#else
        QSKIP("How Windows is listened to is only tried on Windows");
#endif
    }

    // With a sound card, and a tone that comes and goes playing on it, each
    // kind of source has to hear the tone, hear it stop, and hear it come
    // back. Hearing it stop is the part that depends on silence being passed
    // on: the analyzer holds on to the last sound it was given. GitHub runs
    // this with a virtual cable for a sound card and tests/tone.ps1 for the
    // tone; TRANCE_TEST_HEARS names the application that plays it.
    void captureHearsWhatPlays()
    {
#ifdef Q_OS_WIN
        const QString player = qEnvironmentVariable("TRANCE_TEST_HEARS");
        if (player.isEmpty())
            QSKIP("Needs a sound card and a tone playing on it: see tests/tone.ps1");
        // The player is on the list while its tone sounds, and the devices are.
        QStringList sources;
        QElapsedTimer waited;
        waited.start();
        for (bool found = false; !found;) {
            QList<AudioCapture::Node> nodes;
            QString error;
            QVERIFY2(AudioCapture::listNodes(&nodes, &error), qPrintable(error));
            sources = {"system", "app:" + player, "microphone"};
            for (const AudioCapture::Node &node : nodes) {
                if (node.kind == AudioCapture::Node::Application)
                    found = found || node.application.compare(player, Qt::CaseInsensitive) == 0;
                else if (node.available)
                    sources << (node.kind == AudioCapture::Node::Output ? "output:" : "input:") + node.name;
            }
            QVERIFY2(found || waited.elapsed() < 20000, "The application playing the tone never got on the list");
            if (!found)
                QTest::qWait(200);
        }
        QVERIFY2(sources.size() >= 5, "No output and input to hear the tone through");

        for (const QString &source : sources) {
            Analyzer analyzer;
            AudioCapture capture(&analyzer);
            capture.start(source);
            QElapsedTimer since;
            since.start();
            const auto silent = [&] { return analyzer.analyze(since.restart() / 1000.0).silent; };
            QTRY_VERIFY2_WITH_TIMEOUT(!silent(), qPrintable(source + " was not heard. " + capture.error()), 20000);
            QTRY_VERIFY2_WITH_TIMEOUT(silent(), qPrintable(source + " was not heard to stop"), 20000);
            QTRY_VERIFY2_WITH_TIMEOUT(!silent(), qPrintable(source + " was not heard again. " + capture.error()), 20000);
            QVERIFY2(capture.error().isEmpty(), qPrintable(capture.error()));
            qInfo().noquote() << "heard, heard to stop, and heard again:" << source;
        }
#else
        QSKIP("How Windows is listened to is only tried on Windows");
#endif
    }

    // Titles on Windows come from its media controls. The test is the media
    // players itself: two of them, saying what they play as a browser does.
    // Trance has to take the track of one already playing when it starts,
    // follow it to the next, leave a paused player's title be, and stay with
    // the player it shows while that one plays, whatever another starts.
    // TRANCE_TEST_TITLES asks for it, since a real player playing at the same
    // time would be taken instead; GitHub's runner has none.
    void titlesFollowWhatWindowsSaysIsPlaying()
    {
#ifdef Q_OS_WIN
        if (!qEnvironmentVariableIsSet("TRANCE_TEST_TITLES"))
            QSKIP("Plays the media player to Windows, with no real one playing: set TRANCE_TEST_TITLES");
        const auto told = qScopeGuard([] {
            if (QTest::currentTestFailed())
                for (const QString &player : playersWindowsLists())
                    qInfo().noquote() << "player:" << player;
        });
        try {
            FakePlayer first(L"Trance.Tests.First");
            first.announce(L"One", L"Somebody");
            first.play(true);

            // A player already playing when Trance starts.
            NowPlaying nowPlaying;
            QSignalSpy changed(&nowPlaying, &NowPlaying::changed);
            QTRY_COMPARE_WITH_TIMEOUT(nowPlaying.title(), QString("One"), 10000);
            QCOMPARE(nowPlaying.artist(), QString("Somebody"));
            QCOMPARE(changed.count(), 1);

            // Its next track.
            first.announce(L" Two ", L"Somebody else");
            QTRY_COMPARE(nowPlaying.title(), QString("Two"));
            QCOMPARE(nowPlaying.artist(), QString("Somebody else"));

            // Paused, it keeps the title it had, whatever it moves on to...
            first.play(false);
            QTest::qWait(300);
            first.announce(L"Three", L"Somebody");
            QTest::qWait(1500);
            QCOMPARE(nowPlaying.title(), QString("Two"));
            // ...until it plays again.
            first.play(true);
            QTRY_COMPARE(nowPlaying.title(), QString("Three"));

            // Another player starting does not take over from the one shown.
            auto second = std::make_unique<FakePlayer>(L"Trance.Tests.Second");
            second->announce(L"Elsewhere", L"Nobody");
            second->play(true);
            QTest::qWait(1500);
            // Windows lists the two apart, by the ids they gave.
            const QStringList both = playersWindowsLists();
            for (const QString &player : both)
                qInfo().noquote() << "player:" << player;
            QCOMPARE(both.filter(QRegularExpression("^Trance\\.Tests\\.(First|Second): status 4,")).size(), 2);
            QCOMPARE(nowPlaying.title(), QString("Three"));
            // It does once the one shown is paused...
            first.play(false);
            QTRY_COMPARE(nowPlaying.title(), QString("Elsewhere"));
            // ...and is then the one stayed with.
            first.announce(L"Four", L"Somebody");
            first.play(true);
            QTest::qWait(1500);
            QCOMPARE(nowPlaying.title(), QString("Elsewhere"));
            // When it closes, the one still playing is taken.
            second.reset();
            QTRY_COMPARE(nowPlaying.title(), QString("Four"));
            QCOMPARE(changed.count(), 5);
        } catch (const winrt::hresult_error &error) {
            QFAIL(qPrintable("Windows said: " + fromWindows(error.message())));
        }
#else
        QSKIP("Windows' media controls are only asked on Windows");
#endif
    }

    // What shows over the picture is the user's to ask for: nothing does
    // until they have, and what they choose is remembered.
    void backendKeepsThePictureClearUnlessAsked()
    {
        QTemporaryDir dir;
        {
            Backend backend(dir.path(), false);
            QVERIFY(!backend.showNames());
            QVERIFY(!backend.debug());
            backend.setShowNames(true);
            backend.setDebug(true);

            const QVariantMap stats = backend.stats();
            QCOMPARE(stats.value("waiting").toString(), QString());
            QVERIFY(stats.value("nextIn").toDouble() > 0 && stats.value("nextIn").toDouble() <= backend.cycleSeconds());
            QVERIFY(stats.value("layers").toInt() > 0);
            QCOMPARE(stats.value("sampleRate").toInt(), 48000);
            backend.setLocked(true);
            QCOMPARE(backend.stats().value("waiting").toString(), QString("held"));
            backend.setLocked(false);
            backend.setEditing(true);
            QCOMPARE(backend.stats().value("waiting").toString(), QString("editing"));
            backend.setEditing(false);
            backend.setCycleSeconds(0);
            QCOMPARE(backend.stats().value("waiting").toString(), QString("never"));

            // What is being listened to goes by its plain name until
            // PipeWire has said whether it is there.
            QCOMPARE(backend.sourceLabel(), QString("Everything that's playing"));
            backend.setSource("app:Spotify");
            QCOMPARE(backend.sourceLabel(), QString("Spotify"));
            backend.setSource("output:alsa_output.speaker");
            QCOMPARE(backend.sourceLabel(), QString("alsa_output.speaker"));
            // The window is told when that name may have changed.
            QSignalSpy relabelled(&backend, &Backend::sourcesChanged);
            backend.setSource("system");
            QCOMPARE(relabelled.count(), 1);
            const QMetaProperty label = backend.metaObject()->property(backend.metaObject()->indexOfProperty("sourceLabel"));
            QCOMPARE(label.notifySignal().name(), QByteArray("sourcesChanged"));
            QCOMPARE(label.read(&backend).toString(), QString("Everything that's playing"));

            // The levels are announced only while something is showing them.
            QSignalSpy levels(&backend, &Backend::levelsChanged);
            QTest::qWait(160);
            QCOMPARE(levels.count(), 0);
            backend.setMetering(true);
            QTRY_VERIFY(levels.count() > 0);
        }
        Backend again(dir.path(), false);
        QVERIFY(again.showNames());
        QVERIFY(again.debug());

        // How dark the glass is: clear unless asked, kept, and held in range.
        QCOMPARE(again.glassOpacity(), 0.0);
        again.setGlassOpacity(0.35);
        again.setGlassOpacity(7);
        QCOMPARE(again.glassOpacity(), 1.0);
        again.setGlassOpacity(0.35);
        Backend third(dir.path(), false);
        QCOMPARE(third.glassOpacity(), 0.35);

        // Settings from before there were title styles: titles turned off
        // stay off.
        QTemporaryDir older;
        write(older.path() + "/settings.ini", "[General]\nshowTitles=false\n");
        QCOMPARE(Backend(older.path(), false).titleStyle(), QString("off"));
    }

    // `trance next` and friends, as the window hears them. The command waits
    // for its answer, so it is given a thread of its own to wait on.
    void remoteSteersTheWindow()
    {
        QTemporaryDir dir;
        Backend backend(dir.path(), false);
        const QString name = dir.filePath("remote.sock");
        const auto ask = [&](const QString &request) {
            QString answer;
            std::atomic<bool> done{false};
            std::thread asking([&] {
                if (!remote::send(request, &answer, name, 1000))
                    answer = "nobody";
                done = true;
            });
            while (!done)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            asking.join();
            return answer;
        };
        QCOMPARE(ask("Current"), QString("nobody"));

        // What a window that crashed left in the way doesn't keep this one out.
        write(name, "left behind");
        remote::Server server(&backend, name);
        QVERIFY(server.isListening());
        QCOMPARE(ask("Current"), "ok " + backend.currentName());
        QCOMPARE(ask("Select Whitecaps"), QString("ok"));
        QCOMPARE(backend.currentName(), QString("Whitecaps"));
        QCOMPARE(ask("Select Nothing By This Name"), QString("no"));
        QCOMPARE(backend.currentName(), QString("Whitecaps"));
        const bool locked = backend.locked();
        QCOMPARE(ask("ToggleLock"), QString("ok"));
        QCOMPARE(backend.locked(), !locked);
        QCOMPARE(ask("Next"), QString("ok"));
        // An application goes by its name alone; anything else by its id.
        QCOMPARE(ask("Source Spotify"), QString("ok"));
        QCOMPARE(backend.source(), QString("app:Spotify"));
        QCOMPARE(ask("Source microphone"), QString("ok"));
        QCOMPARE(backend.source(), QString("microphone"));
        QCOMPARE(ask("Source"), QString("no"));
        QCOMPARE(ask("Dance"), QString("no"));

        // The first window keeps the place; one opened beside it is left out.
        remote::Server second(&backend, name);
        QVERIFY(!second.isListening());
        QCOMPARE(ask("Current"), "ok " + backend.currentName());
    }

    // render --frames: every frame from --start on, numbered from one, for
    // ffmpeg to make a video of.
    void renderWritesFrames()
    {
        QOpenGLContext context; // only to know whether there is OpenGL here
        if (!context.create())
            QSKIP("No OpenGL context here");
        QTemporaryDir dir;
        const QString frames = dir.filePath("frames");
        const QStringList render{"trance", "render", "Whitecaps", "--size", "64x36", "--fps", "10", "--seconds", "2"};
        QCOMPARE(runCli(render + QStringList{"--frames", frames, "--start", "1.5", "--then", "Starwake", "--drift"}),
                 0);
        QCOMPARE(QDir(frames).entryList(QDir::Files),
                 (QStringList{"00001.png", "00002.png", "00003.png", "00004.png", "00005.png"}));
        QCOMPARE(QImage(frames + "/00005.png").size(), QSize(64, 36));
        // One picture or every frame, not both; and a start within the run.
        QCOMPARE(runCli(render + QStringList{"--frames", frames, "--sheet", "4"}), 1);
        QCOMPARE(runCli(render + QStringList{"--frames", frames, "-o", dir.filePath("one.png")}), 1);
        QCOMPARE(runCli(render + QStringList{"--start", "1", "-o", dir.filePath("one.png")}), 1);
        QCOMPARE(runCli(render + QStringList{"--frames", frames, "--start", "2"}), 1);
        QVERIFY(!QFile::exists(dir.filePath("one.png")));
    }

    // Colors drifting on their own always go somewhere new, never to mono,
    // and stay in a family of colors when given one.
    void colorsDriftSomewhereElse()
    {
        for (int i = 0; i < 200; ++i) {
            const QString next = presets::driftPalette("ocean");
            QVERIFY(next != "ocean" && next != "mono");
            QVERIFY(next.isEmpty() || presets::paletteNames().contains(next));
            const QString blue = presets::driftPalette("ocean", "blue");
            QVERIFY(blue != "ocean");
            QVERIFY(blue.isEmpty() || presets::colorFamily(presets::paletteStops(blue)) == "blue");
        }
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QVERIFY(theme.colors().isEmpty());
    }

    void themeReadsEveryColor()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "mode = \"dark\"\naccent = \"#112233\"\nbright_red = '#FF7A93'\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.colors().value("bright_red").name(), QString("#ff7a93"));
        QCOMPARE(theme.colors().size(), 2);
    }

    void themeFollowsHyprlandRounding()
    {
#ifdef Q_OS_UNIX
        // A stand-in Hyprland: one socket answers questions and hangs up,
        // the other announces what happens.
        QTemporaryDir dir;
        QByteArray answer = R"({"option": "decoration:rounding", "int": 0, "set": true })";
        int asked = 0, closed = 0, late = 0;
        bool answering = true;
        QLocalServer requests, events;
        QVERIFY(requests.listen(dir.filePath(".socket.sock")));
        QVERIFY(events.listen(dir.filePath(".socket2.sock")));
        connect(&requests, &QLocalServer::newConnection, &requests, [&] {
            QLocalSocket *client = requests.nextPendingConnection();
            connect(client, &QLocalSocket::disconnected, client, [&] { ++closed; });
            // Hyprland waits for the question the moment it takes the
            // connection, and the whole desktop with it. So does this, on
            // the thread Trance's own window code runs on: a question that
            // needs that thread to be sent can't arrive, and is late.
            if (!client->bytesAvailable() && !client->waitForReadyRead(1000)) {
                ++late;
                return;
            }
            QCOMPARE(client->readAll(), QByteArray("j/getoption decoration:rounding"));
            ++asked;
            if (!answering)
                return;
            client->write(answer);
            client->disconnectFromServer();
        });

        Theme theme(dir.path(), nullptr, dir.path());
        QSignalSpy changed(&theme, &Theme::changed);
        QTRY_COMPARE(asked, 1);
        QCOMPARE(theme.radius(), 0);

        // It asks again only when Hyprland says its config was reloaded.
        answer.replace("\"int\": 0", "\"int\": 6");
        QTRY_VERIFY(events.hasPendingConnections());
        QLocalSocket *listener = events.nextPendingConnection();
        listener->write("activewindow>>kitty,zsh\n");
        QTest::qWait(100);
        QCOMPARE(asked, 1);
        listener->write("configreloaded>>\n");
        QTRY_COMPARE(theme.radius(), 6);
        QCOMPARE(asked, 2);
        QCOMPARE(changed.count(), 1);

        // A change made at runtime sends no event, so it is also asked when
        // the window becomes active.
        answer.replace("\"int\": 6", "\"int\": 8");
        emit qGuiApp->applicationStateChanged(Qt::ApplicationInactive);
        QTest::qWait(100);
        QCOMPARE(asked, 2);
        emit qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        QTRY_COMPARE(theme.radius(), 8);
        QCOMPARE(asked, 3);
        QTRY_COMPARE(closed, 3);

        // A dropped event connection is made again, and the rounding asked
        // for once more, since a reload may have gone unheard meanwhile.
        answer.replace("\"int\": 8", "\"int\": 12");
        delete listener;
        QTRY_COMPARE(theme.radius(), 12);
        QCOMPARE(asked, 4);
        QTRY_COMPARE(closed, 4);

        // A question Hyprland takes but never answers is given up on.
        answering = false;
        QTRY_VERIFY(events.hasPendingConnections());
        events.nextPendingConnection()->write("configreloaded>>\n");
        QTRY_COMPARE(asked, 5);
        QCOMPARE(closed, 4);
        QTRY_COMPARE(closed, 5);
        QCOMPARE(theme.radius(), 12);
        QCOMPARE(late, 0);
#else
        QSKIP("Hyprland is not on this system");
#endif
    }

private:
    // Test presets are written with single quotes, to stay readable in C++.
    static QJsonObject fixture(const char *text)
    {
        return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
    }

    static double evaluate(const QString &source)
    {
        expr::Vars vars;
        vars.declare("pi", M_PI);
        expr::Program program;
        QString error;
        if (!program.parse(source, vars, &error))
            qFatal("%s: %s", qPrintable(source), qPrintable(error));
        return program.run(vars.values);
    }

    static QString parseError(const QString &source)
    {
        expr::Vars vars;
        expr::Program program;
        QString error;
        return program.parse(source, vars, &error) ? QString() : error;
    }

    // Feeds the analyzer sixty frames a second and returns the last one.
    template <typename Source>
    static AudioFrame play(Analyzer &analyzer, double seconds, Source source)
    {
        std::vector<float> chunk(800 * 2);
        AudioFrame frame;
        for (int i = 0; i < int(seconds * 60); ++i) {
            source(chunk.data(), 800, qint64(i) * 800);
            analyzer.push(chunk.data(), 800);
            frame = analyzer.analyze(1.0 / 60);
        }
        return frame;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(TranceTests)
#include "trance_tests.moc"
