#include "preset.h"

#include "audio.h"

#include <QColor>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

Field number(const char *key, double def, double min, double max) {
    return Field{key, Field::Number, def, min, max, {}, {}};
}
Field integer(const char *key, double def, double min, double max) {
    return Field{key, Field::Integer, def, min, max, {}, {}};
}
Field toggle(const char *key, bool def) { return Field{key, Field::Toggle, def ? 1.0 : 0.0, 0, 1, {}, {}}; }
Field held(Field field, double low, double high = std::numeric_limits<double>::infinity()) {
    field.heldLow = low;
    field.heldHigh = high;
    return field;
}
// How a layer's light meets what is under it. Each kind of layer has its
// own default; the choices are listed in the same order everywhere.
Field blend(const char *def) { return Field{"blend", Field::Choice, 0, 0, 0, def, {"over", "add", "max"}}; }
Field choice(const char *key, const QStringList &options) {
    return Field{key, Field::Choice, 0, 0, 0, options.first(), options};
}
Field color(const char *key, const char *def) { return Field{key, Field::Color, 0, 0, 0, def, {}}; }
Field code(const char *key) { return Field{key, Field::Code, 0, 0, 0, {}, {}}; }

const QHash<QString, QList<Field>> &allFields() {
    static const QHash<QString, QList<Field>> sections = {
        // How last frame's picture is bent before this frame is drawn on it.
        // Values are per frame at 60 fps; the engine scales them to the real
        // frame time, so a preset looks the same at 60 and 144 Hz.
        {"feedback", {
            held(number("decay", 0.96, 0.5, 1), 0, 1), held(number("zoom", 1, 0.8, 1.2), 0.01),
            held(number("zoomexp", 1, 0.25, 4), 0.01),
            number("rot", 0, -0.2, 0.2), number("warp", 0, 0, 5), held(number("warpScale", 1, 0.1, 5), 0.01),
            number("warpSpeed", 1, 0, 5), number("cx", 0, -1, 1), number("cy", 0, -1, 1),
            number("dx", 0, -0.05, 0.05), number("dy", 0, -0.05, 0.05), held(number("sx", 1, 0.8, 1.2), 0.01),
            held(number("sy", 1, 0.8, 1.2), 0.01), held(number("blur", 0, 0, 1), 0, 1),
            number("hueshift", 0, -0.2, 0.2),
            choice("edge", {"wrap", "mirror", "clamp", "black"}), toggle("darkenCenter", false),
        }},
        // What happens between the feedback buffer and the screen.
        {"post", {
            held(number("colorize", 0, 0, 1), 0, 1), number("colorShift", 0, -1, 1),
            held(number("colorCurve", 1, 0.2, 4), 0.05),
            held(number("bloom", 0.5, 0, 3), 0), number("bloomThreshold", 0.55, 0, 1.2),
            number("brightness", 1, 0, 3), number("contrast", 1, 0, 2), number("saturation", 1, 0, 2),
            held(number("gamma", 1, 0.3, 3), 0.05), number("hue", 0, -3.1416, 3.1416),
            held(number("invert", 0, 0, 1), 0, 1),
            choice("mirror", {"none", "x", "y", "quad"}), integer("kaleido", 0, 0, 16),
            number("kaleidoAngle", 0, -3.1416, 3.1416), held(number("echoAlpha", 0, 0, 1), 0, 1),
            held(number("echoZoom", 1, 0.25, 4), 0.05), choice("echoFlip", {"none", "x", "y", "xy"}),
            number("chroma", 0, 0, 3), held(number("vignette", 0.3, 0, 1.5), 0, 2), held(number("grain", 0, 0, 0.5), 0, 1),
            held(number("scanlines", 0, 0, 1), 0, 1),
        }},
        // What shows where the picture is dark. It is drawn fresh every
        // frame behind the feedback picture, so it never smears.
        {"background", {
            choice("style", {"none", "gradient", "nebula", "stars", "grid", "rays", "plasma"}),
            number("intensity", 0.4, 0, 2), number("tint", 0, 0, 1), number("spread", 0.3, 0, 1),
            held(number("scale", 1, 0.2, 5), 0.01), number("speed", 1, 0, 5),
        }},
        {"wave", {
            toggle("enabled", true), choice("source", {"mix", "left", "right", "spectrum"}),
            choice("shape", {"line", "circle", "xy", "custom"}), held(integer("samples", 256, 8, 512), 8, 512),
            number("scale", 0.4, 0, 2), held(number("smooth", 0.3, 0, 1), 0, 0.98), number("x", 0, -2, 2),
            number("y", 0, -1, 1),
            number("width", 1, 0, 2), number("radius", 0.5, 0, 1.5), number("angle", 0, -3.1416, 3.1416),
            number("thickness", 2.5, 0.5, 40), color("color", "accent"), held(number("alpha", 1, 0, 1), 0, 1),
            blend("add"), toggle("dots", false), choice("mirror", {"none", "x", "y", "quad"}),
            code("point"),
        }},
        {"bars", {
            toggle("enabled", true), choice("layout", {"bottom", "top", "center", "radial"}),
            held(integer("count", 48, 4, 128), 4, 128), number("x", 0, -2, 2), number("y", 0, -1, 1),
            number("width", 0.9, 0.1, 1),
            number("height", 0.8, 0, 2), held(number("gap", 0.25, 0, 0.9), 0, 0.95), number("radius", 0.35, 0, 1),
            number("angle", 0, -3.1416, 3.1416), color("color", "palette(t)"), held(number("alpha", 0.9, 0, 1), 0, 1),
            held(number("fade", 0.6, 0, 1), 0, 1), toggle("peaks", true), toggle("symmetric", false), blend("over"),
            code("point"),
        }},
        {"shape", {
            toggle("enabled", true), held(integer("sides", 4, 3, 64), 3, 64),
            held(integer("instances", 1, 1, 512), 1, 512),
            number("x", 0, -2, 2), number("y", 0, -1, 1), number("radius", 0.3, 0, 2),
            number("angle", 0, -3.1416, 3.1416), color("color", "accent"), held(number("alpha", 1, 0, 1), 0, 1),
            color("color2", "black"), held(number("alpha2", 0, 0, 1), 0, 1), color("border", "foreground"),
            held(number("borderAlpha", 0, 0, 1), 0, 1), number("borderWidth", 2, 0.5, 20), blend("over"),
            toggle("textured", false), number("texZoom", 1, 0.2, 5), number("texAngle", 0, -3.1416, 3.1416),
            code("code"),
        }},
        {"terrain", {
            toggle("enabled", true), choice("style", {"mesh", "lines", "dots"}),
            held(integer("rows", 36, 8, 64), 8, 64),
            held(integer("cols", 64, 16, 128), 16, 128), number("speed", 18, 1, 60), number("height", 0.55, 0, 2),
            number("width", 1.7, 0.2, 4), number("y", -0.75, -1.5, 1), number("horizon", 0.25, -1, 1),
            number("depth", 3, 0.2, 10), number("thickness", 1.6, 0.5, 10), color("color", "palette(t)"),
            held(number("alpha", 0.9, 0, 1), 0, 1), blend("add"),
        }},
        // SoundSpectrum's WhiteCap: the last few seconds of the spectrum as
        // a sheet of lines hung in space. It reads the spectrum its own way
        // (bins, low to fade), a form says where in space each point of the
        // sheet goes, and a camera looks at the result.
        {"wireframe", {
            toggle("enabled", true), choice("form", {"sheet", "tube", "ring", "sphere"}),
            choice("form2", {"sheet", "tube", "ring", "sphere"}), held(number("morph", 0, 0, 1), 0, 1),
            held(integer("bins", 48, 8, 128), 8, 128), held(integer("rows", 32, 4, 64), 4, 64),
            number("speed", 20, 1, 60),
            number("low", 40, 0, 4000), number("high", 5000, 200, 16000), number("tilt", 10, 0, 30),
            number("smooth", 0.35, 0, 0.95), number("fade", 0.35, 0.02, 3), number("height", 0.5, -2, 2),
            number("width", 1.2, 0.1, 4), number("depth", 1.2, 0.1, 4), number("radius", 0.6, 0.05, 3),
            number("yaw", 0, -3.1416, 3.1416), number("pitch", 0.5, -1.5708, 1.5708),
            number("roll", 0, -3.1416, 3.1416), number("distance", 3, 0.5, 20), number("size", 1, 0.1, 4),
            number("x", 0, -2, 2), number("y", 0, -1, 1), choice("style", {"mesh", "lines", "strands", "dots"}),
            number("thickness", 1.4, 0.5, 10), color("color", "palette(t)"), held(number("alpha", 0.9, 0, 1), 0, 1),
            held(number("fog", 0.6, 0, 1), 0, 1), blend("add"), code("point"),
        }},
        // MilkDrop's motion vectors: a grid of short strokes showing which
        // way the picture is flowing at each place.
        {"vectors", {
            toggle("enabled", true), held(integer("cols", 28, 2, 64), 2, 64), held(integer("rows", 16, 2, 48), 2, 48),
            number("length", 1, 0, 8), number("thickness", 1.5, 0.5, 12), color("color", "palette(t + i)"),
            held(number("alpha", 0.5, 0, 1), 0, 1), toggle("dots", false), blend("add"),
        }},
        {"particles", {
            toggle("enabled", true), held(integer("count", 600, 16, 4000), 16, 4000), number("rate", 120, 0, 2000),
            choice("emit", {"point", "wave", "bars", "edges", "screen"}),
            number("x", 0, -2, 2), number("y", 0, -1, 1), number("area", 0, 0, 2), number("speed", 0.6, 0, 4),
            number("angle", 0, -3.1416, 3.1416), number("spread", 6.2832, 0, 6.2832),
            number("gravity", 0, -3, 3), number("drag", 0.4, -3, 6), number("flow", 0, 0, 2),
            number("field", 0, 0, 4), number("fieldScale", 1.5, 0.2, 8), number("fieldSpeed", 0.2, 0, 3),
            number("pull", 0, -6, 6), number("swirl", 0, -6, 6), choice("edge", {"none", "wrap", "bounce"}),
            number("life", 2, 0.1, 10), number("size", 3, 0.5, 30), number("streak", 0, 0, 0.5),
            color("color", "palette(i)"), held(number("alpha", 1, 0, 1), 0, 1), blend("add"),
        }},
    };
    return sections;
}

