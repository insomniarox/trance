# Known issues

Open points from the audit of November 2026. None of them is urgent.

## Open

- **Window rounding misses runtime changes.** Trance asks Hyprland for
  `decoration:rounding` at startup and again when Hyprland reports
  `configreloaded`. A change made with `hyprctl keyword decoration:rounding`
  sends no such event, so the corners keep the old radius until the next
  config reload (`src/theme.cpp`).
- **The Hyprland event connection is not retried.** If the event socket
  (`.socket2.sock`) can't be reached at startup, or drops later, rounding is
  never refreshed again for the life of the window. A rounding query that
  Hyprland accepts but never answers is not timed out either, so its socket
  stays open. Both need Hyprland itself to be misbehaving (`src/theme.cpp`).
- **"Waiting for sound from ..." doesn't refresh when the sources arrive.**
  The message calls `backend.sourceLabel()`, which has no change signal, so
  the binding is not re-evaluated when PipeWire's list of sources comes in.
  For a device it can show the raw node name until the silence message is
  next rebuilt (`src/Main.qml:358`).
- **`Backend` does a lot.** It holds the settings, the editing and undo, the
  preset library, the audio, the titles and the mash-ups, and every setting
  shares one `settingsChanged` signal, so changing any one re-evaluates every
  settings binding. If it grows, the settings are the first thing to move
  into a class of their own (`src/backend.h`).

## Kept on purpose

- **The transition buffers stay allocated.** The two buffers a preset change
  draws with are kept for the life of the window, about 44 MB of graphics
  memory at 1440p, because creating them when a change starts is a visible
  stutter (`src/engine.cpp`).
