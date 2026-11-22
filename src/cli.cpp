#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "audio.h"
#include "audiocapture.h"
#include "engine.h"
#include "library.h"
#include "preset.h"
#include "remote.h"
#include "theme.h"

namespace {

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        m_name = name;
    }
    void json() { parser.addOption({"json", "Print the result as JSON."}); }
    void process(const QStringList &arguments) {
        // Drop the command word, so the parser sees "trance <name>" as the program.
        QStringList rest = arguments;
        rest[0] = rest[0] + " " + m_name;
        rest.removeAt(1);
        parser.process(rest);
    }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    QCommandLineParser parser;

private:
    QString m_name;
};

// An OpenGL context with no window, for rendering to an image.
struct Headless {
    QOpenGLContext context;
    QOffscreenSurface surface;
    bool create() {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        if (!context.create())
            return false;
        surface.setFormat(context.format());
        surface.create();
        return context.makeCurrent(&surface);
    }
};

ThemeColors currentTheme() { return Theme::readColors(Theme::defaultDirectory() + "/theme/colors.toml"); }

QJsonArray problemsJson(const QList<Problem> &problems) {
    QJsonArray array;
    for (const auto &problem : problems)
        array.append(QJsonObject{{"path", problem.path}, {"message", problem.message},
                                 {"severity", problem.error ? "error" : "warning"}});
    return array;
}

int list(const QStringList &arguments) {
    Command command("list", "List every preset: the built-in ones and your own.");
    command.json();
    command.process(arguments);
    const Library library;
    if (command.parser.isSet("json")) {
        QJsonArray array;
        for (const auto &preset : library.presets())
            array.append(QJsonObject{{"name", preset.name}, {"author", preset.author}, {"builtin", preset.builtin},
                                     {"shadowsBuiltin", preset.shadowsBuiltin}, {"path", preset.path},
                                     {"family", preset.family},
                                     {"features", QJsonArray::fromStringList(preset.features)}});
        print({{"presets", array}, {"directory", library.userDirectory()}});
    } else {
        for (const auto &preset : library.presets())
            print(stdout, preset.builtin           ? preset.name
                          : preset.shadowsBuiltin ? preset.name + "  (your copy, hiding the built-in)"
                                                  : preset.name + "  (yours)");
    }
    return 0;
}

int show(const QStringList &arguments) {
    Command command("show", "Print a preset's JSON, to read or to start a new one from.");
    command.parser.addPositionalArgument("preset", "A preset's name, or a file.");
    command.process(arguments);
    QString error;
    const QJsonObject preset = Library().find(command.argument(0), &error);
    if (preset.isEmpty())
        return fail(error);
    fputs(QJsonDocument(preset).toJson().constData(), stdout);
    return 0;
}

int check(const QStringList &arguments) {
    Command command("check", "Report every problem in one or more presets. Exits 1 when any has an error.");
    command.parser.addPositionalArgument("presets", "Preset names or files.", "<preset>...");
    command.json();
    command.process(arguments);
    if (command.parser.positionalArguments().isEmpty())
        return fail("Give check a preset name or file.");
    const Library library;
    // With a graphics context the generated shaders are compiled too; without
    // one, the check still covers everything written in the preset.
    Headless gl;
    const bool haveGl = gl.create();
    bool failed = false;
    QJsonArray results;
    for (const auto &argument : command.parser.positionalArguments()) {
        QString error;
        const QJsonObject json = library.find(argument, &error);
        QList<Problem> problems;
        if (json.isEmpty()) {
            problems.append(Problem{"", error, true});
        } else {
            const CompiledPreset compiled = presets::compile(json);
            problems = compiled.problems;
            if (compiled.ok() && haveGl) {
                Engine engine;
                QOpenGLFramebufferObject target(64, 64);
                engine.setPreset(compiled, 0);
                engine.render(AudioFrame(), 1.0 / 60, target.handle(), 64, 64);
                if (!engine.error().isEmpty())
                    problems.append(Problem{"", engine.error(), true});
            }
        }
        bool ok = true;
        for (const auto &problem : problems) {
            ok = ok && !problem.error;
            if (!command.parser.isSet("json"))
                print(problem.error ? stderr : stdout,
                      QString("%1: %2%3%4").arg(argument, problem.error ? "" : "warning: ",
                                                problem.path.isEmpty() ? "" : problem.path + ": ", problem.message));
        }
        failed = failed || !ok;
        results.append(QJsonObject{{"preset", argument}, {"ok", ok}, {"problems", problemsJson(problems)}});
        if (ok && !command.parser.isSet("json"))
            print(stdout, argument + ": ok");
    }
    if (command.parser.isSet("json"))
        print({{"ok", !failed}, {"presets", results}});
    return failed ? 1 : 0;
}