const QHash<QString, QStringList> &palettes() {
    static const QHash<QString, QStringList> all = {
        // The first two follow the desktop theme live.
        {"theme", {"accent", "magenta", "red", "orange", "yellow", "green", "cyan", "blue"}},
        {"accent", {"darker_background", "muted", "accent", "bright_foreground"}},
        {"fire", {"#000000", "#5a0a00", "#d43a00", "#ff9a00", "#ffe66d", "#ffffff"}},
        {"ocean", {"#000814", "#001d3d", "#003566", "#0077b6", "#00b4d8", "#90e0ef", "#caf0f8"}},
        {"neon", {"#ff00a8", "#7a00ff", "#00c8ff", "#00ffa3", "#fff200", "#ff5e00"}},
        {"sunset", {"#0d0221", "#3d1a78", "#b5179e", "#f72585", "#ff8500", "#ffd60a"}},
        {"aurora", {"#001219", "#005f73", "#0a9396", "#94d2bd", "#e9d8a6", "#ee9b00", "#ca6702"}},
        {"vapor", {"#2b1055", "#7597de", "#ff71ce", "#01cdfe", "#05ffa1", "#b967ff", "#fffb96"}},
        {"acid", {"#0b0f00", "#3bff00", "#d0ff00", "#00ffc8", "#ff00e6"}},
        {"ice", {"#000000", "#0b132b", "#1c2541", "#3a506b", "#5bc0be", "#c9f9ff", "#ffffff"}},
        {"gold", {"#000000", "#3d2b00", "#b8860b", "#ffd60a", "#fff7cc"}},
        {"rainbow", {"#ff0040", "#ff8a00", "#ffe600", "#3dff00", "#00e5ff", "#2a5cff", "#b400ff"}},
        // Three from the plotting world, where a color map has to read
        // evenly from dark to bright: they suit colorize.
        {"magma", {"#000004", "#3b0f70", "#8c2981", "#de4968", "#fe9f6d", "#fcfdbf"}},
        {"inferno", {"#000004", "#420a68", "#932667", "#dd513a", "#fca50a", "#fcffa4"}},
        {"viridis", {"#440154", "#3b528b", "#21918c", "#5ec962", "#fde725"}},
        {"lagoon", {"#00141a", "#004e64", "#00a5cf", "#25a18e", "#7ae582", "#eafff5"}},
        {"forest", {"#06140a", "#1b4332", "#2d6a4f", "#52b788", "#b7e4c7", "#ffd166"}},
        {"dusk", {"#0b0c2a", "#2a1b5e", "#6b2d8a", "#c0478f", "#ff8e72", "#ffd9a0"}},
        {"arctic", {"#01060f", "#0a2540", "#1f6f8b", "#99d5c9", "#e8f7ff", "#b39ddb"}},
        {"amethyst", {"#0d0221", "#2e1065", "#6d28d9", "#a78bfa", "#f0abfc", "#fdf4ff"}},
        {"copper", {"#0a0503", "#4a1f0e", "#b5651d", "#e8a35c", "#ffe2b8", "#7fd1c7"}},
        {"cyber", {"#020a0a", "#003b46", "#07575b", "#00ff9c", "#f5d300", "#ff2a6d"}},
        {"phosphor", {"#000000", "#003b00", "#008f11", "#00ff41", "#d8ffd8"}},
        {"candy", {"#ff5d8f", "#ffb3c6", "#fff0a5", "#b8f2e6", "#aed9e0", "#c8a2ff"}},
        // Four more that rise evenly from black, in tones the rest leave
        // out: old film, cold metal, a blush, and the deep sea with a flare
        // of pink where it is brightest.
        {"sepia", {"#0b0704", "#2e1c10", "#5e3f25", "#93683f", "#c79f6e", "#eed9b0", "#fff8e7"}},
        {"steel", {"#030508", "#121a24", "#2c3c4e", "#55697f", "#8ea2b5", "#cdd9e3", "#ffffff"}},
        {"rose", {"#050003", "#2a0718", "#661537", "#b02d5c", "#e86d8e", "#ffb3c4", "#fff0f3"}},
        {"abyss", {"#000206", "#03122b", "#073d5c", "#00808a", "#1fc9b0", "#9effd8", "#ff7ad9"}},
        {"tropic", {"#ff5e5b", "#ff9f43", "#ffd166", "#06d6a0", "#00b4d8", "#3a86ff", "#c86bfa"}},
        // Three more from black: a thermal camera's ironbow, a plasma that
        // starts in deep indigo rather than black, and jade.
        {"thermal", {"#000000", "#1a0a4a", "#5a0f7a", "#a51e6b", "#e04a2a", "#f79a1a", "#ffe066", "#ffffff"}},
        {"nova", {"#0d0887", "#4c02a1", "#7e03a8", "#a92395", "#cc4778", "#e66c5c", "#f89540", "#fdc527",
                  "#f0f921"}},
        {"jade", {"#000503", "#01261a", "#045c3d", "#0f9465", "#4cc994", "#a8f0cc", "#f2fff8"}},
        // Two bright cycles: seventies earth and teal, and a carnival.
        {"retro", {"#e76f51", "#f4a261", "#e9c46a", "#8ab17d", "#2a9d8f", "#3d6f8e", "#b5557a"}},
        {"carnival", {"#7b2cbf", "#c77dff", "#ffd60a", "#f77f00", "#2dc653", "#00b4d8"}},
        // Every hue at about the same brightness, so a palette that keeps
        // turning changes color without flickering lighter and darker.
        {"phase", {"#b8860b", "#d2603f", "#d0477c", "#9c5ac8", "#4f83d1", "#24a3a0", "#62a83a"}},
        // Three more from black, each a single hue the rest pass through
        // on the way to another: blood red, sulfur, and cobalt blue.
        {"crimson", {"#050000", "#2c0204", "#6e060c", "#b10e16", "#e5281f", "#ff7a5c", "#ffe0d0"}},
        {"sulfur", {"#030400", "#1a2003", "#3e4d08", "#73860f", "#b4c41a", "#e6f04a", "#fbffcc"}},
        {"cobalt", {"#000105", "#020b2e", "#06216e", "#0a3fc2", "#2f74ff", "#8fb8ff", "#eef4ff"}},
        // Two bright ones: the Bauhaus's red, yellow, paper and blue, which
        // palette(0), (0.25), (0.5) and (0.75) give exactly, and citrus.
        {"bauhaus", {"#e63946", "#f4b400", "#f1ebe0", "#1d4ed8"}},
        {"citrus", {"#2fbf71", "#a7e22e", "#ffe83d", "#ffb627", "#ff7b1c"}},
        // Pale all the way round, pink to peach, mint, sky and lavender and
        // back to pink: the play of color in an opal, without a seam.
        {"opal", {"#f9c6d8", "#fde2b8", "#d4f1c5", "#b9e3f0", "#d3c4f5"}},
        // Pale, down through night and back up through dusk: its two ends
        // meet, so palette(t + time*0.1) turns without a seam anywhere.
        {"twilight", {"#e2d9e2", "#a8b8d2", "#6f7fc0", "#5a4aa4", "#3b1a5e", "#2a0f2c", "#6e2347", "#b0545a",
                      "#d99a86"}},
        // Three more from black. Cividis is the plotting world's map that
        // reads the same to eyes that confuse red and green; noir is black
        // and white film with a red flare where it is brightest; storm is
        // slate and gunmetal up to the violet white of lightning.
        {"cividis", {"#00224e", "#123570", "#3b496c", "#575d6d", "#707173", "#8a8678", "#a59c74", "#c3b369",
                     "#e1cc55", "#fee838"}},
        {"noir", {"#000000", "#141414", "#383838", "#6b6b6b", "#a6a6a6", "#e6e6e6", "#ff2b2b"}},
        {"storm", {"#020308", "#0e1424", "#23304d", "#3f4f7a", "#6b6fb8", "#b79cff", "#f4f0ff"}},
        // Two bright ones: the plotting world's spectral, softer than the
        // rainbow, and a coral reef's coral, sun, turquoise and violet.
        {"spectral", {"#9e0142", "#d53e4f", "#f46d43", "#fdae61", "#fee08b", "#e6f598", "#abdda4", "#66c2a5",
                      "#3288bd", "#5e4fa2"}},
        {"reef", {"#ff6f61", "#ffb347", "#ffe066", "#3ddbd9", "#0091ad", "#7b5cd6", "#e85d9f"}},
        // Bright and light all the way round, as a hologram's foil turns:
        // cyan, periwinkle, violet, pink, peach, lemon and mint, whose ends
        // meet, so a palette that keeps turning has no seam.
        {"hologram", {"#7ff3ff", "#8fa8ff", "#c77dff", "#ff7ad9", "#ffb38a", "#f5f08a", "#a6ffcf"}},
        // Three more from black: ultraviolet up to the green of a
        // highlighter under it, a cyanotype's Prussian blue up to the paper,
        // and the bark of a cherry tree up through blossom to petal white,
        // with the green of young leaves where it is brightest.
        {"blacklight", {"#000000", "#0c0221", "#2a0a5e", "#5b14b8", "#8f2cff", "#c86bff", "#39ff14", "#eaffd0"}},
        {"cyanotype", {"#02060d", "#061a33", "#0b3263", "#12508f", "#2f74a8", "#6d9fc2", "#b6cfdf", "#f4f1e6"}},
        {"sakura", {"#0a0507", "#2b1420", "#6b2f48", "#c25d82", "#f29bb8", "#ffd9e5", "#fff6f8", "#b9e08f"}},
        // Two bright ones: autumn leaves, from burgundy through rust,
        // pumpkin and mustard to moss and chestnut; and a risograph's
        // inks, fluorescent pink, orange, yellow, green, teal, blue and
        // purple.
        {"autumn", {"#8c1c13", "#bf4e1d", "#e8871e", "#edb92e", "#a3a83b", "#5f7a36", "#7a3b2e"}},
        {"riso", {"#ff48b0", "#ff6c2f", "#ffe800", "#00a95c", "#00838a", "#3255a4", "#765ba7"}},
        // The plotting world's better rainbow: dark blue up through cyan,
        // green and yellow, and down through orange to dark red, evenly
        // brighter toward the middle.
        {"turbo", {"#30123b", "#4145ab", "#4675ed", "#39a2fc", "#1bcfd4", "#24eca6", "#61fc6c", "#a4fc3b",
                   "#d1e834", "#f3c63a", "#fe9b2d", "#f36315", "#d93806", "#b11901", "#7a0402"}},
        // Two banded ones, dark and light in turn and bright at the end,
        // for colorize: a fading trail passes through every band, so the
        // whole screen is drawn in contours, as G-Force's ColorMaps do. A
        // contour map's night blues to gold and cream; ink and paper with
        // teal between.
        {"contour", {"#03040b", "#16306b", "#8fc9ff", "#0a1633", "#2f7fd0", "#f4fbff", "#1a2a55", "#ffd27a",
                     "#fffaf0"}},
        {"inkwell", {"#010205", "#0b2238", "#5fb3b3", "#071521", "#d9efe8", "#0e2a2f", "#f7f1df"}},
        // Midsummer light all the way round: coral, marigold, leaf, sky,
        // violet and rose, each about as bright as the next.
        {"solstice", {"#ff7a45", "#ffd166", "#7bd389", "#3aa7d8", "#7b5cd6", "#e0559a"}},
        // Lichen on stone, from the dark of the rock through moss to the
        // pale green of new growth.
        {"lichen", {"#050805", "#122515", "#25502a", "#4f8a3c", "#94c265", "#d7ecb0", "#fbfde9"}},
        // Three more from black: washi paper over indigo ink with a
        // vermilion seal where it is brightest; the deep sea up to the cyan
        // and green of things that glow in it; and candlelight, from the
        // dark of a nave through wine and amber to the flame.
        {"washi", {"#05070f", "#111c38", "#24407a", "#5a78b0", "#c9c0aa", "#f4ecdc", "#d4412b"}},
        {"biolume", {"#000308", "#001a2c", "#00395a", "#006f8e", "#00b3b8", "#3dffd0", "#d4fff4"}},
        {"vesper", {"#070302", "#2a0d0a", "#5c1a17", "#9c3b1a", "#d9782a", "#ffc05c", "#fff0c9"}},
        // Two bright ones: a peacock's teal, emerald, sapphire, violet and
        // gold, and a desert mesa's terracotta, sand, sage and dusk.
        {"peacock", {"#0b7a75", "#14a05a", "#1e5bb8", "#4a2f9a", "#8a2e8e", "#c9a227"}},
        {"mesa", {"#c1502e", "#e08a4c", "#efc78a", "#a7b48a", "#6d8c8a", "#7a5c86"}},
        // Banded, like contour, for colorize: a survey map's greens and
        // browns, dark and light in turn, cream at the top.
        {"topo", {"#030502", "#1d3b1a", "#a8d08d", "#0e1f0c", "#6b8f3a", "#f2e8c4", "#2a1e10", "#d9a55a",
                  "#fffbea"}},
        // Two colors only, as mono is, but a night violet into coral.
        {"duotone", {"#12062e", "#ff7a59"}},
        // Violet, which the rest pass through but seldom stay in: two from
        // black, heather into orchid and indigo into the glow of a black
        // light, and wisteria's lilacs bright all the way, with a leaf.
        {"orchid", {"#060208", "#22082a", "#4a1456", "#7d2a8c", "#b45cc4", "#e0a6ea", "#fbeefd"}},
        {"ultraviolet", {"#020008", "#0e0330", "#2a0a6e", "#5418b8", "#8a3cff", "#c79bff", "#f4ecff"}},
        {"wisteria", {"#7d6bc9", "#a48fe0", "#c9a7ec", "#e2c6f2", "#b58fd6", "#8fb573"}},
        // Two more from black: pencil lead up to warm paper, nearly without
        // color, and the green of absinthe up to its pale yellow when water
        // clouds it.
        {"graphite", {"#050505", "#17171a", "#2e2d30", "#4a4846", "#75716b", "#aaa59c", "#e8e3d8"}},
        {"absinthe", {"#020502", "#0b2410", "#1b5a22", "#3f9a2e", "#8cd13f", "#d4f07a", "#fafde0"}},
        {"mono", {"#000000", "#ffffff"}},
    };
    return all;
}

