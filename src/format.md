# Writing a Trance preset

This is the short reference. `trance help manual` (docs/manual.md) explains
each setting at length and builds a preset step by step.

A preset is one JSON file. Trance's own are built in; yours live in
`~/.config/trance/presets/` (`%LOCALAPPDATA%\trance\presets` on Windows).
Start one with `trance new "My Preset"` (add `--from "Neon Tunnel"` to copy
an existing one), check it with `trance check "My Preset"`, and see it
without opening a window with `trance render "My Preset" -o look.png`.

```json
{
  "name": "My Preset",
  "palette": "theme",
  "feedback": { "decay": 0.96, "zoom": "1.02 + 0.03*bass_att" },
  "background": { "style": "nebula", "intensity": 0.3 },
  "layers": [
    { "type": "wave", "shape": "circle", "color": "palette(t + time*0.1)" },
    { "type": "particles", "emit": "wave", "flow": 1 }
  ],
  "post": { "bloom": 0.8 }
}
```

## How a frame is made

1. Last frame's picture is bent and faded by the `feedback` settings (and the
   `pixel` code, if any). This is the flow: MilkDrop's warp, G-Force's
   FlowField.
2. Each of the `layers` is drawn on top, in order.
3. The result goes to the screen through the `post` settings (and the `shade`
   code, if any), with the `background` filling what is left dark.

Because step 1 starts from the previous frame, anything drawn leaves a trail
that the feedback keeps moving: zoom above 1 makes a tunnel, a little `rot`
makes it spiral, `decay` decides how long it lasts.

The parts are meant to work on each other. A wave or the bars give particles
somewhere to be born (`emit`), the flow carries the particles along with the
picture (`flow`), `vectors` draws the flow itself, and the palette colors all
of it. A preset that has all of them at once is usually the better for it.

When one preset gives way to another, both run on the same picture for a few
seconds: the flow and the look change hands place by place, layers fade
across, and each wave reshapes itself into the next preset's wave. Nothing
in a preset has to allow for this.

## Formulas

**Any number may be written as a formula in quotes instead.** A formula is
recomputed every frame, which is how settings follow the music.

Variables:

| Name | Meaning |
|---|---|
| `bass` `mid` `treb` `vol` | Loudness of each range right now. 1 is normal for the current song, 2 is a hard hit, 0 is silence |
| `bass_att` `mid_att` `treb_att` `vol_att` | The same, smoothed. Use these for motion, the plain ones for flashes |
| `sub` `low` `lowmid` `highmid` `presence` `air` | Six narrower ranges, measured the same way, for giving each layer a part of the sound of its own: 20–60 Hz (rumble), 60–200 (kick and bass line), 200–600 (warmth), 600–2000 (voices and leads), 2000–6000 (snares, the bite of a guitar), 6000–16000 (cymbals) |
| `sub_att` … `air_att` | The same, smoothed |
| `beat` | Jumps to 1 on a beat and falls back to 0 in about half a second |
| `beats` | How many beats so far |
| `time` `dt` `frame` `fps` | Seconds since start, seconds per frame, frame number, frame rate |
| `aspect` | Width divided by height |
| `progress` | 0 to 1 through the time before the next automatic preset change |
| `pi` `tau` | 3.14159…, 6.28318… |

Operators: `+ - * / %`, `^` (power), comparisons, `&&`, `||`, `!`, and
`condition ? a : b`. Dividing by zero gives 0.

Functions: `sin cos tan asin acos atan atan2(y, x)`, `abs sign floor ceil
fract int sqrt sqr exp log pow(x, y)`, `min max mod clamp(x, low, high)
mix(a, b, t) step(edge, x) smoothstep(a, b, x)`, `if(c, a, b) above(a, b)
below(a, b) equal(a, b) band bor bnot`, `rand()` (0 to 1) or `rand(n)`,
`noise(x)` or `noise(x, y)` (smooth, 0 to 1), `length(x, y)`, `tri(x)`
(triangle wave).

## Coordinates