// Decodes any audio ffmpeg can read to the float stereo the analyzer takes.
bool decodeAudio(const QString &path, std::vector<float> *samples, QString *error) {
    QByteArray raw;
    if (path.endsWith(".f32") || path.endsWith(".raw")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = "Could not read " + path;
            return false;
        }
        raw = file.readAll();
    } else {
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) {
            *error = "ffmpeg is needed to read audio files; install it or pass raw .f32";
            return false;
        }
        QProcess process;
        process.start(ffmpeg, {"-v", "error", "-i", path, "-f", "f32le", "-ac", "2", "-ar", "48000", "-"});
        if (!process.waitForFinished(120000) || process.exitCode() != 0) {
            *error = "ffmpeg could not read " + path + ": " + QString::fromUtf8(process.readAllStandardError()).trimmed();
            return false;
        }
        raw = process.readAllStandardOutput();
    }
    samples->resize(size_t(raw.size()) / sizeof(float));
    memcpy(samples->data(), raw.constData(), samples->size() * sizeof(float));
    return true;
}

// A line of text as the engine wants it: white on nothing.
QImage titleImage(const QString &text) {
    QFont font = QGuiApplication::font();
    font.setPixelSize(120);
    font.setWeight(QFont::Bold);
    const QFontMetrics metrics(font);
    QImage image(metrics.horizontalAdvance(text) + 48, metrics.height() + 32, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::white);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, text);
    return image;
}