QString codeText(const QJsonValue &value) {
    if (value.isArray()) {
        QStringList lines;
        for (const auto &line : value.toArray())
            lines << line.toString();
        return lines.join('\n');
    }
    return value.toString();
}

// Splits "a, f(b, c), d" at the commas outside parentheses.
QStringList splitArguments(const QString &text) {
    QStringList parts;
    int depth = 0, start = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == '(')
            ++depth;
        else if (text[i] == ')')
            --depth;
        else if (text[i] == ',' && depth == 0) {
            parts << text.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    parts << text.mid(start).trimmed();
    return parts;
}

class Compiler {
public:
    explicit Compiler(CompiledPreset &preset) : p(preset) {}

    void problem(const QString &path, const QString &message, bool error = true) {
        p.problems.append(Problem{path, message, error});
    }

    expr::Program program(const QString &source, const QString &path) {
        expr::Program result;
        QString error;
        const QStringList before = p.vars.undefined();
        if (!result.parse(source, p.vars, &error))
            problem(path, error);
        // Remember where each not-yet-assigned name first shows up, to point
        // at the right field if nothing ever assigns it.
        for (const auto &name : p.vars.undefined())
            if (!before.contains(name) && !m_firstUse.contains(name))
                m_firstUse.insert(name, path);
        return result;
    }

    expr::Program numberValue(const QJsonValue &value, const Field &field, const QString &path) {
        if (value.isUndefined() || value.isNull())
            return expr::Program::constant(field.def);
        if (value.isDouble()) {
            // Past what the engine holds it to, a number draws as the end it
            // passed. Formulas are free to swing past it.
            const double number = value.toDouble();
            if (number < field.heldLow || number > field.heldHigh) {
                const double end = number < field.heldLow ? field.heldLow : field.heldHigh;
                problem(path, std::isinf(field.heldHigh)
                                  ? QString("Held to at least %1, so %2 draws as %1").arg(field.heldLow).arg(number)
                                  : QString("Held to %1 to %2, so %3 draws as %4")
                                        .arg(field.heldLow).arg(field.heldHigh).arg(number).arg(end),
                        false);
            }
            return expr::Program::constant(number);
        }
        if (value.isBool())
            return expr::Program::constant(value.toBool() ? 1 : 0);
        if (value.isString()) {
            if (value.toString().trimmed().isEmpty())
                return expr::Program::constant(field.def);
            expr::Program result = program(value.toString(), path);
            if (result.isEmpty())
                result = expr::Program::constant(field.def);
            return result;
        }
        problem(path, "Expected a number or a formula");
        return expr::Program::constant(field.def);
    }

