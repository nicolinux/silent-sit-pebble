# On-watch marker diagnostics log (test builds only)

## Context

Part of the v1.2.0 plan (`docs/tickets/00-plan.md`). Depends on ticket 02.

The missing-buzz bug only shows on real hardware over real hour-long sittings,
and the developer connection (for `pebble logs`) is unreliable on the iOS app
with Pebble Time 2. To confirm ticket 02's fix on the wrist we need the watch
itself to record what happened, readable after the sitting, with no phone or
laptop attached.

## Design

Compiled **only** when `MARKER_DIAG` is defined, exactly like
`SCREENSHOT_FAKE_ELAPSED`: `wscript` adds `-DMARKER_DIAG` when the env var
`MARKER_DIAG=1` is set. A normal `pebble build` must contain none of this.

### Recording
- New module `src/c/diag.c/.h` (all bodies `#ifdef MARKER_DIAG`; with it
  undefined, the API is no-op inline stubs or the calls are `#ifdef`-guarded).
- Fixed-size record, e.g.
  `{ uint16_t elapsed_s; uint16_t due_s; uint8_t kind; }` where kind ∈
  {START, TICK, HALF, END, STOP}. `elapsed_s` = when delivered, `due_s` = the
  marker's scheduled second (lateness = difference).
- Ring buffer of the last ~40 records for the **most recent session only**
  (cleared at session start), persisted with `persist_write_data` under a new
  key (`PKEY_DIAG 100`), keeping each write ≤ `PERSIST_DATA_MAX_LENGTH` (256 B;
  split across 2 keys if needed). Persist on every record so a crash or forced
  exit still leaves the log.
- Hook points: session start (`window_load`), `fire_marker` (the single
  delivery point from ticket 02 — record due vs actual), `enter_summary` (STOP),
  and `window_unload` if no STOP was recorded (records an abnormal exit).
- Also record the session start wall-clock time (one `int32` in its own key)
  so the view can show it.

### Viewing
- On the **summary** screen, pressing **Down** pushes a simple scrollable
  window (a `ScrollLayer` + `TextLayer`, or a `MenuLayer` with one row per
  record) listing lines like:
  `start 07:02`, `30:00 HALF`, `60:00 END`, `61:45 END late 105s`, `stop 72:10`.
  English only, no localization needed (debug build).
- Only wired up in `MARKER_DIAG` builds; normal builds keep Down unused there.

## Tests

- Emulator with `MARKER_DIAG=1 SCREENSHOT_FAKE_ELAPSED=3540`: start a session,
  wait ~70 s, stop (Back, Back), press Down, screenshot the log view. It must
  show `start`, `60:00 END`, `stop`. Attach the screenshot to the PR.
- Normal `pebble build`: confirm the diag code is absent
  (`arm-none-eabi-nm build/emery/pebble-app.elf | grep -i diag` is empty) and
  the `.pbw` size is unchanged within a few bytes.

## Acceptance criteria

- `MARKER_DIAG=1 pebble build` produces a sideloadable `.pbw` whose log survives
  closing and reopening the app and is readable from the summary screen.
- Normal builds are byte-for-byte unaffected by the feature's logic.
- README gains a short *Diagnostics build* subsection: how to build it, how to
  sideload it on iPhone (AirDrop the `.pbw` → open in the Pebble app), how to
  read the log.