qint64 median(std::vector<qint64> values) {
    if (values.empty())
        return 0;
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

int render(const QStringList &arguments) {
    Command command("render", "Render a preset to a PNG, or every frame to PNGs, without opening a window, "
                              "reacting to a built-in test loop or to an audio file.");
    command.parser.addPositionalArgument("preset", "A preset's name, or a file.");
    command.parser.addOptions({
        {{"o", "output"}, "Where to write the PNG (default: <preset>.png).", "file"},
        {"size", "Picture size (default 1280x720).", "WxH", "1280x720"},
        {"seconds", "How long to run before the picture is taken (default 6).", "seconds", "6"},
        {"fps", "Frames per second to simulate (default 60).", "fps", "60"},
        {"audio", "Audio to react to: any file ffmpeg reads, or raw 48 kHz stereo .f32.", "file"},
        {"palette", "Use this palette instead of the preset's own.", "name"},
        {"sheet", "Tile this many evenly spaced frames into one picture.", "count", "1"},
        {"then", "Change to this preset halfway through, to see the two change over.", "preset"},
        {"blend", "How long that change takes, in seconds (default 4).", "seconds", "4"},
        {"transition", "The change's pattern: " + presets::transitionStyles().join(", ") + ".", "style", "mixed"},
        {"title", "Write this into the picture, as a new track's name is.", "text"},
        {"visitors", "Let the built-in visitors drop in, as they do in the window."},
        {"drift", "Let the colors drift, as they do in the window, but every few seconds."},
        {"frames", "Write every frame instead, as <dir>/00001.png and on, to make a video from.", "dir"},
        {"start", "With --frames, the first frame to write, in seconds (default 0), so the trails have built "
                  "up.", "seconds", "0"},
    });
    command.json();
    command.process(arguments);

    QString error;
    const Library library;
    const QJsonObject json = library.find(command.argument(0), &error);
    if (json.isEmpty())
        return fail(error);
    const CompiledPreset compiled = presets::compile(json);
    if (!compiled.ok())
        return fail(command.argument(0) + ": " + compiled.firstError());

    const QStringList size = command.parser.value("size").toLower().split('x');
    const int width = size.value(0).toInt(), height = size.value(1).toInt();
    const double seconds = command.parser.value("seconds").toDouble();
    const int fps = command.parser.value("fps").toInt();
    const int sheet = qBound(1, command.parser.value("sheet").toInt(), 64);
    if (width < 16 || height < 16 || width > 8192 || height > 8192)
        return fail("--size wants WIDTHxHEIGHT, such as 1920x1080.");
    if (seconds <= 0 || seconds > 3600 || fps < 1 || fps > 480)
        return fail("--seconds and --fps want positive numbers.");
    const QString frames = command.parser.value("frames");
    const double start = command.parser.value("start").toDouble();
    if (!frames.isEmpty() && (sheet > 1 || command.parser.isSet("output")))
        return fail("--frames writes every frame, so it takes neither --sheet nor --output.");
    if (command.parser.isSet("start") && frames.isEmpty())
        return fail("--start is for --frames.");
    if (start < 0 || start >= seconds)
        return fail("--start wants a time before --seconds is up.");
    if (!frames.isEmpty() && !QDir().mkpath(frames))
        return fail("Could not make " + frames);
    QString palette = command.parser.value("palette");
    if (!palette.isEmpty() && !presets::paletteNames().contains(palette))
        return fail("Unknown palette '" + palette + "'. Use one of: " + presets::paletteNames().join(", "));

    CompiledPreset second;
    if (command.parser.isSet("then")) {
        const QJsonObject other = library.find(command.parser.value("then"), &error);
        if (other.isEmpty())
            return fail(error);
        second = presets::compile(other);
        if (!second.ok())
            return fail(command.parser.value("then") + ": " + second.firstError());
    }
    const int transition = int(presets::transitionStyles().indexOf(command.parser.value("transition")));
    if (transition < 0)
        return fail("--transition wants one of: " + presets::transitionStyles().join(", "));

    std::vector<float> samples;
    const bool fromFile = command.parser.isSet("audio");
    if (fromFile && !decodeAudio(command.parser.value("audio"), &samples, &error))
        return fail(error);

    const int total = qMax(1, int(std::lround(seconds * fps)));
    // What is written begins here; the change and the title fall in it.
    const int first = qMin(total - 1, int(std::lround(start * fps)));
    const int written = total - first;

    Headless gl;
    if (!gl.create())
        return fail("Could not create an OpenGL context to render with.");
    QImage result;
    // With a sheet, the average over its pictures; the gaps between them
    // would count as dark.
    Exposure exposure;
    // What a frame costs: the time to draw it, and its vertices, the
    // measure of how much is drawn (each frame's count is known once the
    // next one starts). The first frame builds the shaders, so it is left
    // out, and the time is the middle one, which a stall elsewhere on the
    // machine doesn't move.
    std::vector<qint64> times;
    qint64 vertices = 0;
    {
        Engine engine;
        engine.setTheme(currentTheme());
        engine.setPaletteOverride(palette);
        engine.setPreset(compiled, 0);
        engine.setTransitionStyle(transition);
        if (command.parser.isSet("visitors")) {
            QList<CompiledPreset> pool;
            for (const QJsonObject &visitor : Library::visitors())
                pool.append(presets::compile(visitor));
            // Often enough to turn up in a short render.
            engine.setVisitors(pool, 8);
        }
        QOpenGLFramebufferObject target(width, height);
        Analyzer analyzer;
        double nextDrift = 4 + QRandomGenerator::global()->bounded(3.0);
        const int perFrame = analyzer.sampleRate() / fps;
        std::vector<float> chunk(size_t(perFrame) * 2);
        const int columns = int(std::ceil(std::sqrt(double(sheet))));
        const int rows = (sheet + columns - 1) / columns;
        if (sheet > 1) {
            result = QImage(width * columns, height * rows, QImage::Format_RGB32);
            result.fill(Qt::black);
        }
        int taken = 0;
        QElapsedTimer clock;
        for (int frame = 0; frame < total; ++frame) {
            const qint64 position = qint64(frame) * perFrame;
            if (fromFile) {
                // Past the end of the file there is silence.
                for (size_t i = 0; i < chunk.size(); ++i) {
                    const size_t at = size_t(position) * 2 + i;
                    chunk[i] = at < samples.size() ? samples[at] : 0.f;
                }
            } else {
                synthesize(chunk.data(), perFrame, position, analyzer.sampleRate());
            }
            analyzer.push(chunk.data(), perFrame);
            if (frame == first + written / 2 && command.parser.isSet("then"))
                engine.setPreset(second, qMax(0.0, command.parser.value("blend").toDouble()));
            if (frame == first + written / 3 && command.parser.isSet("title"))
                engine.showText(titleImage(command.parser.value("title")));
            if (command.parser.isSet("drift") && double(frame) / fps >= nextDrift) {
                nextDrift += 4 + QRandomGenerator::global()->bounded(3.0);
                palette = presets::driftPalette(palette);
                engine.setPaletteOverride(palette);
            }
            engine.setProgress(double(frame) / total);
            const AudioFrame &heard = analyzer.analyze(1.0 / fps);
            // Timed to the end of the drawing, so the graphics card's share
            // counts, and the vertices are those of the frame just drawn.
            clock.start();
            engine.render(heard, 1.0 / fps, target.handle(), width, height);
            QOpenGLContext::currentContext()->functions()->glFinish();
            if (frame > 0) {
                times.push_back(clock.nsecsElapsed());
                vertices += engine.vertexCount();
            }
            if (!engine.error().isEmpty())
                return fail(command.argument(0) + ": " + engine.error());
            if (!frames.isEmpty() && frame >= first) {
                const QString path = QDir(frames).filePath(QString("%1.png").arg(frame - first + 1, 5, 10, QChar('0')));
                // Light compression: these are for ffmpeg, and the heavy kind takes longer than the drawing.
                if (!target.toImage().convertToFormat(QImage::Format_RGB32).save(path, "PNG", 80))
                    return fail("Could not write " + path);
            }
            // Sheet frames are spread over the run and end on the last one.
            if (sheet > 1 && frame + 1 == (qint64(taken + 1) * total) / sheet) {
                const QImage tile = target.toImage();
                QPainter painter(&result);
                painter.drawImage((taken % columns) * width, (taken / columns) * height, tile);
                const Exposure seen = measureExposure(tile);
                exposure.clipped += seen.clipped / sheet;
                exposure.mean += seen.mean / sheet;
                ++taken;
            }
        }
        if (sheet == 1) {
            result = target.toImage().convertToFormat(QImage::Format_RGB32);
            exposure = measureExposure(result);
        }
    }

    if (!frames.isEmpty()) {
        if (command.parser.isSet("json"))
            print({{"frames", QFileInfo(frames).absoluteFilePath()}, {"count", written}, {"fps", fps},
                   {"width", width}, {"height", height}, {"preset", compiled.name},
                   {"problems", problemsJson(compiled.problems)}});
        else
            print(stdout, QString("Wrote %1 frames to %2").arg(written).arg(frames));
        return 0;
    }

    QString output = command.parser.value("output");
    if (output.isEmpty())
        output = Library::slug(compiled.name) + ".png";
    if (!result.save(output))
        return fail("Could not write " + output);
    if (command.parser.isSet("json"))
        print({{"output", QFileInfo(output).absoluteFilePath()}, {"width", result.width()},
               {"height", result.height()}, {"preset", compiled.name},
               {"clipped", std::round(exposure.clipped * 1000) / 1000},
               {"mean", std::round(exposure.mean * 1000) / 1000},
               {"ms", std::round(double(median(times)) / 1e4) / 100},
               {"vertices", times.empty() ? 0 : vertices / qint64(times.size())},
               {"problems", problemsJson(compiled.problems)}});
    else
        print(stdout, "Wrote " + output);
    return 0;
}

int create(const QStringList &arguments) {
    Command command("new", "Start a preset of your own, blank or copied from another, and print its file.");
    command.parser.addPositionalArgument("name", "What to call it.");
    command.parser.addOption({"from", "Copy this preset instead of starting blank.", "preset"});
    command.json();
    command.process(arguments);
    const QString name = command.argument(0).trimmed();
    if (name.isEmpty())
        return fail("Give the new preset a name.");
    Library library;
    if (library.indexOf(name) >= 0)
        return fail("There is already a preset called '" + name + "'.");
    QString error;
    QJsonObject preset = presets::blank(name);
    if (command.parser.isSet("from")) {
        preset = library.find(command.parser.value("from"), &error);
        if (preset.isEmpty())
            return fail(error);
        preset["name"] = name;
        preset.remove("author");
    }
    const QString path = library.save(preset, &error);
    if (path.isEmpty())
        return fail(error);
    if (command.parser.isSet("json"))
        print({{"name", name}, {"path", path}});
    else
        print(stdout, path);
    return 0;
}

// Sends one call to the running Trance.
int steer(const QStringList &arguments) {
    const QString word = arguments.value(1);
    Command command(word, word == "preset" ? "Switch a running Trance to the named preset."
                          : word == "source" ? "Have a running Trance listen to something else: an id from "
                                               "trance sources, or an application's name."
                                             : "Tell a running Trance to do this. Bind it to a key.");
    if (word == "preset")
        command.parser.addPositionalArgument("name", "The preset to show.");
    if (word == "source")
        command.parser.addPositionalArgument("source", "system, microphone, demo, app:Spotify, or just Spotify.");
    command.process(arguments);
    static const QHash<QString, QString> methods = {{"next", "Next"}, {"previous", "Previous"},
                                                    {"random", "Random"}, {"preset", "Select"},
                                                    {"lock", "ToggleLock"}, {"current", "Current"},
                                                    {"mashup", "Mashup"}, {"source", "Source"}};
    if (word == "preset" && command.argument(0).isEmpty())
        return fail("Give preset a name; trance list shows them.");
    if (word == "source" && command.argument(0).isEmpty())
        return fail("Give source something to listen to; trance sources lists what there is.");
    QString request = methods.value(word);
    if (word == "preset" || word == "source")
        request += ' ' + command.argument(0);
    QString reply;
    if (!remote::send(request, &reply))
        return fail("Trance isn't running.");
    if (reply == "unsaved")
        return fail("Trance has unsaved changes to the preset it shows; save or discard them in the window first.");
    const bool done = reply == "ok" || reply.startsWith("ok ");
    if (word == "preset" && !done)
        return fail("No preset called '" + command.argument(0) + "'.");
    if (word == "mashup" && !done)
        return fail("No mash-up came together this time; try again.");
    if (word == "current")
        print(stdout, reply.mid(3));
    return 0;
}

// What there is to listen to, as the sound system reports it right now.
int sources(const QStringList &arguments) {
    Command command("sources", "List what Trance can listen to: everything, one application, or one device.");
    command.json();
    command.process(arguments);
    QList<AudioCapture::Node> nodes;
    QString error;
    if (!AudioCapture::listNodes(&nodes, &error))
        return fail(error);
    const QVariantList list = AudioCapture::describe(nodes, {});
    if (command.parser.isSet("json")) {
        print(QJsonObject{{"sources", QJsonArray::fromVariantList(list)}});
        return 0;
    }
    for (const auto &entry : list) {
        const QVariantMap source = entry.toMap();
        print(stdout, QString("%1  %2").arg(source.value("id").toString(), -34).arg(source.value("label").toString()));
    }
    return 0;
}

// As the hey and basecamp tools do: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/trance and link into ~/.claude/skills",
                                         "[install]");
    command.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(0).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(0) != "install")
        return fail("Use trance skill to print the skill, or trance skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/trance");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/trance");