    ColorSpec colorValue(const QString &source, const QString &path) {
        ColorSpec spec;
        const QString text = source.trimmed();
        static const QRegularExpression call("^(palette|hsv|rgb)\\s*\\((.*)\\)$",
                                             QRegularExpression::DotMatchesEverythingOption);
        const auto match = call.match(text);
        if (text.startsWith('#') || text == "white" || text == "black") {
            const QColor parsed(text);
            if (!parsed.isValid()) {
                problem(path, QString("'%1' is not a color; write #rrggbb").arg(text));
                return spec;
            }
            spec.rgb[0] = parsed.redF();
            spec.rgb[1] = parsed.greenF();
            spec.rgb[2] = parsed.blueF();
        } else if (match.hasMatch()) {
            const QString kind = match.captured(1);
            const QStringList arguments = splitArguments(match.captured(2));
            const int wanted = kind == "palette" ? 1 : 3;
            if (arguments.size() != wanted || arguments.contains(QString())) {
                problem(path, QString("%1() takes %2 value(s)").arg(kind).arg(wanted));
                return spec;
            }
            spec.kind = kind == "palette" ? ColorSpec::Palette : kind == "hsv" ? ColorSpec::Hsv : ColorSpec::Rgb;
            spec.a = program(arguments[0], path);
            if (wanted == 3) {
                spec.b = program(arguments[1], path);
                spec.c = program(arguments[2], path);
            }
            if (spec.a.isEmpty() || (wanted == 3 && (spec.b.isEmpty() || spec.c.isEmpty())))
                spec.kind = ColorSpec::Constant;
        } else if (presets::themeColorNames().contains(text)) {
            spec.kind = ColorSpec::Theme;
            spec.theme = text;
        } else {
            problem(path, QString("Unknown color '%1'. Use #rrggbb, a theme color such as accent, "
                                  "palette(t), hsv(h, s, v) or rgb(r, g, b)").arg(text));
        }
        return spec;
    }

    Block block(const QJsonObject &object, const QString &section, const QString &path,
                const QStringList &alsoAllowed = {}) {
        Block result;
        const auto &fields = presets::fields(section);
        QStringList known = alsoAllowed;
        for (const Field &field : fields) {
            const QString key = QString::fromLatin1(field.key);
            known << key;
            const QJsonValue value = object.value(key);
            const QString fieldPath = path + "." + key;
            switch (field.type) {
            case Field::Number:
            case Field::Integer:
            case Field::Toggle:
                result.numbers.insert(field.key, numberValue(value, field, fieldPath));
                break;
            case Field::Choice: {
                QString picked = value.isUndefined() ? field.text : value.toString();
                if (!field.options.contains(picked)) {
                    problem(fieldPath, QString("'%1' is not one of: %2").arg(picked, field.options.join(", ")));
                    picked = field.text;
                }
                result.choices.insert(field.key, picked);
                break;
            }
            case Field::Color:
                if (!value.isUndefined() && !value.isString())
                    problem(fieldPath, "Expected a color as text");
                result.colors.insert(field.key, colorValue(value.isString() ? value.toString() : field.text, fieldPath));
                break;
            case Field::Code: {
                const QString source = codeText(value);
                if (!source.trimmed().isEmpty())
                    result.code.insert(field.key, program(source, fieldPath));
                break;
            }
            }
        }
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!known.contains(it.key()))
                problem(path + "." + it.key(), QString("Unknown setting '%1'").arg(it.key()), false);
        return result;
    }

    // Reports names that are read but never assigned, once each.
    void reportUndefined() {
        for (const auto &name : p.vars.undefined()) {
            problem(m_firstUse.value(name), QString("Unknown variable '%1'").arg(name));
            p.vars.declare(name);
        }
    }

    CompiledPreset &p;

private:
    QHash<QString, QString> m_firstUse;
};

// While one preset hands over to the next, both are drawn every frame and
// this decides, pixel by pixel, how much of the new one shows. u_blend runs
// 0 to 1 over the change; the pattern says where the new picture arrives
// first. The feedback pass and the screen pass ask the same question, so
// the motion and the look change hands in the same places.
const char *const BlendFunction = R"(
uniform float u_blend;
uniform float u_pattern;
uniform float u_patternSeed;
float f_blendAt(vec2 uv) {
  if (u_blend >= 1.0) return 1.0;
  if (u_blend <= 0.0) return 0.0;
  if (u_pattern < 0.5) return u_blend;
  vec2 p = vec2((uv.x * 2.0 - 1.0) * u_aspect, uv.y * 2.0 - 1.0);
  float reach = length(vec2(u_aspect, 1.0));
  // g, from 0 to 1, is when a place changes over; soft is how much of the
  // change it takes there, so a pattern with edges of its own keeps them.
  float g, soft = 0.5;
  if (u_pattern < 1.5) {
    g = length(p) / reach;
  } else if (u_pattern < 2.5) {
    g = 1.0 - length(p) / reach;
  } else if (u_pattern < 3.5) {
    g = abs(fract(f_atan2(p.y, p.x) / 6.28318530718 + u_patternSeed) * 2.0 - 1.0);
  } else if (u_pattern < 4.5) {
    vec2 q = p * 1.3 + u_patternSeed * 37.0;
    g = f_noise(q) * 0.6 + f_noise(q * 2.3 + 11.0) * 0.4;
  } else if (u_pattern < 5.5) {
    float a = u_patternSeed * 6.28318530718;
    g = dot(p, vec2(cos(a), sin(a))) / reach * 0.5 + 0.5;
  } else if (u_pattern < 6.5) {
    // Blinds: eight slats at a slant, each opening from one side, the
    // first a little before the last.
    float a = u_patternSeed * 6.28318530718;
    float d = dot(p, vec2(cos(a), sin(a))) / reach * 0.5 + 0.5;
    g = fract(d * 8.0) * 0.8 + d * 0.2;
    soft = 0.2;
  } else if (u_pattern < 7.5) {
    // Tiles: square ones, turning over in no order.
    vec2 cell = floor(p * 4.0 + u_patternSeed * 13.0);
    g = f_hash(cell + vec2(u_patternSeed * 91.0, u_patternSeed * 37.0));
    soft = 0.15;
  } else if (u_pattern < 8.5) {
    // Spiral: two fronts setting off from one arm of a spiral and going
    // round it opposite ways, to meet on the far side.
    float turn = f_atan2(p.y, p.x) / 6.28318530718 + length(p) / reach * 1.5;
    g = abs(fract(turn + u_patternSeed) * 2.0 - 1.0);
    soft = 0.35;
  } else {
    // Shards: the picture breaks into cells of broken glass, which fall
    // away from the middle outward and in no order among neighbours.
    vec2 q = p * 2.5 + u_patternSeed * 17.0;
    vec2 base = floor(q), best = base;
    float nearest = 9.0;
    for (int j = -1; j <= 1; ++j)
      for (int i = -1; i <= 1; ++i) {
        vec2 cell = base + vec2(float(i), float(j));
        vec2 point = cell + vec2(f_hash(cell), f_hash(cell + 17.3));
        float d = dot(point - q, point - q);
        if (d < nearest) {
          nearest = d;
          best = cell;
        }
      }
    vec2 middle = (best + 0.5 - u_patternSeed * 17.0) / 2.5;
    g = clamp(f_hash(best + 5.1) * 0.65 + length(middle) / reach * 0.35, 0.0, 1.0);
    soft = 0.15;
  }
  float w = clamp((u_blend * (1.0 + soft) - g) / soft, 0.0, 1.0);
  return w * w * (3.0 - 2.0 * w);
}
)";