The middle of the screen is (0, 0). `y` runs from -1 at the bottom to 1 at
the top; `x` from `-aspect` on the left to `aspect` on the right, so a circle
is always round. Angles are in radians, counter-clockwise. Sizes named in
pixels (`thickness`, `size`, `borderWidth`) are for a 1080-line screen and
scale with the window.

## Code

Four optional blocks, each a string (statements separated by line breaks or
`;`) or a list of strings. Assigning to a new name makes a variable every
later formula can read.

| Block | Runs | Use |
|---|---|---|
| `init` | once, when the preset starts | starting values. Roll them with `rand()` and the preset is a little different every time it comes round |
| `frame` | every frame, before the layers | your own variables; it may also overwrite feedback settings such as `zoom` |
| `pixel` | for every pixel of the feedback step | reads `x y rad ang` (distance and angle from the middle) and may change `zoom rot dx dy sx sy cx cy warp decay` there, or `x`/`y` to read from somewhere else |
| `shade` | for every pixel on the way to the screen | reads `x y rad ang lum` and changes `r g b` |

```json
"frame": "spin = spin + 0.02*bass_att",
"pixel": "rot = 0.03*sin(rad*6 - time); zoom = zoom + 0.02*cos(ang*3 + spin)"
```

## feedback

Motion values are per frame at 60 fps (Trance adjusts for other rates).

| Setting | Default | |
|---|---|---|
| `decay` | 0.96 | How much of the last frame survives. 1 never fades |
| `zoom` | 1 | Above 1 pushes outward, below 1 pulls inward |
| `zoomexp` | 1 | Above 1 zooms the edges more than the middle |
| `rot` | 0 | Turn, in radians |
| `warp` `warpScale` `warpSpeed` | 0, 1, 1 | A slow liquid wobble: how much, how large, how fast |
| `cx` `cy` | 0 | The point zoom and rotation work around |
| `dx` `dy` | 0 | Slide |
| `sx` `sy` | 1 | Stretch |
| `blur` | 0 | 0 to 1, softens the trail |
| `hueshift` | 0 | Turns the trail's colors a little every frame |
| `edge` | `wrap` | What lies beyond the screen: `wrap`, `mirror`, `clamp` or `black` |
| `darkenCenter` | off | A soft dark spot in the middle every frame, so a zoom doesn't burn out the centre |

For a flow that is different everywhere, write it in `pixel`. Two shapes
cover most of them: currents, `dx = 0.005*sin(y*4 + time)`, and whirlpools,
`rot = 0.02*sin(rad*5 - time)`.

## background

What shows where the picture is dark. It is drawn fresh every frame, behind
the feedback picture, so it never smears; in glass mode the desktop takes its
place.

| Setting | Default | |
|---|---|---|
| `style` | `none` | `gradient`, `nebula`, `stars`, `grid` (a floor running to the horizon), `rays`, `plasma` |
| `intensity` | 0.4 | How bright. `"0.3 + 0.3*bass_att"` makes it breathe |
| `tint` `spread` | 0, 0.3 | Where on the palette its colors start, and how much of the palette it spans |
| `scale` `speed` | 1, 1 | Size of its features, and how fast it moves |

## layers

Every layer has `type`, and may have `enabled` and `blend`: `over` covers
what is under it, `add` adds light to it, `max` keeps the brighter of the two
and so never grows past the layer's own color (the cure for a dense layer
that burns out). Waves, terrain, wireframes, vectors and particles
default to `add`, bars and shapes to `over`. Light that piles up keeps its
hue: a hot spot is its own color at full strength, white only where the
color is. Colors are described below.

Where a layer's `color` is said to take `t`, `i` or `v`, only the color and
the `point` code see them. Every other setting, `alpha` included, is worked
out once a frame for the whole layer, where they are always 0, and `check`
warns about them there. Particles already fade with age and terrain with
distance. A shape's copies are the exception: there every setting is worked
out per copy.

