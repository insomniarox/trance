# Known issues

Open points from the audit of November 2026. None of them is urgent.

## Open

- **`Backend` does a lot.** It holds the settings, the editing and undo, the
  preset library, the audio, the titles and the mash-ups, and every setting
  shares one `settingsChanged` signal, so changing any one re-evaluates every
  settings binding. If it grows, the settings are the first thing to move
  into a class of their own (`src/backend.h`).

## Kept on purpose

- **Runtime rounding changes show when Trance is next focused.** Hyprland
  sends no event when rounding is changed while it runs (`hyprctl eval`, or
  `hyprctl keyword` on older versions), so Trance asks again each time its
  window becomes active rather than polling or listening to every focus
  change on the desktop. A config reload is still picked up at once
  (`src/theme.cpp`).

- **The transition buffers stay allocated.** The two buffers a preset change
  draws with are kept for the life of the window, about 44 MB of graphics
  memory at 1440p, because creating them when a change starts is a visible
  stutter (`src/engine.cpp`).