const char *const WarpTemplate = R"(
uniform sampler2D u_prev;
uniform sampler2D u_other;
uniform float V[%SIZE%];
uniform float u_aspect;
uniform float u_k;
uniform float u_warpTime;
uniform float u_seed;
uniform vec2 u_texel;
uniform float u_wide;
in vec2 v_uv;
out vec4 o_color;
%PRELUDE%
%BLEND%
vec3 hueRotate(vec3 c, float a) {
  const vec3 k = vec3(0.57735);
  float ca = cos(a), sa = sin(a);
  return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca);
}
void main() {
  float l_x = (v_uv.x * 2.0 - 1.0) * u_aspect;
  float l_y = v_uv.y * 2.0 - 1.0;
  float l_rad = length(vec2(l_x, l_y));
  float l_ang = f_atan2(l_y, l_x);
%CODE%
  float zoom = %zoom%, zoomexp = %zoomexp%, rot = %rot%, warp = %warp%;
  vec2 center = vec2(%cx%, %cy%);
  vec2 p = vec2(l_x, l_y);
  // u_k is the frame time in 60ths of a second, so motion per second stays
  // the same at any refresh rate.
  float z = pow(max(zoom, 0.01), pow(max(zoomexp, 0.01), l_rad * 2.0 - 1.0) * u_k);
  vec2 q = (p - center) / z;
  q /= pow(max(vec2(%sx%, %sy%), vec2(0.01)), vec2(u_k));
  float a = -rot * u_k;
  float ca = cos(a), sa = sin(a);
  q = vec2(q.x * ca - q.y * sa, q.x * sa + q.y * ca);
  q += center - vec2(%dx%, %dy%) * u_k;
  if (warp != 0.0) {
    float w = warp * u_k * 0.007;
    float t = u_warpTime;
    vec4 f = vec4(11.68 + 4.0 * cos(t * 1.413 + 10.0), 8.77 + 3.0 * cos(t * 1.113 + 7.0),
                  10.54 + 3.0 * cos(t * 1.233 + 3.0), 11.49 + 4.0 * cos(t * 0.933 + 5.0));
    vec2 m = p * 0.5 / max(%warpScale%, 0.01);
    q.x += w * sin(t * 0.333 + (m.x * f.x - m.y * f.w));
    q.y += w * cos(t * 0.375 - (m.x * f.z + m.y * f.y));
    q.x += w * cos(t * 0.753 - (m.x * f.y - m.y * f.z));
    q.y += w * sin(t * 0.825 + (m.x * f.x + m.y * f.w));
  }
  vec2 uv = vec2(q.x / u_aspect, q.y) * 0.5 + 0.5;
  vec3 c = texture(u_prev, uv).rgb;
  // Blur spreads the picture the way heat spreads, and what it spreads in a
  // second stays the same at any refresh rate and at any size of picture.
  // Mixing in a share b of neighbours r texels away spreads it by b*r*r,
  // and spreads add up from frame to frame. The measure is what blur
  // always did in a 1080-line picture at 60 frames a second: a share of
  // neighbours 1.5 pixels away. A spread too wide for one mix at 1.5
  // texels reaches further instead.
  float blur = clamp(%blur%, 0.0, 1.0);
  if (blur > 0.0) {
    float pixels = 1.5 / (1080.0 * u_texel.y);
    float spread = blur * u_k * pixels * pixels;
    float reach = max(1.5, sqrt(spread));
    vec2 d = u_texel * reach;
    vec3 around = texture(u_prev, uv + vec2(d.x, 0.0)).rgb + texture(u_prev, uv - vec2(d.x, 0.0)).rgb +
                  texture(u_prev, uv + vec2(0.0, d.y)).rgb + texture(u_prev, uv - vec2(0.0, d.y)).rgb;
    c = mix(c, around * 0.25, spread / (reach * reach));
  }
%EDGE%
  float hs = %hueshift%;
  if (hs != 0.0) c = max(hueRotate(c, hs * u_k), vec3(0.0));
  c *= pow(clamp(%decay%, 0.0, 1.0), u_k);
  // The small subtraction lets trails reach true black instead of leaving a
  // faint permanent stain.
  c = max(c - 0.0012 * u_k, vec3(0.0));
  // During a change of preset, u_other is the same frame bent the old way.
  float w = f_blendAt(v_uv);
  if (w < 1.0) c = mix(texture(u_other, v_uv).rgb, c, w);
  // Light that piles up is held by its brightest channel, so it keeps its
  // hue: held channel by channel, a pink that kept adding up reached the
  // ceiling in red, then blue, then green, and turned white.
  c *= min(1.0, 1.3 / max(max(c.r, max(c.g, c.b)), 1e-6));
  // The buffer keeps fewer digits than this. Some cards drop the rest,
  // which takes a little from every frame: the more frames a second, the
  // faster trails fade and the less blur spreads. Others round to the
  // nearest, which keeps a change smaller than half a step from ever
  // happening, so a slow fade stalls and leaves a stain. So the value is
  // rounded here, up or down at random in proportion to how near it is,
  // which on average keeps all of it. Half floats then hold it exactly;
  // 8 bits get it a quarter of a step up, which every card stores as the
  // step itself.
  vec3 quantum = u_wide > 0.5 ? exp2(floor(log2(max(c, vec3(6.103515625e-05)))) - 10.0) : vec3(1.0 / 255.0);
  c = floor(c / quantum + f_hash(gl_FragCoord.xy + u_seed * vec2(113.0, 59.0))) * quantum;
  if (u_wide < 0.5) c += 0.25 / 255.0;
  o_color = vec4(c, 1.0);
}
)";

const char *const CompositeTemplate = R"(
uniform sampler2D u_tex;
uniform sampler2D u_bloom;
uniform sampler2D u_palette;
uniform sampler2D u_snapshot;
uniform float V[%SIZE%];
uniform float u_aspect;
uniform float u_seed;
uniform float u_glass;
uniform float u_glassFloor;
uniform float u_paletteSpan;
uniform float u_bgTime;
in vec2 v_uv;
out vec4 o_color;
%PRELUDE%
%BLEND%
vec3 f_pal(float t) { return texture(u_palette, vec2(t, 0.5)).rgb; }
vec3 f_background(vec2 p) {
  float S = max(%bg_scale%, 0.01), T = %bg_tint%, W = %bg_spread%;
%BACKGROUND%
}
vec3 hueRotate(vec3 c, float a) {
  const vec3 k = vec3(0.57735);
  float ca = cos(a), sa = sin(a);
  return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca);
}
vec2 fold(vec2 uv) { return 1.0 - abs(1.0 - mod(uv, 2.0)); }
vec3 pick(vec2 uv) { return texture(u_tex, fold(uv)).rgb; }
void main() {
  vec2 screen = vec2((v_uv.x * 2.0 - 1.0) * u_aspect, v_uv.y * 2.0 - 1.0);
  vec2 p = screen;
%MIRROR%
  float segments = floor(%kaleido% + 0.5);
  if (segments >= 2.0) {
    float wedge = 6.28318530718 / segments;
    float an = f_atan2(p.y, p.x) + %kaleidoAngle%;
    an = abs(mod(an, wedge) - wedge * 0.5);
    p = length(p) * vec2(cos(an), sin(an));
  }
  vec2 uv = vec2(p.x / u_aspect, p.y) * 0.5 + 0.5;
  vec3 c;
  float chroma = %chroma%;
  if (chroma > 0.0) {
    vec2 d = (uv - 0.5) * chroma * 0.012;
    c = vec3(pick(uv + d).r, pick(uv).g, pick(uv - d).b);
  } else {
    c = pick(uv);
  }
  float echoAlpha = clamp(%echoAlpha%, 0.0, 1.0);
  if (echoAlpha > 0.0) {
    vec2 e = (uv - 0.5) / max(%echoZoom%, 0.05);
%ECHO%
    c = mix(c, pick(e + 0.5), echoAlpha);
  }
  c += texture(u_bloom, fold(uv)).rgb * max(%bloom%, 0.0);
  float colorize = clamp(%colorize%, 0.0, 1.0);
  if (colorize > 0.0) {
    float level = clamp(max(c.r, max(c.g, c.b)), 0.0, 1.0);
    // The curve bends which brightness lands on which color, and the shift
    // slides the whole palette along (it is a loop, so it can keep going).
    float place = pow(level, max(%colorCurve%, 0.05)) * u_paletteSpan + %colorShift%;
    vec3 mapped = f_pal(place) * smoothstep(0.0, 0.2, level);
    c = mix(c, mapped, colorize);
  }
  c = max((c - 0.5) * %contrast% + 0.5, vec3(0.0)) * %brightness%;
  c = mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, %saturation%);
  float hue = %hue%;
  if (hue != 0.0) c = hueRotate(c, hue);
  c = pow(max(c, vec3(0.0)), vec3(1.0 / max(%gamma%, 0.05)));
  c = mix(c, 1.0 - min(c, vec3(1.0)), clamp(%invert%, 0.0, 1.0));
  float l_x = screen.x, l_y = screen.y;
  float l_rad = length(screen);
  float l_ang = f_atan2(screen.y, screen.x);
  float l_r = c.r, l_g = c.g, l_b = c.b;
  float l_lum = dot(c, vec3(0.299, 0.587, 0.114));
%CODE%
  c = max(vec3(l_r, l_g, l_b), vec3(0.0));
  // A soft shoulder instead of a hard clip, on the brightest channel with
  // the others in step, so a bright core keeps its color.
  float top = max(c.r, max(c.g, c.b));
  if (top > 0.75) c *= (0.75 + 0.25 * tanh((top - 0.75) * 4.0)) / top;
  // The background fills what the picture leaves dark. Glass has the
  // desktop for a background instead.
  float behind = %bg_intensity% * (1.0 - u_glass);
  if (behind > 0.0)
    c += max(f_background(screen), vec3(0.0)) * behind * (1.0 - clamp(max(c.r, max(c.g, c.b)) * 1.5, 0.0, 1.0));
  c *= 1.0 - clamp(%vignette%, 0.0, 2.0) * smoothstep(0.55, 1.75, length(screen / vec2(max(u_aspect, 1.0), 1.0)) * 1.4);
  c *= 1.0 - clamp(%scanlines%, 0.0, 1.0) * 0.5 * (0.5 + 0.5 * sin(gl_FragCoord.y * 2.0943951));
  c += (f_hash(gl_FragCoord.xy + vec2(u_seed * 91.0, u_seed * 47.0)) - 0.5) * %grain%;
  c = clamp(c, 0.0, 1.0);
  // During a change of preset, u_snapshot is this frame as the old one shows it.
  float w = f_blendAt(v_uv);
  if (w < 1.0) c = mix(texture(u_snapshot, v_uv).rgb, c, w);
  float alpha = 1.0;
  if (u_glass > 0.0) {
    // Over the desktop: black is see-through, light is solid, and the
    // floor is how much of the black stays, from none of it to all.
    alpha = clamp(max(c.r, max(c.g, c.b)) * 1.6, 0.0, 1.0);
    c = min(c, vec3(alpha));
    alpha = max(alpha, clamp(u_glassFloor, 0.0, 1.0));
  }
  o_color = vec4(c, alpha);
}
)";

