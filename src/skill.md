---
name: trance
description: Write, check, render and switch presets for Trance, the music visualizer for Linux and Windows. Use when asked to make or change a visualizer preset, to match the visualizer to a theme or a song, to render a preset to an image, or to control a running Trance (next preset, lock).
---

# Trance

Trance is a music visualizer. A preset is a JSON file: feedback settings that
bend last frame's picture, layers drawn on top (waves, bars, shapes, terrain,
wireframes, particles), and post effects. Any number can be a formula of the
music (`"1.02 + 0.05*bass_att"`).

Read the format before writing a preset; it is short and complete. The
manual says the same at length, with the reasoning behind each setting and a
preset built step by step:

```sh
trance help format
trance help manual
```

## Working on a preset

```sh
trance list --json                     # every preset: its file, color family, layers, ranges
trance show "Neon Tunnel"              # a preset's JSON, to learn from
trance new "Night Drive" --from Horizon   # prints the new file's path
trance check "Night Drive" --json      # every problem, by setting
trance render "Night Drive" -o look.png --sheet 6 --seconds 8
```

- The user's presets are in `~/.config/trance/presets/`
  (`%LOCALAPPDATA%\trance\presets` on Windows). Built-in presets can't be
  changed; `new --from` copies one.
- Always run `check` after editing; it exits 1 on errors and names the
  setting (`layers.0.color`). Then `render` and look at the picture.
  `--sheet N` tiles N moments so motion is visible in one image, `--audio
  file` reacts to a real track, `--palette name` tries another palette.
- Prefer `palette(...)` and theme color names over fixed hex colors, so the
  preset follows the desktop theme.
- To make the layers of a preset move apart, give each a different range:
  `sub`, `low`, `lowmid`, `highmid`, `presence`, `air` (and `_att` for the
  smoothed ones) sit beside `bass`, `mid` and `treb`. `trance show Strata`
  and `trance show Clockwork` do this with one layer per range.
- A `wireframe` layer is the one thing in three dimensions, and it reads the
  sound differently from every other layer (even steps of pitch between
  `low` and `high`, no decibels), so it is the quickest way to make a preset
  that doesn't look or move like the rest. Give `yaw` a formula such as
  `time*0.2` or it reads as flat. `trance show Whitecaps` is the plain
  sheet, `trance show Scroll` morphs two forms, `trance show Ribbon` bends
  one with `point` code, and `trance show Borehole` puts the camera inside.
- To give each range of the sound a wireframe of its own, set its `low` and
  `high` to the range's Hz (sub 20-60, low 60-200, lowmid 200-600, highmid
  600-2000, presence 2000-6000, air 6000-16000) and drive its height or
  size with the matching `_att`. `trance show Terraces` and `trance show
  Moons` do it six times over.
- Additive layers with a `decay` near 1 and a `zoom` above 1 pile up to
  white. Lower `alpha` or `decay` when a render looks blown out.
- The best presets combine the parts: a flow in `pixel`, a `background`, a
  wave or bars, particles born on them (`"emit": "wave"`) that ride the flow
  (`"flow": 1`), and one palette coloring all of it. `trance show Tidewater`
  and `trance show "Ember Drift"` are short examples.
- `render A --then B --sheet 12` shows how two presets change over.

## Steering a running Trance

```sh
trance next | previous | random
trance preset "Night Drive"
trance mashup          # a new preset from parts of others
trance lock            # hold the current preset, or release it
trance current
trance sources         # what there is to listen to
trance source Spotify  # follow one application's sound only (or: system, microphone)
```

These reach the open window.
