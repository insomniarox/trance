# Making effects for Trance

An effect in Trance is a **preset**: one JSON file that says how the picture
moves, what is drawn on it, and how it is finished. This manual explains
every setting a preset can have, what it does, and why it is there, and then
builds a preset from nothing.

Every setting has two names. The **editor name** is what the preset editor
shows (`E` in the window); resting the pointer on it gives a short
explanation. The **file name** is what goes in the JSON and never changes.
Both are given throughout.

Contents:

1. [How a picture is made](#1-how-a-picture-is-made)
2. [Where to work](#2-where-to-work)
3. [Listening to the music](#3-listening-to-the-music)
4. [Formulas](#4-formulas)
5. [Code blocks](#5-code-blocks)
6. [Where things are on screen](#6-where-things-are-on-screen)
7. [Motion: the feedback settings](#7-motion-the-feedback-settings)
8. [Layers](#8-layers)
9. [Colors and palettes](#9-colors-and-palettes)
10. [Background](#10-background)
11. [Finish: the post settings](#11-finish-the-post-settings)
12. [Building one from nothing](#12-building-one-from-nothing)
13. [When it looks wrong](#13-when-it-looks-wrong)

## 1. How a picture is made

Trance never draws a frame from scratch. Sixty times a second it does four
things, in this order:

1. **It listens.** The sound is measured: how loud each range of it is,
   whether a beat just landed, and the shape of the waveform.
2. **It bends the last frame.** The picture that was on screen a moment ago
   is zoomed, turned, slid and faded a little. This is the *feedback* step,
   and it is where all the motion comes from.
3. **It draws the layers on top**, in the order they are listed: waves,
   bars, shapes, a terrain, a wireframe, particles, vectors.
4. **It finishes the picture** on its way to the screen: glow, color
   changes, mirrors, and a background wherever the picture is dark.

Step 2 is the idea everything else rests on. Because each frame starts from
the one before, anything a layer draws is still there next frame, moved a
little and a little dimmer. Draw a single dot with a zoom above 1 and you
get a line streaming outward. Add a slight rotation and the line curls into
a spiral. Raise the decay and the spiral lasts longer. A layer only has to
say *where the new light goes*; the feedback decides *what happens to it
afterwards*.

So a preset has three parts that do different jobs:

| Part | In the file | Its job |
|---|---|---|
| Motion | `feedback`, and the `pixel` code | What happens to light already on screen |
| Layers | `layers` | Where new light is put each frame |
| Look | `palette`, `background`, `post`, and the `shade` code | The colors, and the finish |

A preset is at its best when the three work on each other: a wave gives
particles somewhere to be born, the motion carries the particles along with
the picture, and one palette colors all of it.

## 2. Where to work

**In the window.** `E` opens the editor on the preset that is showing. Its
tabs are the three parts above, plus the whole file as JSON. Every change
shows at once and nothing is written until you save. Saving one of your
presets under a name you have changed renames it; Save as (`Ctrl+Shift+S`)
leaves it as it was and saves a copy. Neither writes over another of your
presets. A setting whose name is in the accent color has been changed since
the last save; a dot on a tab or a layer says something inside it has.
Double-click a name to put that setting back to its default.

**In a file.** Your presets are in `~/.config/trance/presets/`
(`%LOCALAPPDATA%\trance\presets` on Windows), one `.json` each. Built-in
presets can't be changed, but saving one makes your own copy under the same
name, which shows in its place (marked "your copy" in the list) until you
delete it and the built-in comes back.

**From the command line**, which is the quickest way to see what a change
did without waiting for the music:

```sh
trance new "Night Drive"                 # start one (add --from Strata to copy)
trance show Strata                       # print a preset, to learn from
trance check "Night Drive"               # every problem, by setting
trance render "Night Drive" -o look.png --sheet 6 --seconds 8
```

`render --sheet 6` tiles six moments into one picture, so motion shows in a
still. `--audio song.mp3` renders against real music instead of the built-in
test loop. To see it move for real, `--frames dir --start 4` writes every
frame after the first four seconds as `dir/00001.png` and on, and
`ffmpeg -framerate 60 -i dir/%05d.png look.mp4` makes a video of them.

The smallest preset that does something:

```json
{
  "name": "First",
  "feedback": { "decay": 0.95, "zoom": 1.02 },
  "layers": [ { "type": "wave", "shape": "circle" } ]
}
```

Anything left out takes its default, so a preset only needs to say what is
different about it.

## 3. Listening to the music

These are the numbers a preset can read. Each is recomputed every frame.

| Name | What it is |
|---|---|
| `bass` `mid` `treb` | How loud the low, middle and high thirds of the sound are |
| `vol` | The three together |
| `sub` | 20–60 Hz: rumble, the bottom of a kick |
| `low` | 60–200 Hz: the kick and the bass line |
| `lowmid` | 200–600 Hz: warmth, low strings, toms |
| `highmid` | 600–2000 Hz: voices and leads |
| `presence` | 2000–6000 Hz: snares, the bite of a guitar |
| `air` | 6000–16000 Hz: cymbals and hiss |
| `beat` | Jumps to 1 when a beat lands and falls back to 0 in about half a second |
| `beats` | How many beats there have been |

Every loudness has a smoothed twin with `_att` on the end: `bass_att`,
`sub_att`, `air_att` and so on.

**What the numbers mean.** A loudness is not a volume. It is how loud that
range is *compared with its own recent average in this song*. So **1 is
normal**, 2 is a hard hit, 0 is silence, and the same preset reacts alike to
a quiet track and a loud one. This is why nearly every formula in a preset
has the shape

```
resting value + how much it moves * loudness
```

for example `"zoom": "1.01 + 0.02*bass_att"`: a zoom of 1.03 on an ordinary
bar, a little more on a hit, 1.01 in a pause.

**Plain or smoothed.** The plain ones (`bass`) jump: use them for things
that should flash, such as brightness or thickness. The smoothed ones
(`bass_att`) glide: use them for things that should move, such as size,
position and speed. Motion driven by a plain loudness jitters.

**Six ranges or three.** `bass`, `mid` and `treb` are wide and always have
something in them. The six narrow ranges tell the instruments apart, which
is what lets each layer of a preset move to a different part of the music.
A narrow range can be empty for a while and then leap when something sounds
in it, so cap it where a leap would look wrong: `min(air_att, 2)`.

**On the beat.** `beat` is the simplest way to make something happen in
time: `"rate": "50 + 1500*beat"` bursts particles on every beat, and
`above(beat, 0.9)` is true for a frame or two right as it lands.

## 4. Formulas

**Any number in a preset may be a formula in quotes instead.** `0.3` is a
fixed size; `"0.3 + 0.1*bass_att"` is a size that breathes. A formula is
worked out again every frame.

Besides the music, a formula can read:

| Name | What it is |
|---|---|
| `time` | Seconds since the preset started |
| `dt` | Seconds since the last frame (about 0.0167) |
| `frame` `fps` | Frame number, and frames a second |
| `aspect` | The screen's width divided by its height (1.78 on a wide screen) |
| `progress` | 0 to 1 through the time before the next automatic preset change |
| `pi` `tau` | 3.14159…, 6.28318… |

Arithmetic is `+ - * / %` and `^` for powers. Comparisons (`<`, `>`, `==`)
give 1 or 0, `&&` `||` `!` combine them, and `condition ? a : b` chooses.
Dividing by zero gives 0 rather than an error. A formula can put brackets,
signs and functions inside one another up to 64 deep, and a statement can
chain up to 256 operations, as a long sum does: far more than any preset
needs, and the check says so if one goes past.

Functions:

| | |
|---|---|
| `sin cos tan asin acos atan atan2(y, x)` | Trigonometry, in radians |
| `abs sign floor ceil fract int sqrt sqr exp log pow(x, y)` | The usual |
| `min(a, b) max(a, b) clamp(x, low, high) mod(x, y)` | Limits and wrapping |
| `mix(a, b, t)` | From `a` to `b` as `t` goes 0 to 1 |
| `step(edge, x)` `smoothstep(a, b, x)` | A hard and a soft switch from 0 to 1 |
| `if(c, a, b) above(a, b) below(a, b) equal(a, b)` | Choosing, and comparing |
| `band(a, b) bor(a, b) bnot(a)` | And, or, not |
| `rand()` `rand(n)` | A random number from 0 to 1, or 0 to `n`, new each time |
| `noise(x)` `noise(x, y)` | A smooth random number from 0 to 1: nearby inputs give nearby results |
| `length(x, y)` | Distance from the middle |
| `tri(x)` | A triangle wave |

Three shapes of formula cover most of what presets do:

- **Breathing:** `rest + amount*loudness`.
- **Swaying:** `amount*sin(time*speed)`. A slow sine on a rotation or a
  position keeps a picture from ever standing still.
- **Travelling:** `fract(time*speed)` runs from 0 to 1 again and again, for
  rings that keep spreading or anything that loops.

## 5. Code blocks

A formula gives one setting a value. A code block is several statements,
one to a line (or separated by `;`), and it can make **variables of your
own** that every later formula can read. There are four, each a top-level
entry in the file.

| Block | Runs | Reads | May change |
|---|---|---|---|
| `init` | Once, when the preset starts | | Your own variables |
| `frame` | Every frame, before anything is drawn | Everything in sections 3 and 4 | Your own variables, and any feedback setting |
| `pixel` | For every pixel of the feedback step | `x` `y` `rad` `ang` | `zoom` `rot` `dx` `dy` `sx` `sy` `cx` `cy` `warp` `decay`, and `x` `y` |
| `shade` | For every pixel on the way to the screen | `x` `y` `rad` `ang` `lum` | `r` `g` `b` |

**`init`** gives variables their starting values. Roll them with `rand()`
and the preset is a little different every time it comes round:

```json
"init": "ka = 2 + rand(3)\nph = rand(6)"
```

**`frame`** is where a preset keeps things that build up over time. The
most useful line in the whole format is an accumulator:

```json
"frame": "spin = spin + dt*(0.2 + 0.8*bass_att)"
```

`spin` now turns faster when the bass is loud and never jumps backwards,
which `time*bass_att` would. Use it as an angle, a phase, or a position. A
variable that is read before it is ever assigned starts at 0. Each frame the
feedback formulas are worked out first, then `frame` runs, then everything
else, so `frame` may overwrite a feedback setting (`zoom = 1.05`) and a
variable made in `frame` is ready for every layer.

**`pixel`** makes the motion different in different places. It runs on the
graphics card for every pixel, with `rad` the distance from the middle and
`ang` the angle. Two shapes cover most flows:

```json
"pixel": "dx = 0.005*sin(y*4 + time)"
```
is a *current*: each height slides sideways by its own amount.

```json
"pixel": "rot = 0.02*sin(rad*5 - time)"
```
is a *whirlpool*: rings at different distances turn different ways.

**`shade`** recolors the finished picture, pixel by pixel: `lum` is how
bright the pixel is, and `r` `g` `b` are yours to change.

```json
"shade": "r = r*(1 + 0.3*sin(rad*8 - time))"
```

## 6. Where things are on screen

The middle of the screen is (0, 0). `y` runs from -1 at the bottom to 1 at
the top. `x` runs from `-aspect` on the left to `aspect` on the right, about
±1.78 on a wide screen, so one unit is the same length both ways and a
circle is always round. Write `x` as a share of `aspect` (`0.8*aspect`) when
something should keep to the edge on any screen.

Angles are in radians, counter-clockwise, with 0 pointing right: 1.57 is
up, 3.14 is a half turn, 6.28 a whole one.

Sizes given in pixels (thickness, particle size, outline width) are for a
screen 1080 lines tall and scale with the window.

## 7. Motion: the feedback settings

These are in `"feedback": { … }`, on the editor's **Motion** tab. They say
what happens to the last frame before the new one is drawn on it. Motion
values are per frame at 60 frames a second; Trance adjusts them at other
rates, so a preset moves the same on any screen.

They are applied in this order: the `pixel` code, then zoom, stretch,
rotation, slide and warp decide *where* each pixel is copied from; then
blur, hue drift and decay decide *how it looks* when it gets there.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Decay | `decay` | 0.96 | 0.5 to 1 | How much of the last frame survives. 1 never fades; 0.9 is gone in under a second |
| Zoom | `zoom` | 1 | 0.8 to 1.2 | Above 1 grows the last frame, so everything streams outward. Below 1 shrinks it, so everything falls into the middle |
| Zoom curve | `zoomexp` | 1 | 0.25 to 4 | Above 1 zooms the edges more than the middle; below 1 the middle more than the edges |
| Rotation | `rot` | 0 | -0.2 to 0.2 | Turns the last frame, in radians per frame |
| Warp | `warp` | 0 | 0 to 5 | A slow liquid wobble over the whole picture |
| Warp size | `warpScale` | 1 | 0.1 to 5 | How broad the wobble's swells are |
| Warp speed | `warpSpeed` | 1 | 0 to 5 | How fast the wobble moves |
| Center X, Center Y | `cx` `cy` | 0 | -1 to 1 | The point zoom and rotation work around |
| Slide X, Slide Y | `dx` `dy` | 0 | -0.05 to 0.05 | Slides the last frame: positive X drifts right, positive Y drifts up |
| Stretch X, Stretch Y | `sx` `sy` | 1 | 0.8 to 1.2 | Stretches the last frame wider or taller (above 1) or squeezes it (below 1) |
| Blur | `blur` | 0 | 0 to 1 | Softens the last frame, so trails turn to smoke |
| Hue drift | `hueshift` | 0 | -0.2 to 0.2 | Turns the trail's colors a little every frame |
| Edge | `edge` | `wrap` | | What lies past the screen: `wrap`, `mirror`, `clamp` or `black` |
| Darken center | `darkenCenter` | off | | A soft dark spot in the middle every frame |

**The logic of each.**

*Decay* is the length of the memory. Everything else in this section only
shows for as long as decay lets the picture last. With decay at 0.8 the
picture is nearly just the layers; at 0.99 it is mostly history. Start at
0.95 and move from there. A decay of exactly 1 never clears anything, which
suits a preset whose slide carries the old picture off the screen.

*Zoom* is the tunnel. The numbers are small because they repeat sixty times
a second: 1.02 doubles the size of something in about half a second. Tie it
to the bass (`"1.01 + 0.03*bass_att"`) and the picture lunges on every kick.
Zoom below 1 pulls the edge of the screen inward, so the *edge* setting
decides what arrives: `black` gives clean shapes falling into the middle,
`wrap` or `mirror` an endless regress.

*Zoom curve* turns a flat zoom into a lens. Above 1 the middle hangs back
while the edges rush, which reads as speed.

*Rotation* with no zoom spins the picture in place. With a zoom it makes
spirals. A slow sine (`"0.008*sin(time*0.2)"`) that changes direction keeps
a spiral from looking mechanical.

*Warp* is for when a picture is too tidy. It moves every part of it by a
slightly different amount, slowly, like looking through water.

*Center* moves the vanishing point. Sway it and the whole tunnel leans.

*Slide* is wind. A steady slide with `edge` set to `black` makes anything
drawn at one side leave a ribbon across the screen; this is how a scrolling
picture is made.

*Stretch* is a zoom on one axis: stretch X above 1 with stretch Y below 1
smears the picture into horizontal streaks.

*Blur* decides whether trails are ink or smoke. Sharp trails show every
frame as a separate copy; a little blur melts them together.

*Hue drift* makes age visible: the newest light is the color it was drawn
in and older light has turned further round the color wheel.

*Edge* matters whenever the motion reaches outside the screen. `wrap`
brings in the opposite side, `mirror` a reflection (no seams), `clamp`
stretches the border's color outward, `black` brings in nothing.

*Darken center* exists because a zoom above 1 keeps enlarging the middle of
the picture into itself, and added light there piles up into one flat
blaze. The dark spot is too soft to see but enough to stop it.

## 8. Layers

`"layers": [ … ]` is a list, drawn first to last, so a later layer covers an
earlier one. Each has a `type`. In the editor they are cards on the
**Layers** tab, with buttons to add, reorder, copy and remove them. A preset
can have up to 24.

### Settings every layer has

| In the editor | In the file | What it does |
|---|---|---|
| Shown | `enabled` | Turns the layer off without removing it |
| Blend | `blend` | How its light meets what is under it: `over` covers it, `add` adds to it, `max` keeps the brighter of the two |
| Opacity | `alpha` | How solid it is drawn, 0 to 1 |
| Color | `color` | See [section 9](#9-colors-and-palettes) |

*Blend* is the difference between light and paint. `add` layers glow and
blend, and two of them crossing make a brighter spot. They are also the
usual reason a picture burns out: light that adds up every frame on top of
a long decay has nowhere to go, and spreads into flat patches at the full
strength of its color. Light keeps its hue however much of it piles up, so
a patch is white only where the color is, or where colors cross and add up
to it. `over` covers what is under it, which
is how to draw something solid, or something black that hides the middle.
`max` keeps whichever is brighter, the layer or what is under it, so it
glows where it is drawn but never grows past its own color, however long
the decay and however often it crosses itself: G-Force's way, where a line
overwrites the picture instead of adding to it. Dense layers that flatten
out as `add` keep their detail as `max`; a dark color drawn with it shows only
on darker ground. Waves, terrain, wireframes, vectors and particles are
`add` unless you say otherwise, bars and shapes `over`. Older presets that
say `"additive": true` or `false` mean `add` and `over`.

A layer's formulas can read everything in sections 3 and 4, plus a few
numbers of the layer's own, which are different for each point, bar, copy or
spark. Those are what make one layer a rainbow rather than one color:

| Name | In a wave | In bars | In a shape | In a terrain | In a wireframe | In particles | In vectors |
|---|---|---|---|---|---|---|---|
| `t` | 0 to 1 along the line | 0 to 1 across | 0 to 1 across the copies | 0 to 1 across | 0 to 1, lowest pitch to highest | age, 0 to 1 | 0 to 1 across |
| `i` | the point's number | the bar's number | the copy's number | distance, 0 front to 1 | age, 0 newest to 1 | a random number kept for life | 0 to 1 up |
| `v` | the sound there | the bar's height | the spectrum at `t` | the height there | the height there | speed | how fast the picture moves there |
| `v2` | the other channel | | the waveform at `t` | | | | |
| `n` `count` | | | the copy's number, and how many | | | | |

Only the color and the `point` code are worked out point by point. Every
other setting, `alpha` included, is worked out once a frame for the whole
layer, before its first point, so there `t`, `i` and `v` are always 0: a
terrain with `"alpha": "1 - 0.7*i"` is simply drawn at full strength, and
`trance check` warns about it. To fade a layer point by point, set `a` in
its `point` code, which waves, bars and wireframes have. Particles already fade as they age and a terrain
as it recedes, so they don't need it. Shapes are the exception: with
several copies, every setting is worked out once per copy and may use them.

### Wave

The sound drawn as a line. `"type": "wave"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Source | `source` | `mix` | | `mix`, `left`, `right`, or `spectrum` |
| Shape | `shape` | `line` | | `line`, `circle`, `xy` or `custom` |
| Points | `samples` | 256 | 8 to 512 | How many points the line has |
| Height | `scale` | 0.4 | 0 to 2 | How far the sound pushes the line |
| Smoothing | `smooth` | 0.3 | 0 to 1 | Rounds the line off |
| X, Y | `x` `y` | 0 | | Where its middle is |
| Width | `width` | 1 | 0 to 2 | How much of the screen's width a line spans |
| Radius | `radius` | 0.5 | 0 to 1.5 | The ring's size, for a circle |
| Angle | `angle` | 0 | | Turns it |
| Thickness | `thickness` | 2.5 | 0.5 to 40 | Line width in pixels |
| Dots | `dots` | off | | Draws the points without joining them |
| Mirror | `mirror` | `none` | | Reflected copies: `x`, `y` or `quad` |
| Point code | `point` | | | Code run for every point |

*Source* chooses between two different pictures of the sound. The waveform
(`mix`, `left`, `right`) is the sound's actual shape over the last fiftieth
of a second: it wriggles, and it is the same kind of wriggle whatever is
playing. The `spectrum` is loudness by pitch, bass at one end and treble at
the other: it stands still and its humps are the instruments.

*Shape*: a `line` across the screen is an oscilloscope; a `circle` wraps it
into a ring that the sound pushes in and out; `xy` plots the left channel
against the right, which is a knot that opens up the wider the stereo is.
`custom` puts every point at X, Y and leaves the rest to the point code.

*Point code* is the most powerful thing a wave has, because it lets the
line be any curve at all. It runs once for each point, with `t` going from
0 at the first point to 1 at the last, and whatever it leaves in `x` and
`y` is where the point goes. A circle, written out:

```json
{ "type": "wave", "shape": "custom", "samples": 128,
  "point": "th = t*tau\nx = 0.5*cos(th)\ny = 0.5*sin(th)" }
```

Change the radius to `0.5*(0.7 + 0.3*cos(5*th))` and it is a flower with
five petals; add `+ v*0.03` and the sound ripples along it. The point code
may also change `r`, `g`, `b` and `a` to color or fade the line along its
length.

### Bars

The spectrum as bars, bass to treble. `"type": "bars"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Layout | `layout` | `bottom` | | `bottom`, `top`, `center` or `radial` |
| Bars | `count` | 48 | 4 to 128 | How many |
| X, Y | `x` `y` | 0 | | Moves the row, or the middle of the circle |
| Width | `width` | 0.9 | 0.1 to 1 | How much of the screen's width the row takes |
| Height | `height` | 0.8 | 0 to 2 | How tall a full bar is |
| Gap | `gap` | 0.25 | 0 to 0.9 | The space between bars, as a share of each bar's slot |
| Radius | `radius` | 0.35 | 0 to 1 | The circle the bars stand on, for `radial` |
| Angle | `angle` | 0 | | Turns that circle |
| Foot fade | `fade` | 0.6 | 0 to 1 | How dark the foot of a bar is beside its tip |
| Peak marks | `peaks` | on | | A mark that stays at each bar's recent high and sinks slowly |
| Symmetric | `symmetric` | off | | Bass in the middle, treble out to both sides |
| Bar code | `point` | | | Code run for every bar; may change `v` `r` `g` `b` `a` |

Bars are the one layer that needs no explaining to whoever is watching,
which makes them a good anchor for a picture that is otherwise abstract.
`radial` bars round a circle with a zoom behind them make a sunburst.
Bars are `over` unless you say otherwise, so they cover what is under them.

### Shape

A polygon, or many copies of one. `"type": "shape"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Sides | `sides` | 4 | 3 to 64 | 3 is a triangle; from about 32 it reads as a circle |
| Copies | `instances` | 1 | 1 to 512 | How many to draw |
| X, Y | `x` `y` | 0 | | Where its middle is |
| Size | `radius` | 0.3 | 0 to 2 | From the middle to the corners |
| Angle | `angle` | 0 | | Turns it |
| Center color, Center opacity | `color` `alpha` | `accent`, 1 | | The fill at the middle |
| Rim color, Rim opacity | `color2` `alpha2` | `black`, 0 | | The fill at the corners |
| Outline color | `border` | `foreground` | | The line round it |
| Outline opacity | `borderAlpha` | 0 | 0 to 1 | 0 draws no outline |
| Outline width | `borderWidth` | 2 | 0.5 to 20 | In pixels |
| Show the picture | `textured` | off | | Fills the shape with last frame's picture |
| Picture zoom, Picture angle | `texZoom` `texAngle` | 1, 0 | | How that picture sits inside the shape |
| Copy code | `code` | | | Code run for every copy |

The fill runs smoothly from the center color to the rim color, and that one
fact gives three quite different things:

- Center and rim both solid: a flat tile.
- Center solid, rim opacity 0: a **soft glowing ball**, the usual way to
  draw a light.
- Both opacities 0 and an outline: a **ring** or an empty polygon.

*Copies* turn one shape into a crowd. Every setting is worked out again for
each copy with `n` (0, 1, 2…), `count` and `t` (0 to 1 across them), so a
formula places them: `"x": "(t*2 - 1)*aspect"` spreads them across the
screen, `"x": "0.5*cos(t*tau)"` with `"y": "0.5*sin(t*tau)"` rings them.
Each copy also gets `v`, the spectrum at its place in the row, so
`"radius": "0.03 + 0.1*v"` makes every copy answer a different pitch.

*Show the picture* fills the shape with the previous frame instead of a
color. The shape becomes a window onto the feedback, and since the feedback
then contains the window, it repeats inside itself.

### Terrain

The spectrum as a landscape scrolling away to a horizon. The newest row is
at the front and each older row sits further back. `"type": "terrain"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Style | `style` | `mesh` | | `mesh` (lines both ways), `lines` (rows only) or `dots` |
| Rows | `rows` | 36 | 8 to 64 | How many rows of the past it keeps |
| Columns | `cols` | 64 | 16 to 128 | How many points across each row |
| Speed | `speed` | 18 | 1 to 60 | Rows a second: how fast it scrolls away |
| Height | `height` | 0.55 | 0 to 2 | How tall the loudest peaks stand |
| Width | `width` | 1.7 | 0.2 to 4 | Half the width of the front edge |
| Front edge | `y` | -0.75 | -1.5 to 1 | Where the nearest row sits |
| Horizon | `horizon` | 0.25 | -1 to 1 | The height everything recedes toward |
| Depth | `depth` | 3 | 0.2 to 10 | How strongly distance shrinks things |
| Thickness | `thickness` | 1.6 | 0.5 to 10 | Line width in pixels |

A terrain is its own memory: it keeps the last few seconds of the spectrum
without any help from the feedback, so it works with a low decay and a still
picture. Rows and speed together set how many seconds are on screen (rows
divided by speed). In its color, `v` is the height and `i` the distance, so
`palette(0.3 + 0.5*v)` colors the peaks differently from the plain.

### Wireframe

The last few seconds of the spectrum as a sheet of lines hung in space, the
way SoundSpectrum's WhiteCap draws it. Pitch runs across the sheet and time
along it: every line across is the spectrum as it was at one moment, the
newest at one end. `"type": "wireframe"`.

It answers three questions, and its settings fall into three groups.

*What shape is the sheet in?*

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Form | `form` | `sheet` | | `sheet`, `tube`, `ring` or `sphere` |
| Second form | `form2` | `sheet` | | The form it turns into |
| Morph | `morph` | 0 | 0 to 1 | How far it is from the one to the other |
| Width | `width` | 1.2 | 0.1 to 4 | Half the width of the flat sheet |
| Length | `depth` | 1.2 | 0.1 to 4 | Half the length of the sheet or the tube |
| Radius | `radius` | 0.6 | 0.05 to 3 | The size of the tube, ring or sphere |
| Height | `height` | 0.5 | -2 to 2 | How far the loudest bin stands out |

A `sheet` lies flat, with the sound standing up from it. A `tube` is the
sheet rolled up, time running down its length. A `ring` is a disc with the
newest sample at the hole in the middle, so the sound spreads outward like
rings on a pond. A `sphere` wraps it round a ball, the newest sample at the
top. On the three that close on themselves the pitch runs up one side and
back down the other, so that the two edges of the sheet meet without a step;
a sheet that morphs into one of them is laid out the same way.

*Morph* is what makes it WhiteCap: `"form": "sheet", "form2": "tube",
"morph": "0.5 - 0.5*cos(time*0.2)"` rolls the sheet up and lays it flat
again, twice a minute. And when none of the four forms is the one you want,
the *Point code* (`point`) moves every point after the form has placed it.
It reads `t`, `i` and `v` and may change `x`, `y` and `z`, which are right,
up and away from you before the camera turns anything. This twists the
sheet along its length:

```json
"point": "w = z*1.5; x1 = x*cos(w) - y*sin(w); y = x*sin(w) + y*cos(w); x = x1"
```

*Where is it seen from?*

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Turn | `yaw` | 0 | | Turns it about its upright axis |
| Tip | `pitch` | 0.5 | -1.57 to 1.57 | Tips it toward you: 0 is from the side, 1.57 from above |
| Roll | `roll` | 0 | | Turns the view itself |
| Distance | `distance` | 3 | 0.5 to 20 | How far away the camera stands |
| Size | `size` | 1 | 0.1 to 4 | Scales the whole of it on screen |
| X, Y | `x` `y` | 0 | | Where its middle is on screen |
| Style | `style` | `mesh` | | `mesh`, `lines`, `strands` or `dots` |
| Thickness | `thickness` | 1.4 | 0.5 to 10 | Line width in pixels |
| Fog | `fog` | 0.6 | 0 to 1 | How much the far side is dimmed |

A formula in *Turn* (`"time*0.2"`) is what shows that the thing is solid;
held still, a wireframe reads as a flat drawing. *Distance* sets how strong
the perspective is. From far away near and far are the same size; close up
the near edge looms. Closer than the form is big, the camera is inside it: a
tube with a `depth` of 3 seen from a `distance` of 1 is a tunnel whose walls
are the music. *Style* chooses which lines of the mesh are drawn: `lines`
are the samples, one spectrum after another, and `strands` are the bins,
each a thread running back through time.

*How does it hear?*

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Bins | `bins` | 48 | 8 to 128 | How many lines wide the sheet is |
| Samples kept | `rows` | 32 | 4 to 64 | How many moments of the past it holds |
| Samples a second | `speed` | 20 | 1 to 60 | How often a new one is taken |
| Lowest pitch | `low` | 40 | 0 to 4000 | Where its stretch of the sound begins, in Hz |
| Highest pitch | `high` | 5000 | 200 to 16000 | Where it ends, in Hz |
| Treble weight | `tilt` | 10 | 0 to 30 | How much more the top of the stretch counts |
| Smoothing | `smooth` | 0.35 | 0 to 0.95 | Rounds neighbouring bins into each other |
| Fall time | `fade` | 0.35 | 0.02 to 3 | Seconds a bin takes to sink back |

This is the part that sets a wireframe apart from every other layer. Bars, a
terrain and a spectrum wave all read the same measurement, which is built to
show *everything that is sounding*: its bands are spaced by octave, the way
the ear hears pitch, and measured in decibels, which lifts quiet sounds up
beside loud ones. A wireframe takes the measurement WhiteCap takes, which
shows *what stands out*:

- The stretch from *Lowest pitch* to *Highest pitch* is cut into *Bins* of
  equal width in Hz, not in octaves. Bass takes up little room and the
  space goes to the harmonics, so a voice or a lead draws a row of evenly
  spaced ridges.
- Each bin is the average of everything in it, and loudness is left as it
  is. A sound twice as strong stands twice as tall. Quiet parts of the mix
  lie nearly flat, and what is loud rises out of them alone.
- Music is far stronger low down, so *Treble weight* counts the top of the
  stretch for more. At 0 a kick drum is the only thing on the sheet.
- The loudest bin of the last few seconds is given the height 1. A sudden
  hit stands taller than that for a moment.
- A bin rises at once and sinks over the *Fall time*. Short, and the sheet
  flickers with every note; long, and held notes pile up into ridges while
  a drum leaves a slope behind it.

So put a wireframe beside bars in one preset and they will not move alike:
the bars fill and empty across their whole width, while the wireframe stays
calm and throws up a few sharp crests. *Lowest* and *Highest pitch* aim it:
`"low": 150, "high": 1200` spreads a melody across the whole sheet, and
`"low": 2000, "high": 12000` leaves only cymbals and the breath of a voice.

**One for each range.** Set *Lowest* and *Highest pitch* to the edges of
one of the six ranges in [section 3](#3-listening-to-the-music) (`"low":
60, "high": 200` for `low`), and drive the same layer's size, height or
opacity with that range's variable (`"height": "0.1 + 0.15*low_att"`).
The layer then both shows and moves with that part of the music alone,
and six of them side by side come apart the way the instruments do.
`trance show Terraces` stacks six sheets that way, and `trance show
Moons` puts six globes in orbit.

Like a terrain it is its own memory, and needs nothing from the feedback: a
low decay gives clean lines, a high one lets the turning form smear into a
solid. Particles with `"emit": "wave"` are born along its newest sample.

### Particles

Sparks. `"type": "particles"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Most alive | `count` | 600 | 16 to 4000 | The most sparks alive at once |
| Birth rate | `rate` | 120 | 0 to 2000 | Sparks born every second |
| Born at | `emit` | `point` | | `point`, `wave`, `bars`, `edges` or `screen` |
| X, Y | `x` `y` | 0 | | The birth point, and the point pull and swirl work around |
| Birth area | `area` | 0 | 0 to 2 | How far from that point a spark may be born |
| Speed | `speed` | 0.6 | 0 to 4 | How fast they set off, in screen-heights a second |
| Direction | `angle` | 0 | | Which way they set off |
| Spread | `spread` | 6.28 | 0 to 6.28 | How wide a fan of directions around that |
| Gravity | `gravity` | 0 | -3 to 3 | Pulls them down; negative lets them rise |
| Drag | `drag` | 0.4 | -3 to 6 | Slows them as they go; negative speeds them up |
| Ride the flow | `flow` | 0 | 0 to 2 | 1 carries them exactly as the feedback carries the picture |
| Currents | `field` | 0 | 0 to 4 | The strength of a field of currents of their own |
| Current detail | `fieldScale` | 1.5 | 0.2 to 8 | How tight those currents' swirls are |
| Current change | `fieldSpeed` | 0.2 | 0 to 3 | How fast the currents shift |
| Pull | `pull` | 0 | -6 to 6 | Draws them toward X, Y; negative pushes them away |
| Swirl | `swirl` | 0 | -6 to 6 | Swings them around X, Y |
| At the border | `edge` | `none` | | `none`, `wrap` or `bounce` |
| Lifetime | `life` | 2 | 0.1 to 10 | Seconds a spark lives |
| Size | `size` | 3 | 0.5 to 30 | In pixels |
| Streak | `streak` | 0 | 0 to 0.5 | Draws each as a line this many seconds long |

Particles have the most settings because they answer three separate
questions.

*Where are they born?* `point` is a fountain at X, Y. `wave` puts them
along the wave layers listed **before** this one, mostly where the sound is
loudest, so the line seems to shed them (a wireframe counts: they come from
its newest sample). `bars` puts them on the tips of the
bars. `edges` starts them on the border heading for X, Y. `screen` is
anywhere. *Birth rate* decides how many; tie it to the beat or to a range
and the sparks become that instrument.

*What moves them?* Their own speed and direction at first. Then any of:
*gravity* (rain, embers), *drag* (a puff that hangs in the air), *pull* and
*swirl* (a drain, an orbit), *currents* (a field that makes neighbouring
sparks travel together, like dust in wind), and *ride the flow*. The last is
the one that makes particles belong to the picture: at 1 a spark is moved
exactly as the feedback moves the pixel under it, so sparks stream down the
same tunnel and round the same whirlpools as everything else.

*How long, and how do they look?* *Lifetime* and *Most alive* together cap
how busy the layer gets: once that many are alive no more are born. Each
spark fades as it ages. *Streak* draws a line back along the way it came,
which turns dots into rain or tracer fire. In the color, `t` is the spark's
age and `i` a random number it keeps for life, so `palette(i)` gives each
its own color and `palette(t)` changes all of them as they age.

### Vectors

A grid of short strokes, each pointing the way the picture is flowing at
that place. `"type": "vectors"`.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Columns, Rows | `cols` `rows` | 28, 16 | 2 to 64, 2 to 48 | How many strokes |
| Length | `length` | 1 | 0 to 8 | How long a stroke is for a given speed of flow |
| Thickness | `thickness` | 1.5 | 0.5 to 12 | Line width in pixels |
| Dots only | `dots` | off | | Marks each place with a dot instead |

Vectors draw nothing of the music. They draw the motion itself, so they
only do anything in a preset whose feedback moves. They are the quickest way
to see what a `pixel` flow is doing while you write it, and worth keeping
faintly in the finished picture.

## 9. Colors and palettes

A color is text, one of:

| | |
|---|---|
| `"#ff2d95"` `"white"` `"black"` | A fixed color |
| `"accent"` `"foreground"` `"red"` `"cyan"` … | A color from the desktop theme, which follows the theme live |
| `"palette(formula)"` | A point on the preset's palette |
| `"hsv(h, s, v)"` `"rgb(r, g, b)"` | Each part a formula from 0 to 1 |

The theme's colors are `accent`, `foreground`, `background`, `selection`,
`muted`, `red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `magenta`,
`brown`, and `bright_red` to `bright_magenta`.

**The palette** is the top-level `"palette"`: a name or your own list,
`["#000000", "accent", "#ffffff"]`. The names are:

| | |
|---|---|
| `theme` `accent` | Built from the desktop theme, and follow it live |
| `fire` `ocean` `ice` `gold` `magma` `inferno` `viridis` `lagoon` `forest` `dusk` `arctic` `amethyst` `copper` `phosphor` `sepia` `steel` `rose` `abyss` `thermal` `nova` `jade` `crimson` `sulfur` `cobalt` `cividis` `noir` `storm` `blacklight` `cyanotype` `sakura` `lichen` | Run from dark to bright, which is what Colorize wants |
| `neon` `sunset` `aurora` `vapor` `acid` `rainbow` `cyber` `candy` `tropic` `retro` `carnival` `bauhaus` `citrus` `spectral` `reef` `autumn` `riso` `solstice` | Bright all the way round, for coloring layers apart |
| `twilight` | Pale, down through dark and back up: its ends meet, so a palette that keeps turning has no seam |
| `phase` | Every hue at about the same brightness, so a palette that keeps turning changes color without pulsing |
| `opal` | Pale all the way round, for a glow that changes color but never darkens; its ends meet too |
| `hologram` | Light and bright all the way round, like the foil of a hologram turning; its ends meet too |
| `turbo` | A rainbow from the plotting world, dark blue up through cyan, green and yellow and down through orange to dark red: dark at both ends and brightest in the middle |
| `contour` `inkwell` | Dark and light in turn, bright at the end: under Colorize a fading trail passes through every band, so the screen is drawn in contours, the way G-Force colors its pictures |
| `mono` | Black to white |

A palette is a strip of colors that blends from each to the next and then
round to the first again. `palette(0)` is its start, `palette(0.5)` halfway along, and
because it is a loop the number can keep growing.

That loop is why nearly every color in a built-in preset is a
`palette(...)`:

- `palette(t)` spreads the palette along a wave, across the bars, or over
  the copies of a shape.
- `palette(t + time*0.1)` does the same and keeps it moving.
- `palette(0.2)`, `palette(0.5)`, `palette(0.8)` on three layers gives them
  three colors that belong together.
- `palette(i)` gives every spark its own.

A preset colored this way looks right under any palette, including the one
picked for every preset in the settings. A preset with fixed `#` colors
ignores both. Prefer the palette and the theme's names.

While the editor is open, the preset's own palette shows, whatever the
settings and drifting colors would put in its place, so a palette you
pick there is the one you see. They come back when the editor closes.

The preset list's color filter goes by the palette too. Each stop pulls
toward its hue as hard as it is colorful, so black, white and grays pull
not at all, and the hue that wins puts the preset among the reds (and
pinks), oranges (and yellows), greens, blues (and teals) or violets. When
the stops pull different ways and no hue wins clearly, it goes under many
colors; with hardly any color at all, under grays; and a palette that
names a theme color goes under the theme. Your own list of colors is
sorted the same way, as soon as the preset is saved. A preset whose layers
use fixed `#` colors is still sorted by its palette.

## 10. Background

`"background": { … }`, on the **Look** tab. It is drawn fresh every frame
behind the picture and shows only where the picture is dark, so it never
smears and never fights the layers. In glass mode the desktop takes its
place.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Style | `style` | `none` | | `gradient`, `nebula`, `stars`, `grid`, `rays` or `plasma` |
| Brightness | `intensity` | 0.4 | 0 to 2 | How bright it is |
| Palette start | `tint` | 0 | 0 to 1 | Where on the palette its colors begin |
| Palette span | `spread` | 0.3 | 0 to 1 | How much of the palette it uses from there |
| Detail | `scale` | 1 | 0.2 to 5 | How fine its features are |
| Speed | `speed` | 1 | 0 to 5 | How fast it moves |

A background gives a sparse preset somewhere to be. `gradient` is a soft
wash, `nebula` drifting clouds, `stars` a field of them at three depths,
`grid` a floor running to the horizon, `rays` beams from the middle, and
`plasma` slow colored swells. Keep it dim (0.2 to 0.4) so the layers stay
the subject, and let the lowest sounds breathe through it:
`"intensity": "0.25 + 0.2*sub_att"`.

## 11. Finish: the post settings

`"post": { … }`, on the **Look** tab. These work on the finished picture on
its way to the screen and never touch the feedback, so they cannot build up
or smear. They are applied in the order of this table.

| In the editor | In the file | Default | Range | What it does |
|---|---|---|---|---|
| Mirror | `mirror` | `none` | | Reflects the picture: `x`, `y` or `quad` |
| Kaleidoscope | `kaleido` | 0 | 0 to 16 | Folds it into this many mirrored wedges |
| Kaleidoscope angle | `kaleidoAngle` | 0 | | Turns the wedges |
| Color fringe | `chroma` | 0 | 0 to 3 | Pulls red and blue apart toward the edges |
| Echo | `echoAlpha` | 0 | 0 to 1 | A second copy of the picture over the first |
| Echo zoom | `echoZoom` | 1 | 0.25 to 4 | The size of that copy |
| Echo flip | `echoFlip` | `none` | | Flips the copy: `x`, `y` or `xy` |
| Bloom | `bloom` | 0.5 | 0 to 3 | Glow around the bright parts |
| Bloom threshold | `bloomThreshold` | 0.55 | 0 to 1.2 | How bright something must be to glow |
| Colorize | `colorize` | 0 | 0 to 1 | Replaces colors with the palette, by brightness |
| Palette shift | `colorShift` | 0 | -1 to 1 | Slides that coloring along the palette |
| Palette curve | `colorCurve` | 1 | 0.2 to 4 | Bends it toward the dark or the bright colors |
| Contrast | `contrast` | 1 | 0 to 2 | Pushes darks and lights apart or together |
| Brightness | `brightness` | 1 | 0 to 3 | Multiplies the whole picture |
| Saturation | `saturation` | 1 | 0 to 2 | 0 is black and white |
| Hue | `hue` | 0 | | Turns every color, in radians |
| Gamma | `gamma` | 1 | 0.3 to 3 | Lifts or sinks the middle tones |
| Invert | `invert` | 0 | 0 to 1 | Mixes in the negative |
| Vignette | `vignette` | 0.3 | 0 to 1.5 | Darkens the corners |
| Scanlines | `scanlines` | 0 | 0 to 1 | Dark horizontal lines |
| Grain | `grain` | 0 | 0 to 0.5 | Film grain |

The `shade` code runs after Invert, and the background is added after
that, so neither colorize nor `shade` changes the background.

**The logic of the ones that matter most.**

*Bloom* is why a thin line looks like light instead of a drawing. It lifts
whatever is brighter than the threshold and spreads it. Too much of it
turns every preset into the same fog; 0.7 to 1.2 suits most.

*Colorize* is a different way of coloring a whole preset. Instead of giving
each layer a color, draw everything in white and let colorize map brightness
onto the palette: the dim ends of trails take the palette's first colors and
the bright cores its last. Fire, ice and anything that should look like one
material are made this way. *Palette curve* decides how much of the picture
is in the dark colors, and a slowly moving *Palette shift* keeps it alive.

*Mirror* and *Kaleidoscope* make symmetry out of anything. They only fold
the finished picture, so the layers and the feedback underneath stay as
they are. With a kaleidoscope of N wedges only the slice just above the
positive x axis is ever seen, so that is where to draw.

*Echo* lays a zoomed or flipped copy over the picture, which doubles the
detail for nothing.

*Vignette*, *grain* and *scanlines* are the frame round the picture. A
little vignette (0.4 to 0.6) holds the eye in the middle.

## 12. Building one from nothing

The plan: three rings, each moving to a different part of the music, in a
slow tunnel, shedding sparks.

**Start with the motion and one thing to see.** Nothing can be judged until
there is light for the feedback to work on.

```json
{
  "name": "Three Rings",
  "palette": "sunset",
  "feedback": { "decay": 0.95, "zoom": 1.012, "edge": "black" },
  "layers": [
    { "type": "wave", "shape": "circle", "radius": 0.5, "scale": 0.05,
      "color": "palette(0.3)" }
  ]
}
```

`trance render "Three Rings" --sheet 4` shows a ring with a trail streaming
outward from it. The zoom is small so the trail is short and dense.

**Give it a range of the music.** The ring should swell with the bass line,
which is `low`. Size is motion, so use the smoothed one:

```json
"radius": "0.5 + 0.06*low_att"
```

and let the plain one flash its thickness: `"thickness": "2 + 2*low"`.

**Add the other two, each on its own range.** Copy the layer twice. A
smaller ring for the leads and the smallest for the cymbals, each a
different point on the palette:

```json
{ "type": "wave", "shape": "circle", "radius": "0.32 + 0.05*highmid_att",
  "scale": 0.04, "thickness": "1.5 + 2*highmid", "color": "palette(0.55)" },
{ "type": "wave", "shape": "circle", "radius": "0.16 + 0.04*min(air_att, 2)",
  "scale": 0.03, "thickness": "1 + 1.5*air", "color": "palette(0.8)" }
```

The cymbals' range is often empty and then leaps, hence the `min`.

**Make them turn, each at its own pace.** This needs something that builds
up, so it goes in `frame`:

```json
"frame": "a = a + dt*(0.1 + 0.5*low_att)\nb = b - dt*(0.2 + 0.7*highmid_att)"
```

and the rings take `"angle": "a"` and `"angle": "b"`. Now each ring turns
faster while its instrument plays and coasts when it rests.

**Bend the motion.** A flat zoom is a plain tunnel. One line of `pixel`
makes rings at different distances turn against each other:

```json
"pixel": "rot = 0.01*sin(rad*5 - time*0.5)"
```

**Let it shed sparks that ride the flow.** Listed after the waves, so they
are born on them:

```json
{ "type": "particles", "emit": "wave", "rate": "60 + 900*beat",
  "flow": 1, "speed": 0.05, "life": 3, "size": 2,
  "color": "palette(i)" }
```

**Finish it.** A dim background for the dark, glow, and the middle kept
from burning out:

```json
"background": { "style": "nebula", "intensity": "0.2 + 0.15*sub_att" },
"post": { "bloom": 0.9, "vignette": 0.5 }
```

with `"darkenCenter": true` in the feedback. Run `trance check` and the
preset is done. Every built-in preset was made in these same steps, and
`trance show` prints any of them: Strata, Clockwork and Pendulum each have
one layer for each of the six ranges.

## 13. When it looks wrong

| What you see | Why | What to do |
|---|---|---|
| It burns out into flat patches | `add` light piling up on a long decay | Give the dense layers `"blend": "max"`, or lower `decay`, the layers' `alpha` or `bloom`; turn on `darkenCenter` if it is the middle. `trance render --json` reports `clipped`, the share at the top |
| It stutters | Too much drawn a frame: a shape with hundreds of copies, many sides and a border, or copies far larger than the screen drawn over each other | `trance render --json` reports `ms` and `vertices` a frame; the built-ins stay under about 40000 vertices. Fewer copies or sides, or no border, cut vertices; smaller shapes cut the time |
| It is nearly black | Decay too low for thin lines, or opacity tied to a range that is silent | Raise `decay` or `thickness`; give the opacity a resting value (`0.3 + 0.5*x`) |
| It jitters | Motion driven by a plain loudness | Use the `_att` one |
| It hardly reacts | The amounts are too small beside the resting values | Double the number in front of the loudness |
| One layer sometimes leaps off the screen | A narrow range that was empty has just sounded | `min(range_att, 2)` |
| It looks the same as every other preset | Too much bloom and too little structure | Lower `bloom`; give the motion a `pixel` flow |
| A hard seam down the screen | `edge` is `wrap` and the two sides don't match | Use `mirror` or `black` |
| The trail is a row of separate copies | The picture moves further each frame than the layer is thick | Add `blur`, slow the motion, or thicken the layer |
| A fade or size along a layer does nothing | It uses `t`, `i` or `v` in a setting other than the color, where they are 0 (`check` warns) | Use `a` in the `point` code instead ([section 8](#8-layers)) |
| `Unknown variable` | A name is read that nothing ever assigns | Assign it in `init` or `frame`, or fix the spelling |

`trance check` names the setting each problem is in, for example
`feedback.zoom: Unknown variable 'basss'`, and unknown settings are
reported as warnings rather than ignored. So is a number past what the engine
draws: `decay` and the opacities are held to 0 to 1, `zoom` and the
stretches to at least 0.01, and the counts (samples, bars, copies, rows,
particles) to the largest the editor offers, so `"decay": 1.2` warns that it
draws as 1. The editor's other ranges are only where its sliders stop; a
number past them does what it says.