// The body of f_background(p) for each style. p is the place on screen, S
// the scale, T where on the palette it sits and W how much of the palette it
// spans; u_bgTime is the time, already multiplied by the style's speed.
QString backgroundCode(const QString &style) {
    if (style == "gradient")
        return R"(  float g = p.y * 0.5 + 0.5 + 0.12 * sin(p.x * 1.3 * S + u_bgTime * 0.3) + 0.08 * sin(u_bgTime * 0.17);
  return f_pal(T + W * g) * mix(0.25, 1.0, clamp(g, 0.0, 1.0));)";
    if (style == "nebula")
        return R"(  vec2 q = p * S * 0.9;
  float t = u_bgTime * 0.05;
  vec2 w = vec2(f_noise(q + vec2(t, -t) + 3.1), f_noise(q * 1.1 - vec2(t, t) + 7.7));
  float n = f_noise(q * 1.6 + w * 2.2 + t) * 0.6 + f_noise(q * 3.7 - w * 1.5) * 0.3 + f_noise(q * 8.0 + w) * 0.1;
  n = smoothstep(0.3, 0.85, n);
  return f_pal(T + W * (w.x + n * 0.5)) * n;)";
    if (style == "stars")
        return R"(  vec3 sum = vec3(0.0);
  for (int layer = 0; layer < 3; ++layer) {
    float depth = 1.0 + float(layer) * 0.8;
    vec2 q = p * S * 9.0 * depth + vec2(u_bgTime * 0.25 / depth, float(layer) * 17.0);
    vec2 cell = floor(q), f = fract(q) - 0.5;
    float h = f_hash(cell + float(layer) * 31.0);
    vec2 offset = vec2(f_hash(cell + 5.3), f_hash(cell + 9.1)) - 0.5;
    float d = length(f - offset * 0.7);
    float twinkle = 0.6 + 0.4 * sin(u_bgTime * (1.0 + h * 3.0) + h * 40.0);
    float star = step(0.82, h) * (1.0 - smoothstep(0.0, 0.09 / depth + 0.02, d)) * twinkle;
    sum += (f_pal(T + W * h) + 0.4) * star;
  }
  return sum;)";
    if (style == "grid")
        return R"(  float horizon = 0.05;
  vec3 col = f_pal(T + W) * exp(-abs(p.y - horizon) * 5.0) * 0.6;
  if (p.y < horizon) {
    float z = 1.0 / max(horizon - p.y, 0.002);
    vec2 g = vec2(p.x * z, z + u_bgTime * 1.5) * S * 0.6;
    vec2 a = abs(fract(g - 0.5) - 0.5) / max(fwidth(g), vec2(1e-4));
    col += f_pal(T) * (1.0 - min(min(a.x, a.y), 1.0)) * exp(-z * 0.08);
  }
  return col;)";
    if (style == "rays")
        return R"(  float a = f_atan2(p.y, p.x), r = length(p);
  float b = 0.5 + 0.25 * sin(a * floor(5.0 * S + 0.5) + u_bgTime * 0.2) +
            0.25 * sin(a * floor(9.0 * S + 0.5) - u_bgTime * 0.31 + 1.7);
  return f_pal(T + W * r) * b * b * b / (1.0 + r * r * 1.5);)";
    if (style == "plasma")
        return R"(  float t = u_bgTime * 0.4;
  float v = sin(p.x * 3.0 * S + t) + sin(p.y * 4.0 * S - t * 1.3) + sin((p.x + p.y) * 2.5 * S + t * 0.7) +
            sin(length(p * S) * 5.0 - t * 1.1);
  v = v * 0.125 + 0.5;
  return f_pal(T + W * v) * (0.3 + 0.7 * v);)";
    return "  return vec3(0.0);";
}

// Fills a template's %name% slots: fixed pieces first, then one per setting.
QString fill(QString text, const QHash<QString, QString> &pieces, const Block &block, const QString &prefix,
             const expr::GlslScope &scope, const expr::Vars &vars) {
    for (auto it = pieces.begin(); it != pieces.end(); ++it)
        text.replace("%" + it.key() + "%", it.value());
    for (auto it = block.numbers.begin(); it != block.numbers.end(); ++it)
        text.replace("%" + QString::fromLatin1(it.key()) + "%",
                     scope.ref(prefix + QString::fromLatin1(it.key()), vars));
    return text;
}

} // namespace

// -------------------------------------------------------------------- block

double Block::num(const char *key, std::vector<double> &values) const {
    const auto it = numbers.constFind(QByteArray::fromRawData(key, qstrlen(key)));
    return it == numbers.constEnd() ? 0.0 : it->run(values);
}

QString Block::choice(const char *key) const { return choices.value(QByteArray::fromRawData(key, qstrlen(key))); }

const ColorSpec *Block::color(const char *key) const {
    const auto it = colors.constFind(QByteArray::fromRawData(key, qstrlen(key)));
    return it == colors.constEnd() ? nullptr : &*it;
}

const expr::Program *Block::program(const char *key) const {
    const auto it = code.constFind(QByteArray::fromRawData(key, qstrlen(key)));
    return it == code.constEnd() || it->isEmpty() ? nullptr : &*it;
}

bool CompiledPreset::ok() const {
    for (const auto &problem : problems)
        if (problem.error)
            return false;
    return true;
}

QString CompiledPreset::firstError() const {
    for (const auto &problem : problems)
        if (problem.error)
            return problem.path.isEmpty() ? problem.message : problem.path + ": " + problem.message;
    return {};
}

// ------------------------------------------------------------------ presets

