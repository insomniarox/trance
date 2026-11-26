// What every setting of a preset is called and what it does, in plain
// words. The editor shows the names and puts the explanations in its
// tooltips; docs/manual.md says the same at more length. The keys on the
// left are the names in the file, which never change.

#include "preset.h"

#include <QHash>

namespace {

struct Entry {
    const char *label;
    const char *help;
};

// Keyed "section.key". A layer setting that means the same in every kind
// of layer is under "layer.key" and found when the kind has none of its own.
const QHash<QString, Entry> &entries() {
    static const QHash<QString, Entry> all = {
        // ------------------------------------------------------- feedback
        {"feedback.decay", {"Decay", "How much of the last frame is still there in the next. 1 never fades; 0.9 is gone "
                                     "in under a second. Long trails want it high, a crisp picture wants it low."}},
        {"feedback.zoom", {"Zoom", "Grows the last frame a little every frame (above 1) or shrinks it (below 1). Above "
                                   "1 everything streams outward like a tunnel; below 1 it falls into the middle."}},
        {"feedback.zoomexp", {"Zoom curve", "Bends the zoom across the screen. Above 1 the edges zoom more than the "
                                            "middle; below 1 the middle more than the edges."}},
        {"feedback.rot", {"Rotation", "Turns the last frame by this many radians every frame. A little of it with a "
                                      "zoom makes a spiral."}},
        {"feedback.warp", {"Warp", "A slow liquid wobble laid over the whole picture. 0 is none, 1 is easy to see."}},
        {"feedback.warpScale", {"Warp size", "How broad the wobble's swells are. Higher is broader and gentler."}},
        {"feedback.warpSpeed", {"Warp speed", "How fast the wobble moves."}},
        {"feedback.cx", {"Center X", "The point the zoom and the rotation work around, left to right. 0 is the middle "
                                     "of the screen."}},
        {"feedback.cy", {"Center Y", "The point the zoom and the rotation work around, bottom to top. 0 is the middle "
                                     "of the screen."}},
        {"feedback.dx", {"Slide X", "Slides the last frame sideways every frame. Positive drifts right."}},
        {"feedback.dy", {"Slide Y", "Slides the last frame up or down every frame. Positive drifts up."}},
        {"feedback.sx", {"Stretch X", "Stretches the last frame wider every frame (above 1) or squeezes it (below 1)."}},
        {"feedback.sy", {"Stretch Y", "Stretches the last frame taller every frame (above 1) or squeezes it (below 1)."}},
        {"feedback.blur", {"Blur", "Softens the last frame a little every frame, so trails turn to smoke instead of "
                                   "staying sharp."}},
        {"feedback.hueshift", {"Hue drift", "Turns the trail's colors a little every frame, so the older a part of the "
                                            "picture is, the further round the color wheel it has gone."}},
        {"feedback.edge", {"Edge", "What the flow finds when it reaches past the screen: wrap (the opposite side), "
                                   "mirror (a reflection), clamp (the edge's own color, stretched) or black (nothing)."}},
        {"feedback.darkenCenter", {"Darken center", "Paints a soft dark spot in the middle every frame. A zoom keeps "
                                                    "feeding the middle into itself, and this stops it burning out."}},

        // ----------------------------------------------------------- post
        {"post.colorize", {"Colorize", "Replaces the picture's colors with the palette, by brightness: dark parts take "
                                       "the palette's first colors, bright parts its last. 0 leaves the colors alone. "
                                       "Draw in white and let this do the coloring."}},
        {"post.colorShift", {"Palette shift", "Slides that coloring along the palette. The palette is a loop, so a value "
                                              "that keeps growing keeps the colors moving."}},
        {"post.colorCurve", {"Palette curve", "Bends the coloring. Above 1 holds on to the dark colors for longer; below "
                                              "1 reaches the bright ones sooner."}},
        {"post.bloom", {"Bloom", "Glow around the bright parts. 0 is none."}},
        {"post.bloomThreshold", {"Bloom threshold", "How bright something must be before it glows. Lower makes more of "
                                                    "the picture glow."}},
        {"post.brightness", {"Brightness", "Multiplies the whole picture. 1 leaves it alone."}},
        {"post.contrast", {"Contrast", "Pushes darks and lights apart (above 1) or together (below 1)."}},
        {"post.saturation", {"Saturation", "0 is black and white, 1 is as drawn, 2 is vivid."}},
        {"post.gamma", {"Gamma", "Lifts the middle tones (above 1) or sinks them (below 1) without moving black or "
                                 "white."}},
        {"post.hue", {"Hue", "Turns every color round the color wheel, in radians. 3.14 is the opposite color."}},
        {"post.invert", {"Invert", "Mixes in the negative of the picture. 1 is fully inverted."}},
        {"post.mirror", {"Mirror", "Reflects the finished picture: x makes the left match the right, y the top match "
                                   "the bottom, quad both."}},
        {"post.kaleido", {"Kaleidoscope", "Folds the picture into this many mirrored wedges around the middle. 0 is off."}},
        {"post.kaleidoAngle", {"Kaleidoscope angle", "Turns the wedges, in radians."}},
        {"post.echoAlpha", {"Echo", "Lays a second copy of the picture over the first. This is how strong the copy is."}},
        {"post.echoZoom", {"Echo zoom", "The size of the copy. Above 1 is larger than the picture."}},
        {"post.echoFlip", {"Echo flip", "Flips the copy: x, y, or xy for both."}},
        {"post.chroma", {"Color fringe", "Pulls red and blue apart toward the edges of the screen, like a cheap lens."}},
        {"post.vignette", {"Vignette", "Darkens the corners."}},
        {"post.grain", {"Grain", "Film grain."}},
        {"post.scanlines", {"Scanlines", "Dark horizontal lines, like an old screen."}},

        // ----------------------------------------------------- background
        {"background.style", {"Style", "What fills the dark: gradient (a soft wash), nebula (drifting clouds), stars, "
                                       "grid (a floor running to the horizon), rays (beams from the middle) or plasma "
                                       "(slow colored swells)."}},
        {"background.intensity", {"Brightness", "How bright the background is. It only shows where the picture is dark."}},
        {"background.tint", {"Palette start", "Where on the palette the background's colors begin, from 0 to 1."}},
        {"background.spread", {"Palette span", "How much of the palette it uses from there. Small keeps it to one color."}},
        {"background.scale", {"Detail", "How fine its features are. Higher packs more of them in."}},
        {"background.speed", {"Speed", "How fast it moves. 0 holds it still."}},

        // ----------------------------------------------- every kind of layer
        {"layer.enabled", {"Shown", "Turns the layer off without removing it."}},
        {"layer.blend", {"Blend", "How its light meets what is under it. over covers it; add adds to it, right for "
                                  "glow but too much burns out into flat color; max keeps the brighter of the "
                                  "two, so it glows but never grows past its own color."}},
        {"layer.alpha", {"Opacity", "How solid it is drawn. 0 is invisible."}},
        {"layer.color", {"Color", "#rrggbb, a theme color such as accent, or palette(formula) for a point on the "
                                  "palette. hsv(h, s, v) and rgb(r, g, b) take formulas too."}},
        {"layer.thickness", {"Thickness", "Line width, in pixels on a 1080-line screen."}},
        {"layer.x", {"X", "Left to right. 0 is the middle; the screen's edges are at about 1.78 on a wide screen."}},
        {"layer.y", {"Y", "Bottom to top. -1 is the bottom of the screen, 1 the top."}},
        {"layer.angle", {"Angle", "Turns it, in radians, counter-clockwise. 3.14 is half a turn."}},

        // ----------------------------------------------------------- wave
        {"wave.source", {"Source", "What the line shows: mix (both channels), left, right, or spectrum (loudness by "
                                   "pitch, bass to treble, instead of the waveform)."}},
        {"wave.shape", {"Shape", "line (across the screen), circle (wrapped into a ring), xy (left channel against "
                                 "right, a tangle that opens up with stereo) or custom (nothing until the point code "
                                 "places it)."}},
        {"wave.samples", {"Points", "How many points the line has. More is finer; few give a jagged line."}},
        {"wave.scale", {"Height", "How far the sound pushes the line away from where it rests."}},
        {"wave.smooth", {"Smoothing", "Rounds the line off. 0 is the raw sound."}},
        {"wave.width", {"Width", "How much of the screen's width a line spans. 1 is all of it."}},
        {"wave.radius", {"Radius", "The size of the ring when the shape is a circle."}},
        {"wave.dots", {"Dots", "Draws the points alone instead of joining them."}},
        {"wave.mirror", {"Mirror", "Draws reflected copies: x adds one left to right, y one top to bottom, quad all "
                                   "four."}},
        {"wave.color", {"Color", "A color, worked out for every point: t runs 0 to 1 along the line and v is the "
                                 "sound there, so palette(t) spreads the palette along it."}},
        {"wave.point", {"Point code", "Runs for every point. Reads t (0 to 1 along the line), i (the point's number), "
                                      "v (the sound there) and v2 (the other channel), and may change x, y, r, g, b "
                                      "and a."}},

        // ----------------------------------------------------------- bars
        {"bars.layout", {"Layout", "Where the bars stand: bottom, top (hanging down), center (growing both ways from "
                                   "the middle) or radial (around a circle)."}},
        {"bars.count", {"Bars", "How many bars the spectrum is split into."}},
        {"bars.width", {"Width", "How much of the screen's width the row takes."}},
        {"bars.height", {"Height", "How tall a bar at full strength is."}},
        {"bars.gap", {"Gap", "The space between bars, as a share of each bar's slot."}},
        {"bars.radius", {"Radius", "For the radial layout: the size of the circle the bars stand on."}},
        {"bars.fade", {"Foot fade", "How dark the foot of each bar is beside its tip."}},
        {"bars.peaks", {"Peak marks", "A small mark that stays at each bar's recent high and sinks slowly."}},
        {"bars.symmetric", {"Symmetric", "Bass in the middle and treble out to both sides, instead of bass on the left."}},
        {"bars.color", {"Color", "A color, worked out for every bar: t runs 0 to 1 across them and v is the bar's "
                                 "height."}},
        {"bars.point", {"Bar code", "Runs for every bar. Reads t (0 to 1 across), i (the bar's number) and v (its "
                                    "height, 0 to 1), and may change v, r, g, b and a."}},

        // ---------------------------------------------------------- shape
        {"shape.sides", {"Sides", "3 is a triangle, 4 a square; from about 32 it reads as a circle."}},
        {"shape.instances", {"Copies", "How many to draw. Every setting is worked out again for each copy, with n (0, "
                                       "1, 2 and so on), count, t (0 to 1 across the copies), v (the spectrum at t) "
                                       "and v2 (the waveform at t)."}},
        {"shape.radius", {"Size", "From the middle of the shape to its corners. 1 reaches the top of the screen."}},
        {"shape.color", {"Center color", "The fill at the middle of the shape."}},
        {"shape.alpha", {"Center opacity", "How solid the middle is."}},
        {"shape.color2", {"Rim color", "The fill at the corners. The fill runs smoothly from the center color to this."}},
        {"shape.alpha2", {"Rim opacity", "How solid the rim is. 0 with a solid center gives a soft glowing ball."}},
        {"shape.border", {"Outline color", "The color of the line round the shape."}},
        {"shape.borderAlpha", {"Outline opacity", "0 draws no outline. With both fills at 0 the shape is an outline "
                                                  "alone."}},
        {"shape.borderWidth", {"Outline width", "In pixels on a 1080-line screen."}},
        {"shape.textured", {"Show the picture", "Fills the shape with last frame's picture instead of a color, which "
                                                "makes it a window onto the feedback."}},
        {"shape.texZoom", {"Picture zoom", "How far into the picture that window looks. Above 1 is closer."}},
        {"shape.texAngle", {"Picture angle", "Turns the picture inside the shape."}},
        {"shape.code", {"Copy code", "Runs for every copy after its settings are worked out. May change x, y, radius, "
                                     "angle, the center's r g b a and the rim's r2 g2 b2 a2."}},

        // -------------------------------------------------------- terrain
        {"terrain.style", {"Style", "mesh (lines both ways), lines (rows only) or dots."}},
        {"terrain.rows", {"Rows", "How many rows of the past it keeps, from the front edge to the horizon."}},
        {"terrain.cols", {"Columns", "How many points across each row."}},
        {"terrain.speed", {"Speed", "Rows a second: how fast the landscape scrolls away."}},
        {"terrain.height", {"Height", "How tall the loudest peaks stand."}},
        {"terrain.width", {"Width", "Half the width of the front edge. 1.78 reaches both sides of a wide screen."}},
        {"terrain.y", {"Front edge", "Where the nearest row sits. -1 is the bottom of the screen."}},
        {"terrain.horizon", {"Horizon", "The height everything recedes toward."}},
        {"terrain.depth", {"Depth", "How strongly distance shrinks things. Higher puts the horizon further away."}},
        {"terrain.color", {"Color", "A color, worked out for every point: t runs 0 to 1 across, v is the height there "
                                    "and i the distance, 0 at the front to 1 at the horizon."}},

        // ------------------------------------------------------ wireframe
        {"wireframe.form", {"Form", "Where in space the sheet of samples is put: sheet (laid flat), tube (rolled up, "
                                    "time running down its length), ring (a disc, the newest sample at the hole in "
                                    "the middle) or sphere (wrapped round a ball, pole to pole)."}},
        {"wireframe.form2", {"Second form", "The form it turns into as Morph goes from 0 to 1."}},
        {"wireframe.morph", {"Morph", "How far it is from the form to the second form: 0 is all the first, 1 all the "
                                      "second. A slow formula here folds one into the other and back."}},
        {"wireframe.bins", {"Bins", "How many lines wide the sheet is. Its stretch of the sound is cut into that many "
                                    "bins of equal width, each the average of everything that falls in it."}},
        {"wireframe.rows", {"Samples kept", "How many moments of the past the sheet holds, newest first."}},
        {"wireframe.speed", {"Samples a second", "How often a new sample is taken, and so how fast the sheet moves "
                                                 "along: speed divided by the samples kept is lengths of the sheet "
                                                 "a second. count*0.3 keeps one pace whatever is kept."}},
        {"wireframe.low", {"Lowest pitch", "Where its stretch of the sound begins, in Hz. Raise it to leave the kick "
                                           "out."}},
        {"wireframe.high", {"Highest pitch", "Where its stretch of the sound ends, in Hz. A short stretch spreads a "
                                             "few notes across the whole sheet."}},
        {"wireframe.tilt", {"Treble weight", "How much more the top of the stretch counts than the bottom. Music is "
                                             "far louder low down, so at 0 the bass stands alone on a flat sheet."}},
        {"wireframe.smooth", {"Smoothing", "Rounds neighbouring bins into each other. 0 leaves every bin on its own."}},
        {"wireframe.fade", {"Fall time", "Seconds a bin takes to sink back after something sounds in it. It rises at "
                                         "once; a long fall lets held notes pile up into ridges."}},
        {"wireframe.height", {"Height", "How far the loudest bin stands out from the form. Negative pushes inward."}},
        {"wireframe.width", {"Width", "Half the width of the flat sheet."}},
        {"wireframe.depth", {"Length", "Half the length of the flat sheet or the tube, newest sample to oldest."}},
        {"wireframe.radius", {"Radius", "The size of the tube, the ring or the sphere."}},
        {"wireframe.yaw", {"Turn", "Turns it about its upright axis, in radians. time*0.2 keeps it turning."}},
        {"wireframe.pitch", {"Tip", "Tips it toward you, in radians. 0 looks at it from the side, 1.57 from "
                                    "straight above."}},
        {"wireframe.roll", {"Roll", "Turns the view itself, in radians, like tilting your head."}},
        {"wireframe.distance", {"Distance", "How far away the camera stands. Close makes the near side loom and the "
                                            "far side shrink; far away flattens it. Closer than the form's own "
                                            "size puts the camera inside it."}},
        {"wireframe.size", {"Size", "Scales the whole of it on screen."}},
        {"wireframe.style", {"Style", "mesh (lines both ways), lines (one for each sample), strands (one for each "
                                      "bin, running back through time) or dots."}},
        {"wireframe.fog", {"Fog", "How much the far side is dimmed, so near and far can be told apart. 0 draws all "
                                  "of it alike."}},
        {"wireframe.color", {"Color", "A color, worked out for every point: t runs 0 to 1 from the lowest pitch to "
                                      "the highest, i 0 to 1 from the newest sample to the oldest, v is the "
                                      "height there, and travel is where the sample lies on the ground."}},
        {"wireframe.point", {"Point code", "Runs for every point, after the form has placed it. Reads t, i, v and "
                                           "travel, and may change x, y and z (right, up and away, before the "
                                           "camera) and r, g, b and a. travel is how far along the ground the "
                                           "sample lies, in lengths of the sheet: it stays the same while the "
                                           "sample moves back, so hills drawn from it move with the rows."}},

        // -------------------------------------------------------- vectors
        {"vectors.cols", {"Columns", "How many strokes across the screen."}},
        {"vectors.rows", {"Rows", "How many strokes up the screen."}},
        {"vectors.length", {"Length", "How long a stroke is for a given speed of flow."}},
        {"vectors.dots", {"Dots only", "Marks each place with a dot instead of a stroke."}},
        {"vectors.color", {"Color", "A color, worked out for every stroke: t runs 0 to 1 across, i 0 to 1 up, and v "
                                    "is how fast the picture is moving there."}},

        // ------------------------------------------------------ particles
        {"particles.count", {"Most alive", "The most sparks alive at once. No more are born until some die."}},
        {"particles.rate", {"Birth rate", "Sparks born every second. 50 + 2000*beat bursts on the beat."}},
        {"particles.emit", {"Born at", "point (at X, Y), wave (along the waves drawn before this layer, mostly where "
                                       "they are loud, or along a wireframe's newest sample), bars (on the tips of the bars), edges (the screen's border, "
                                       "heading for X, Y) or screen (anywhere)."}},
        {"particles.x", {"X", "The birth point, and the point that pull and swirl work around."}},
        {"particles.y", {"Y", "The birth point, and the point that pull and swirl work around."}},
        {"particles.area", {"Birth area", "How far from the point a spark may be born."}},
        {"particles.speed", {"Speed", "How fast they set off, in screen-heights a second. Each gets a little more or "
                                      "less."}},
        {"particles.angle", {"Direction", "Which way they set off, in radians: 0 is right, 1.57 up, -1.57 down."}},
        {"particles.spread", {"Spread", "How wide a fan of directions around that. 6.28 is every way."}},
        {"particles.gravity", {"Gravity", "Pulls them down. Negative lets them rise."}},
        {"particles.drag", {"Drag", "Slows them as they go. Negative speeds them up."}},
        {"particles.flow", {"Ride the flow", "1 carries them exactly as the feedback carries the picture, so they follow "
                                             "its currents. 0 leaves them to themselves."}},
        {"particles.field", {"Currents", "The strength of a slowly changing field of currents of their own, which "
                                         "neighbouring sparks follow together."}},
        {"particles.fieldScale", {"Current detail", "How tight those currents' swirls are. Higher makes them smaller."}},
        {"particles.fieldSpeed", {"Current change", "How fast the currents shift."}},
        {"particles.pull", {"Pull", "Draws them toward X, Y. Negative pushes them away; -3*beat scatters them on the "
                                    "beat."}},
        {"particles.swirl", {"Swirl", "Swings them around X, Y. The sign sets which way."}},
        {"particles.edge", {"At the border", "none (they fly off), wrap (they come back on the other side) or bounce."}},
        {"particles.life", {"Lifetime", "Seconds a spark lives. Each gets a little more or less, and fades as it ages."}},
        {"particles.size", {"Size", "In pixels on a 1080-line screen."}},
        {"particles.streak", {"Streak", "Draws each as a line this many seconds long, back the way it came. 0 draws "
                                        "dots."}},
        {"particles.color", {"Color", "A color, worked out for every spark: t is its age from 0 to 1, i a random number "
                                      "it keeps for life, v its speed."}},

        // ------------------------------------------------- the four blocks
        {"code.init", {"init", "Runs once, when the preset starts. Give your own variables their starting values here; "
                               "roll them with rand() and the preset is a little different every time."}},
        {"code.frame", {"frame", "Runs every frame, before anything is drawn. Work out your own variables here; it may "
                                 "also set feedback settings such as zoom."}},
        {"code.pixel", {"pixel", "Runs for every pixel of the feedback step. Reads x, y, rad (distance from the middle) "
                                 "and ang (angle), and may change zoom, rot, dx, dy, sx, sy, cx, cy, warp and decay "
                                 "there, so the flow is different in different places."}},
        {"code.shade", {"shade", "Runs for every pixel on the way to the screen. Reads x, y, rad, ang and lum "
                                 "(brightness), and may change r, g and b."}},

        // --------------------------------------------------- kinds of layer
        {"type.wave", {"Wave", "The sound as a line: across the screen, round a ring, or wherever point code puts it."}},
        {"type.bars", {"Bars", "The spectrum as bars, bass to treble."}},
        {"type.shape", {"Shape", "A polygon or many copies of one, each of which can follow its own slice of the sound."}},
        {"type.terrain", {"Terrain", "The spectrum as a landscape scrolling away to a horizon."}},
        {"type.wireframe", {"Wireframe", "The spectrum's last few seconds as a sheet of lines in space, flat or rolled "
                                         "into a form, with a camera turning round it."}},
        {"type.vectors", {"Vectors", "A grid of strokes showing which way the picture is flowing."}},
        {"type.particles", {"Particles", "Sparks that can be born on a wave or the bars and ride the flow."}},
    };
    return all;
}

} // namespace

namespace presets {

FieldDoc fieldDoc(const QString &section, const QString &key) {
    const auto &all = entries();
    auto found = all.constFind(section + "." + key);
    if (found == all.constEnd())
        found = all.constFind("layer." + key);
    if (found == all.constEnd())
        return FieldDoc{key, QString()};
    return FieldDoc{QString::fromUtf8(found->label), QString::fromUtf8(found->help)};
}

} // namespace presets