#ifdef Q_OS_WIN
    // A link is not to be had for the asking on Windows, so Claude Code
    // gets a copy of its own.
    QSaveFile copy(link + "/SKILL.md");
    if (!QDir().mkpath(link) || !copy.open(QIODevice::WriteOnly) || copy.write(text) < 0 || !copy.commit())
        return fail("Could not write " + copy.fileName());
    print(stdout, "Installed " + copy.fileName());
    return 0;
#else
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/trance", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
#endif
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (topic == "format" || topic == "manual") {
        QFile guide(topic == "manual" ? ":/manual.md" : ":/format.md");
        if (!guide.open(QIODevice::ReadOnly))
            return 1;
        fputs(guide.readAll().constData(), stdout);
        return 0;
    }
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: trance <command> [options]\n\n" + cliSummary());
    return 0;
}

} // namespace

Exposure measureExposure(const QImage &image) {
    const QImage pixels = image.convertToFormat(QImage::Format_RGB32);
    Exposure result;
    if (pixels.isNull())
        return result;
    qint64 clipped = 0;
    double light = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            clipped += std::max({r, g, b}) >= 250;
            light += 0.299 * r + 0.587 * g + 0.114 * b;
        }
    }
    const double count = double(pixels.width()) * pixels.height();
    result.clipped = double(clipped) / count;
    result.mean = light / 255.0 / count;
    return result;
}