namespace presets {

const QList<Field> &fields(const QString &section) {
    static const QList<Field> none;
    const auto it = allFields().constFind(section);
    return it == allFields().constEnd() ? none : *it;
}

QStringList layerTypes() { return {"wave", "bars", "shape", "terrain", "wireframe", "particles", "vectors"}; }

QStringList transitionStyles() {
    return {"mixed", "dissolve", "center", "edges", "sweep", "clouds", "wipe", "blinds", "tiles", "spiral", "shards"};
}

QStringList paletteNames() {
    return {"theme",   "accent", "fire",   "ocean",    "neon",   "sunset", "aurora", "vapor",    "acid",
            "ice",     "gold",   "rainbow", "magma",   "inferno", "viridis", "lagoon", "forest",   "dusk",
            "arctic",  "amethyst", "copper", "cyber",  "phosphor", "candy",  "sepia",    "steel",  "rose",
            "abyss",   "tropic", "twilight", "thermal", "nova", "jade",    "retro",  "carnival", "phase",
            "crimson", "sulfur", "cobalt", "bauhaus", "citrus", "opal", "cividis", "noir", "storm",
            "spectral", "reef", "hologram", "blacklight", "cyanotype", "sakura", "autumn", "riso", "turbo",
            "contour", "inkwell", "solstice", "lichen", "washi", "biolume", "vesper", "peacock", "mesa",
            "topo", "duotone", "orchid", "ultraviolet", "wisteria", "graphite", "absinthe", "mono"};
}

QStringList paletteStops(const QString &name) { return palettes().value(name); }

QStringList themeColorNames() {
    return {"accent", "foreground", "background", "selection", "muted", "red", "orange", "yellow", "green",
            "cyan", "blue", "magenta", "brown", "bright_red", "bright_yellow", "bright_green", "bright_cyan",
            "bright_blue", "bright_magenta", "dark_background", "darker_background", "lighter_background",
            "dark_foreground", "light_foreground", "bright_foreground"};
}

QStringList variableNames() {
    QStringList names{"time", "dt", "frame", "fps", "bass", "mid", "treb", "vol", "bass_att", "mid_att", "treb_att",
                      "vol_att"};
    for (int band = 0; band < AudioFrame::Bands; ++band)
        names << bandInfo(band).name;
    for (int band = 0; band < AudioFrame::Bands; ++band)
        names << QString(bandInfo(band).name) + "_att";
    return names + QStringList{"beat", "beats", "aspect", "progress", "pi", "tau"};
}

ThemeColors defaultThemeColors() {
    // Tokyo Night, Omarchy's default, with the fallback accent.
    static const ThemeColors colors = {
        {"accent", "#FFD60A"}, {"foreground", "#a9b1d6"}, {"background", "#1a1b26"}, {"selection", "#292e42"},
        {"muted", "#414868"}, {"red", "#f7768e"}, {"orange", "#eb927b"}, {"yellow", "#e0af68"},
        {"green", "#9ece6a"}, {"cyan", "#449dab"}, {"blue", "#7aa2f7"}, {"magenta", "#ad8ee6"},
        {"brown", "#75493d"}, {"bright_red", "#ff7a93"}, {"bright_yellow", "#ff9e64"},
        {"bright_green", "#b9f27c"}, {"bright_cyan", "#0db9d7"}, {"bright_blue", "#7da6ff"},
        {"bright_magenta", "#bb9af7"}, {"dark_background", "#13141c"}, {"darker_background", "#0e0e14"},
        {"lighter_background", "#24283b"}, {"dark_foreground", "#565f89"}, {"light_foreground", "#b4bee6"},
        {"bright_foreground", "#c0caf5"},
    };
    return colors;
}

QColor resolveColor(const QString &stop, const ThemeColors &theme) {
    if (stop.startsWith('#') || stop == "white" || stop == "black")
        return QColor(stop);
    const auto it = theme.constFind(stop);
    if (it != theme.constEnd() && it->isValid())
        return *it;
    return defaultThemeColors().value(stop, QColor("#ffffff"));
}

QJsonObject starterLayer(const QString &type) {
    if (type == "wave")
        return {{"type", "wave"}, {"shape", "line"}, {"color", "palette(t)"}, {"thickness", 3}, {"scale", 0.5}};
    if (type == "bars")
        return {{"type", "bars"}, {"layout", "bottom"}, {"color", "palette(t)"}};
    if (type == "shape")
        return {{"type", "shape"}, {"sides", 6}, {"radius", "0.2 + 0.1*bass_att"}, {"angle", "time*0.3"},
                {"color", "palette(time*0.1)"}, {"alpha", 0.6}, {"borderAlpha", 1}};
    if (type == "terrain")
        return {{"type", "terrain"}, {"color", "palette(t)"}};
    if (type == "wireframe")
        return {{"type", "wireframe"}, {"yaw", "time*0.2"}, {"color", "palette(0.15 + 0.6*v + 0.2*i)"}};
    if (type == "particles")
        return {{"type", "particles"}, {"rate", "60 + 600*beat"}, {"color", "palette(i)"}, {"flow", 1}};
    if (type == "vectors")
        return {{"type", "vectors"}, {"color", "palette(t + i)"}};
    return {};
}

QJsonObject blank(const QString &name) {
    return {
        {"name", name},
        {"palette", "theme"},
        {"feedback", QJsonObject{{"decay", 0.95}, {"zoom", "1.01 + 0.02*bass_att"}}},
        {"layers", QJsonArray{starterLayer("wave")}},
        {"post", QJsonObject{{"bloom", 0.6}}},
    };
}

bool moves(const QJsonObject &json) {
    if (!codeText(json.value("pixel")).trimmed().isEmpty())
        return true;
    const QJsonObject feedback = json.value("feedback").toObject();
    static const QString section = "feedback";
    for (const Field &field : fields(section)) {
        static const QList<QByteArray> motion = {"zoom", "rot", "warp", "dx", "dy", "sx", "sy"};
        if (!motion.contains(field.key))
            continue;
        const QJsonValue value = feedback.value(QString::fromLatin1(field.key));
        // A formula counts: nobody writes one to say "stay still".
        if (value.isString() ? !value.toString().trimmed().isEmpty() : value.isDouble() && value.toDouble() != field.def)
            return true;
    }
    return false;
}

QJsonObject upgradeLayer(QJsonObject layer) {
    // Before blend, a layer was additive or not.
    const QJsonValue additive = layer.value("additive");
    if (additive.isBool() || additive.isDouble()) {
        if (!layer.contains("blend"))
            layer["blend"] = (additive.isBool() ? additive.toBool() : additive.toDouble() != 0) ? "add" : "over";
        layer.remove("additive");
    }
    return layer;
}

QJsonObject upgrade(QJsonObject json) {
    if (!json.value("layers").isArray())
        return json;
    QJsonArray layers = json.value("layers").toArray();
    for (auto it = layers.begin(); it != layers.end(); ++it)
        *it = upgradeLayer(it->toObject());
    json["layers"] = layers;
    return json;
}

const QList<std::pair<QString, QString>> &lightSettings() {
    static const QList<std::pair<QString, QString>> settings = {
        {"feedback", "decay"}, {"post", "bloom"}, {"post", "bloomThreshold"},
        {"post", "brightness"}, {"post", "gamma"}, {"post", "colorize"}, {"post", "colorCurve"},
        {"post", "colorShift"},
    };
    return settings;
}

bool draws(const QJsonObject &json) {
    for (const QJsonValue &layer : json.value("layers").toArray()) {
        const QJsonValue enabled = layer.toObject().value("enabled");
        const bool off = enabled.isBool() ? !enabled.toBool() : enabled.isDouble() && enabled.toDouble() == 0;
        if (!off)
            return true;
    }
    return false;
}

QStringList colorFamilies() { return {"red", "orange", "green", "blue", "violet", "many", "gray", "theme"}; }

// Each stop pulls toward its hue as hard as it is colorful (the gap between
// its brightest and darkest channel), so black and white pull not at all.
// Averaged round the color wheel, the pull says which hue leads, and its
// strength how much it leads: under half, the stops pull different ways.
QString driftPalette(const QString &current, const QString &family) {
    QStringList names = paletteNames();
    names.removeAll("mono");
    if (!family.isEmpty())
        names.removeIf([&](const QString &name) { return colorFamily(paletteStops(name)) != family; });
    names.removeAll(current);
    names << QString(""); // each preset's own
    return names.at(QRandomGenerator::global()->bounded(int(names.size())));
}

QString colorFamily(const QStringList &stops) {
    if (stops.isEmpty())
        return {};
    double x = 0, y = 0, weight = 0;
    for (const QString &stop : stops) {
        if (!stop.startsWith('#'))
            return "theme";
        const QColor color(stop);
        const double chroma = std::max({color.redF(), color.greenF(), color.blueF()}) -
                              std::min({color.redF(), color.greenF(), color.blueF()});
        if (chroma <= 0)
            continue;
        const double angle = color.hsvHueF() * 2 * M_PI;
        x += chroma * std::cos(angle);
        y += chroma * std::sin(angle);
        weight += chroma;
    }
    if (weight / stops.size() < 0.12)
        return "gray";
    if (std::hypot(x, y) / weight < 0.5)
        return "many";
    const double hue = std::fmod(std::atan2(y, x) * 180 / M_PI + 360, 360);
    return hue < 15 ? "red" : hue < 60 ? "orange" : hue < 165 ? "green" : hue < 250 ? "blue" : hue < 315 ? "violet" : "red";
}

// The same reading of `palette` as compile's.
QStringList presetPalette(const QJsonObject &json) {
    const QJsonValue palette = json.value("palette");
    QStringList stops;
    if (palette.isArray()) {
        for (const auto &stop : palette.toArray())
            stops << stop.toString().trimmed();
    } else if (palette.isString() && palette.toString().contains(QRegularExpression("[#,]"))) {
        for (const auto &stop : palette.toString().split(',', Qt::SkipEmptyParts))
            stops << stop.trimmed();
    } else {
        stops = paletteStops(palette.isUndefined() ? QString("theme") : palette.toString().trimmed());
    }
    return stops;
}

QStringList featureNames() { return layerTypes() << "ranges"; }

QStringList features(const QJsonObject &json) {
    QStringList found;
    const QJsonArray layers = json.value("layers").toArray();
    for (const QString &type : layerTypes())
        for (const auto &layer : layers)
            if (layer.toObject().value("type").toString() == type) {
                found << type;
                break;
            }

    // Whether any formula or code names one of the six ranges. The words in
    // the name, the description and comments don't count.
    static const QSet<QString> ranges = [] {
        QSet<QString> names;
        for (int band = 0; band < AudioFrame::Bands; ++band)
            names << bandInfo(band).name << QString(bandInfo(band).name) + "_att";
        return names;
    }();
    static const QRegularExpression word("[A-Za-z_][A-Za-z0-9_]*"), comment("//[^\n]*");
    std::function<bool(const QJsonValue &)> reads = [&](const QJsonValue &value) {
        if (value.isString()) {
            const QString text = value.toString().remove(comment);
            for (auto it = word.globalMatch(text); it.hasNext();)
                if (ranges.contains(it.next().captured()))
                    return true;
        } else if (value.isArray()) {
            for (const auto &item : value.toArray())
                if (reads(item))
                    return true;
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                if (reads(it.value()))
                    return true;
        }
        return false;
    };
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!QStringList{"name", "author", "description"}.contains(it.key()) && reads(it.value())) {
            found << "ranges";
            break;
        }
    return found;
}

QVariantMap schema() {
    auto describe = [](const QString &section) {
        QVariantList list;
        static const char *const types[] = {"number", "integer", "toggle", "choice", "color", "code"};
        for (const Field &field : fields(section)) {
            const QString key = QString::fromLatin1(field.key);
            const FieldDoc doc = fieldDoc(section, key);
            list << QVariantMap{{"key", key}, {"type", types[field.type]},
                                {"def", field.def}, {"min", field.min}, {"max", field.max},
                                {"text", field.text}, {"options", field.options},
                                {"label", doc.label}, {"help", doc.help}};
        }
        return list;
    };
    auto named = [](const QString &section, const QStringList &keys) {
        QVariantMap map;
        for (const auto &key : keys) {
            const FieldDoc doc = fieldDoc(section, key);
            map.insert(key, QVariantMap{{"label", doc.label}, {"help", doc.help}});
        }
        return map;
    };
    QVariantMap layers;
    for (const auto &type : layerTypes())
        layers.insert(type, describe(type));
    return {{"feedback", describe("feedback")}, {"post", describe("post")},
            {"background", describe("background")}, {"layers", layers},
            {"code", named("code", {"init", "frame", "pixel", "shade"})}, {"types", named("type", layerTypes())},
            {"transitions", transitionStyles()},
            {"layerTypes", layerTypes()}, {"palettes", paletteNames()}, {"themeColors", themeColorNames()},
            {"variables", variableNames()}, {"functions", expr::functionNames()}};
}

