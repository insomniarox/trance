#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "expr.h"

// A preset is a JSON document. Every number in it may instead be a formula
// in the expression language, so anything can follow the music. Compiling
// one needs no display: it yields the programs, the generated shaders, and a
// list of everything that's wrong with it.

using ThemeColors = QHash<QString, QColor>;

struct Problem {
    QString path;    // "feedback.zoom", "layers.2.color", or empty for the preset as a whole
    QString message;
    bool error = true;
};

struct Field {
    enum Type { Number, Integer, Toggle, Choice, Color, Code };
    QByteArray key;
    Type type = Number;
    double def = 0, min = 0, max = 1;
    QString text;        // default for Choice and Color
    QStringList options; // Choice
};

struct ColorSpec {
    enum Kind { Constant, Theme, Palette, Hsv, Rgb } kind = Constant;
    float rgb[3] = {1, 1, 1};
    QString theme;
    expr::Program a, b, c;
};

// One compiled section: the feedback settings, the post settings, or a layer.
struct Block {
    QString type;
    QHash<QByteArray, expr::Program> numbers;
    QHash<QByteArray, QString> choices;
    QHash<QByteArray, ColorSpec> colors;
    QHash<QByteArray, expr::Program> code;

    double num(const char *key, std::vector<double> &values) const;
    bool flag(const char *key, std::vector<double> &values) const { return num(key, values) != 0; }
    QString choice(const char *key) const;
    const ColorSpec *color(const char *key) const;
    const expr::Program *program(const char *key) const;
};

struct CompiledPreset {
    QString name;
    expr::Vars vars;
    expr::Program init, frame;
    expr::Program pixel;       // the per-pixel code again, to ask on the CPU where the picture is flowing
    QString initText;          // to tell whether an edit changed the starting values
    Block feedback, post, background;
    QList<Block> layers;
    QStringList palette;       // color stops: #rrggbb or theme color names
    QString warpShader;        // fragment shaders, without the #version line
    QString compositeShader;
    QList<Problem> problems;

    bool ok() const;
    QString firstError() const;
};

// What a setting is called in the editor, and what it does.
struct FieldDoc {
    QString label, help;
};

namespace presets {

CompiledPreset compile(const QJsonObject &json);

// The name and explanation of a setting. The section is feedback, post,
// background or a kind of layer; "code" has the four code blocks and "type"
// the kinds of layer themselves.
FieldDoc fieldDoc(const QString &section, const QString &key);

// The format, for validation, defaults, and the editor.
const QList<Field> &fields(const QString &section); // feedback, post, or a layer type
QStringList layerTypes();
QStringList transitionStyles(); // "mixed" first, then the patterns in the engine's order
QStringList paletteNames();
QStringList paletteStops(const QString &name);
// Where colors drifting on their own go next from the palette they are on:
// any palette but mono and that one, or "" for each preset's own. Given a
// family of colors, only that family's palettes.
QString driftPalette(const QString &current, const QString &family = QString());
QStringList themeColorNames();
QStringList variableNames();
QVariantMap schema();
QJsonObject starterLayer(const QString &type);
// Whether the preset's feedback carries the picture anywhere: a zoom, a
// turn, a slide, a stretch, a warp, or pixel code. Without any of these
// whatever is drawn stays where it was put and only fades.
bool moves(const QJsonObject &json);
QJsonObject blank(const QString &name);

// What the browser sorts presets by. Both come from the preset itself, so
// a new palette or a new preset is sorted as soon as it is there.
// The family of colors stops lean to: "theme" when they follow the desktop,
// "gray" when there is hardly any color, "many" when no one hue leads, and
// otherwise the hue that does: red (pinks too), orange (and yellow), green,
// blue (and teal) or violet. Empty for no stops.
QString colorFamily(const QStringList &stops);
QStringList colorFamilies();
// A preset's palette as stops; the theme's when it names none.
QStringList presetPalette(const QJsonObject &json);
// Each kind of layer the preset has, in layerTypes() order, then "ranges"
// when any of its formulas or code reads one of the six narrow ranges.
QStringList features(const QJsonObject &json);
QStringList featureNames();

// Colors Omarchy themes define, with stand-ins for when there is no theme.
ThemeColors defaultThemeColors();
QColor resolveColor(const QString &stop, const ThemeColors &theme);

} // namespace presets