QString cliSummary() {
    return "Visualizer:\n"
           "  open [preset]                 Open the window, on the last preset by default\n\n"
           "Steering a running Trance (bind these to keys):\n"
           "  next | previous | random      Change preset\n"
           "  preset <name>                 Show the named preset\n"
           "  mashup                        Put a new preset together from parts of others\n"
           "  lock                          Hold the current preset, or let go of it\n"
           "  current                       Print the current preset's name\n"
           "  source <what>                 Listen to system, microphone, demo, or one app (source Spotify)\n\n"
           "Commands that need no display (trance help <command> for options):\n"
           "  list                          List presets\n"
           "  sources                       List what there is to listen to\n"
           "  show <preset>                 Print a preset's JSON\n"
           "  new <name> [--from preset]    Start a preset of your own\n"
           "  check <preset>...             Report every problem in a preset\n"
           "  render <preset>               Render a preset to a PNG, or every frame of it\n"
           "  help format                   The preset format, in brief\n"
           "  help manual                   How to make an effect: every setting and why\n"
           "  skill [install]               Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"list", "show", "new", "check", "render", "next", "previous", "random", "preset", "lock",
                       "current", "mashup", "source", "sources", "skill", "help", "--help", "-h"}
        .contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "list"     ? list(arguments)
           : command == "show"   ? show(arguments)
           : command == "new"    ? create(arguments)
           : command == "check"  ? check(arguments)
           : command == "render" ? render(arguments)
           : command == "skill"  ? skill(arguments)
           : command == "sources" ? sources(arguments)
           : QStringList{"next", "previous", "random", "preset", "lock", "current", "mashup", "source"}
                     .contains(command)
                 ? steer(arguments)
                 : help(arguments);
}