CompiledPreset compile(const QJsonObject &json) {
    CompiledPreset p;
    Compiler compiler(p);
    p.name = json.value("name").toString();

    // What the engine provides every frame...
    for (const auto &name : variableNames())
        p.vars.declare(name);
    p.vars.declare("pi", M_PI);
    p.vars.declare("tau", 2 * M_PI);
    // ...and what it hands a layer's own code for each point or instance.
    for (const char *name : {"t", "i", "v", "v2", "n", "count", "x", "y", "z", "r", "g", "b", "a", "radius",
                             "angle", "size", "r2", "g2", "b2", "a2"})
        p.vars.declare(name);
    // Feedback settings are variables, so the frame code can push them
    // around and the pixel code can bend them across the screen.
    const QList<Field> feedbackFields = fields("feedback"), postFields = fields("post"),
                       backgroundFields = fields("background");
    for (const Field &field : feedbackFields)
        if (field.type != Field::Choice)
            p.vars.declare(QString::fromLatin1(field.key), field.def);
    for (const Field &field : postFields)
        if (field.type != Field::Choice)
            p.vars.declare("post." + QString::fromLatin1(field.key), field.def);
    for (const Field &field : backgroundFields)
        if (field.type != Field::Choice)
            p.vars.declare("bg." + QString::fromLatin1(field.key), field.def);

    static const QStringList topLevel = {"name", "author", "description", "palette", "init", "frame", "pixel",
                                         "shade", "feedback", "background", "layers",
                                         "post"};
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!topLevel.contains(it.key()))
            compiler.problem(it.key(), QString("Unknown setting '%1'").arg(it.key()), false);
    if (p.name.trimmed().isEmpty())
        compiler.problem("name", "A preset needs a name");

    // Palette: a named one, or a list of stops.
    const QJsonValue palette = json.value("palette");
    QStringList stops;
    if (palette.isArray()) {
        for (const auto &stop : palette.toArray())
            stops << stop.toString().trimmed();
    } else if (palette.isString() && palette.toString().contains(QRegularExpression("[#,]"))) {
        for (const auto &stop : palette.toString().split(',', Qt::SkipEmptyParts))
            stops << stop.trimmed();
    } else {
        const QString name = palette.isUndefined() ? QString("theme") : palette.toString();
        stops = paletteStops(name);
        if (stops.isEmpty()) {
            compiler.problem("palette", QString("Unknown palette '%1'. Use one of: %2, or a list of colors")
                                            .arg(name, paletteNames().join(", ")));
            stops = paletteStops("theme");
        }
    }
    for (const auto &stop : stops) {
        const bool literal = stop.startsWith('#') ? QColor(stop).isValid() : (stop == "white" || stop == "black");
        if (!literal && !themeColorNames().contains(stop)) {
            compiler.problem("palette", QString("'%1' is not a color; write #rrggbb or a theme color").arg(stop));
            stops = paletteStops("theme");
            break;
        }
    }
    if (stops.size() == 1)
        stops << stops.first();
    p.palette = stops;

    p.feedback = compiler.block(json.value("feedback").toObject(), "feedback", "feedback");
    p.post = compiler.block(json.value("post").toObject(), "post", "post");
    if (json.contains("background") && !json.value("background").isObject())
        compiler.problem("background", "Expected settings in { }");
    p.background = compiler.block(json.value("background").toObject(), "background", "background");
    p.initText = codeText(json.value("init"));
    if (json.contains("init"))
        p.init = compiler.program(p.initText, "init");
    if (json.contains("frame"))
        p.frame = compiler.program(codeText(json.value("frame")), "frame");

    if (json.contains("layers") && !json.value("layers").isArray())
        compiler.problem("layers", "Expected a list of layers");
    const QJsonArray layers = json.value("layers").toArray();
    for (int i = 0; i < layers.size(); ++i) {
        const QString path = QString("layers.%1").arg(i);
        const QJsonObject object = upgradeLayer(layers[i].toObject());
        const QString type = object.value("type").toString();
        if (!layerTypes().contains(type)) {
            compiler.problem(path + ".type", QString("'%1' is not a layer type; use one of: %2")
                                                 .arg(type, layerTypes().join(", ")));
            continue;
        }
        if (object.contains("additive"))
            compiler.problem(path + ".additive", "additive is now blend, which is one of: over, add, max, and takes "
                                                 "no formula", false);
        Block layer = compiler.block(object, type, path, {"type", "name", "additive"});
        layer.type = type;
        // Outside a shape's copies, only the color and the point code are
        // worked out point by point; the engine sets these names to 0 for
        // everything else, which is never what was meant.
        if (type != "shape") {
            const int own[] = {p.vars.find("t"), p.vars.find("i"), p.vars.find("v"), p.vars.find("v2")};
            for (const Field &field : fields(type)) {
                const auto found = layer.numbers.constFind(field.key);
                if (found == layer.numbers.constEnd())
                    continue;
                const QStringList names = [&] {
                    QStringList read;
                    for (int slot : own)
                        if (found->reads(slot))
                            read << p.vars.name(slot);
                    return read;
                }();
                if (!names.isEmpty())
                    compiler.problem(path + "." + QString::fromLatin1(field.key),
                                     QString("'%1' %2 always 0 here: only the color and the point code are "
                                             "worked out point by point")
                                         .arg(names.join("', '"), names.size() > 1 ? "are" : "is"),
                                     false);
            }
        }
        p.layers.append(layer);
    }
    if (p.layers.size() > 24)
        compiler.problem("layers", "A preset can have at most 24 layers");

    // Everything that runs on the CPU is parsed, so a name still unassigned
    // is a mistake. rad, ang and lum only exist per pixel: they are declared
    // after this check, so a frame formula that reads them is still caught.
    compiler.reportUndefined();
    for (const char *name : {"rad", "ang", "lum"})
        p.vars.declare(name);

    // The two generated shaders. Pixel code sees x, y, rad and ang as plain
    // locals; shade code sees those plus the color it may change.
    expr::GlslScope warpScope;
    for (const char *name : {"x", "y", "rad", "ang"})
        warpScope.predeclare(name);
    const expr::Program pixel = json.contains("pixel") ? compiler.program(codeText(json.value("pixel")), "pixel")
                                                       : expr::Program();
    p.pixel = pixel;
    expr::GlslScope shadeScope;
    for (const char *name : {"x", "y", "rad", "ang", "r", "g", "b", "lum"})
        shadeScope.predeclare(name);
    const expr::Program shade = json.contains("shade") ? compiler.program(codeText(json.value("shade")), "shade")
                                                       : expr::Program();

    compiler.reportUndefined();

    if (p.vars.size() > 480)
        compiler.problem("", "Too many variables");

    const QString size = QString::number(qMax(1, p.vars.size()));
    const QString edge = p.feedback.choice("edge") == "black"
                             ? "  c *= step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);\n"
                             : QString();
    const QString warpCode = pixel.glsl(warpScope, p.vars);
    p.warpShader = fill(WarpTemplate, {{"SIZE", size}, {"PRELUDE", expr::glslPrelude()}, {"BLEND", BlendFunction},
                                       {"CODE", warpCode}, {"EDGE", edge}},
                        p.feedback, "", warpScope, p.vars);

    const QString mirror = p.post.choice("mirror");
    const QString mirrorCode = mirror == "x" ? "  p.x = -abs(p.x);\n"
                               : mirror == "y" ? "  p.y = -abs(p.y);\n"
                               : mirror == "quad" ? "  p = -abs(p);\n" : QString();
    const QString flip = p.post.choice("echoFlip");
    const QString echoCode = flip == "x" ? "    e.x = -e.x;\n"
                             : flip == "y" ? "    e.y = -e.y;\n"
                             : flip == "xy" ? "    e = -e;\n" : QString();
    const QString shadeCode = shade.glsl(shadeScope, p.vars);
    // The post settings live in the variable table under private names, so
    // the shader reads them from V like everything else.
    p.compositeShader = fill(CompositeTemplate, {{"SIZE", size}, {"PRELUDE", expr::glslPrelude()},
                                                 {"BLEND", BlendFunction}, {"CODE", shadeCode},
                                                 {"MIRROR", mirrorCode}, {"ECHO", echoCode},
                                                 {"BACKGROUND", backgroundCode(p.background.choice("style"))}},
                             p.post, "post.", expr::GlslScope(), p.vars);
    for (auto it = p.background.numbers.begin(); it != p.background.numbers.end(); ++it)
        p.compositeShader.replace("%bg_" + QString::fromLatin1(it.key()) + "%",
                                  expr::GlslScope().ref("bg." + QString::fromLatin1(it.key()), p.vars));
    return p;
}

} // namespace presets