**wave** draws the sound as a line.
`source` (`mix`, `left`, `right`, `spectrum`), `shape` (`line`, `circle`,
`xy` for left against right, `custom` for point code only), `samples` (8–512),
`scale`, `smooth` (0–1), `x`, `y`, `width`, `radius`, `angle`, `thickness`,
`color`, `alpha`, `dots`, `mirror` (`none`, `x`, `y`, `quad`), and `point`:
code run for every point with `t` (0–1 along the line), `i` (its number), `v`
(the sample: -1 to 1, or 0 to 1 for the spectrum), `v2` (the other channel),
which may change `x y r g b a`.

**bars** draws the spectrum as bars.
`layout` (`bottom`, `top`, `center`, `radial`), `count`, `x`, `y`, `width`,
`height`, `gap`, `radius` and `angle` (radial), `color`, `alpha`, `fade` (how
dark the foot of a bar is), `peaks`, `symmetric` (bass in the middle), and
`point`: code per bar with `t i v`, which may change `v r g b a`.

**shape** draws polygons.
`sides` (3–64; many sides make a circle), `instances` (copies), `x`, `y`,
`radius`, `angle`, `color` and `alpha` (middle), `color2` and `alpha2` (rim),
`border`, `borderAlpha`, `borderWidth`, `textured` (fill it with last frame's
picture) with `texZoom` and `texAngle`, and `code`. With several instances,
every setting and the code run once per copy with `n` (0, 1, 2…), `count`,
`t` (0–1 across the copies), and `v`/`v2` (the spectrum and waveform at that
position); the code may change `x y radius angle r g b a r2 g2 b2 a2`.

**terrain** draws the spectrum as a landscape scrolling to the horizon.
`style` (`mesh`, `lines`, `dots`), `rows`, `cols`, `speed`, `height`,
`width`, `y` (front edge), `horizon`, `depth`, `thickness`, `color` (with
`t` across, `v` height, `i` distance 0–1), `alpha`.

**wireframe** hangs the spectrum's last few seconds in space as a sheet of
lines, with pitch across it and time along it (SoundSpectrum's WhiteCap).
`form` (`sheet`, `tube`, `ring`, `sphere`), `form2` and `morph` (0–1 from
the one form to the other), `width` and `depth` (sheet; `depth` is also the
tube's length), `radius` (tube, ring, sphere), `height`. The camera: `yaw`
(turn; `"time*0.2"` keeps it turning), `pitch` (tip toward you), `roll`,
`distance` (close exaggerates depth; inside the form's size it is a tunnel),
`size`, `x`, `y`. Drawing: `style` (`mesh`, `lines`, `strands`, `dots`),
`thickness`, `fog` (dims the far side), `color` (with `t` pitch 0–1, `i` age
0–1, `v` height), `alpha`, and `point`: code per point with `t i v`, which
may change `x y z` (right, up, away, before the camera) and `r g b a`.
When either form closes on itself (all but `sheet`), pitch runs up one half
of the sheet and back down the other, so the edges meet without a step.

It reads the sound its own way, so it moves differently from everything
else: `bins` of equal width in Hz between `low` and `high` (each the average
of what falls in it), loudness as it is instead of in decibels, `tilt`
weighting the top of that stretch up, `smooth` between neighbours, and the
loudest bin of late made 1. A bin rises at once and sinks over `fade`
seconds. Bars and terrain show everything that is sounding; this shows what
stands out. `rows` samples are kept, `speed` taken a second.

**particles** emits sparks.
`count` (most alive at once), `rate` (per second; `"50 + 2000*beat"` bursts
on the beat), `x`, `y`, `area`, `speed`, `angle`, `spread`, `gravity`, `drag`
(negative speeds them up), `life`, `size`, `color` (with `t` age 0–1, `i` a
random number fixed per particle, `v` speed), `alpha`. And what makes them
part of the picture:

- `emit`: where they are born. `point` (at `x`, `y`, within `area`), `wave`
  (along the wave layers drawn before this one, mostly where they are loud,
  or along a wireframe's newest sample),
  `bars` (on the tips of the bars), `edges` (the screen's border, heading
  for `x`, `y`), `screen` (anywhere).
- `flow`: 1 carries them exactly as the feedback carries the picture, so
  they ride its currents; 0 leaves them alone.
- `field`, `fieldScale`, `fieldSpeed`: a slowly changing field of currents of
  their own to steer by. Strength, size of its swirls, how fast it changes.
- `pull` and `swirl`: toward and around `x`, `y`. Negative `pull` pushes
  away; `"-3*beat"` scatters them on the beat.
- `edge`: `wrap` or `bounce` at the screen's border (default `none`).
- `streak`: draw each as a line this many seconds long, back the way it came.

**vectors** draws the flow as a grid of strokes (MilkDrop's motion vectors).
`cols`, `rows`, `length`, `thickness`, `color` (with `t` across, `i` up, `v`
how fast the picture moves there), `alpha`, `dots`.

## Colors

A color is text, one of:

- `"#ff2d95"`, `"white"`, `"black"`
- a theme color, which follows the desktop theme live: `accent`, `foreground`,
  `background`, `red`, `orange`, `yellow`, `green`, `cyan`, `blue`,
  `magenta`, `bright_red` … `bright_magenta`, `muted`, `selection`
- `"palette(formula)"`: a point on the preset's palette, which wraps around,
  so `palette(t + time*0.1)` is a moving rainbow of the palette's colors
- `"hsv(h, s, v)"` or `"rgb(r, g, b)"`, each part a formula from 0 to 1

`palette` at the top of the preset is a name (`theme`, `accent`, `fire`,
`ocean`, `neon`, `sunset`, `aurora`, `vapor`, `acid`, `ice`, `gold`,
`rainbow`, `magma`, `inferno`, `viridis`, `lagoon`, `forest`, `dusk`,
`arctic`, `amethyst`, `copper`, `cyber`, `phosphor`, `candy`, `sepia`,
`steel`, `rose`, `abyss`, `tropic`, `twilight`, `thermal`, `nova`, `jade`,
`retro`, `carnival`, `phase`, `crimson`, `sulfur`, `cobalt`, `bauhaus`,
`citrus`, `opal`, `cividis`, `noir`, `storm`, `spectral`, `reef`, `hologram`,
`blacklight`, `cyanotype`, `sakura`, `autumn`, `riso`, `turbo`, `contour`,
`inkwell`, `solstice`, `lichen`, `washi`, `biolume`, `vesper`, `peacock`,
`mesa`, `topo`, `duotone`, `mono`) or a list of colors: `["#000000", "accent", "#ffffff"]`.
`theme` and `accent` are built from the desktop theme. Presets that color
everything with `palette(...)` look right under any palette the user picks
with the T key.

## post

| Setting | Default | |
|---|---|---|
| `colorize` | 0 | 0 to 1: replace colors with the palette, dark to bright. Draw in white and let this color it (G-Force's ColorMap) |
| `colorShift` `colorCurve` | 0, 1 | Slide that mapping along the palette (`"0.05*sin(time*0.1)"`), and bend it: a curve above 1 keeps the dark colors longer |
| `bloom` `bloomThreshold` | 0.5, 0.55 | Glow, and how bright something must be to glow |
| `brightness` `contrast` `saturation` `gamma` | 1 | |
| `hue` | 0 | Turn all colors, in radians |
| `invert` | 0 | 0 to 1 |
| `mirror` | `none` | `x`, `y` or `quad` |
| `kaleido` `kaleidoAngle` | 0 | Number of mirrored wedges (2 or more), and their turn |
| `echoAlpha` `echoZoom` `echoFlip` | 0, 1, `none` | A second, scaled copy over the first |
| `chroma` | 0 | Color fringing toward the edges |
| `vignette` | 0.3 | Darker corners |
| `grain` `scanlines` | 0 | Film grain, CRT lines |

## Checking your work

`trance check` reports every problem with the setting it is in, for example
`feedback.zoom: Unknown variable 'basss'`, and exits 1 if the preset can't
run. Unknown settings are warnings, and so is a number the engine holds to a
range it is past (`"decay": 1.2` draws as 1). `trance render <preset> --sheet 6`
tiles six moments of the preset into one picture, and `--audio song.mp3`
renders against real music instead of the built-in test loop. The window's
editor (E) changes the same file's settings live; its JSON tab shows this
format.
