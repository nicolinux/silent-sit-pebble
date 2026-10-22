# Add a "Halfway buzz" on/off setting

## Context

Part of the v1.2.0 plan (`docs/tickets/00-plan.md`). Do after ticket 02 (same
files).

Today the HALF marker (medium double buzz at mid-cycle) is always on and
invisible in the UI. A user with "60 min, no ticks" expects exactly one buzz per
hour and is surprised by the buzz at 30 min. Make it a setting and show it on
the home screen.

## Changes

### Config & persistence (`src/c/markers.h`, `src/c/app.h`, `src/c/main.c`)
- Add `uint8_t half_enabled;` (0/1) to `SilentSitConfig`, with a comment.
- `#define PKEY_HALF 5` and `#define DEFAULT_HALF 0` — default **off**
  (maintainer's decision: "no ticks" should mean one buzz per cycle). This
  applies to existing users too: after updating, a config with no persisted
  `PKEY_HALF` gets halfway **off**; the release notes (ticket 05) must say so.
- Load in `config_load()` (missing key → default), save in `config_save()`.

### Scheduler (`src/c/markers.c`)
- `marker_for_elapsed`: `is_half` only when `cfg.half_enabled`.
- `next_marker_after`: skip the half candidate when disabled.
- `latest_marker_in` (from ticket 02) must respect it too (it should, if built
  on the two functions above).
- Update the explanatory comments.

### Settings menu (`src/c/settings_window.c`)
- New row **"Halfway buzz"** directly after *Tick interval*:
  Duration, Tick interval, **Halfway buzz**, Vibration, Reset total
  (`NUM_ROWS` 5; renumber `ROW_*`; update the header comment's row list).
- Subtitle `"on"` / `"off"` (universal tokens, untranslated). SELECT toggles
  and persists via `config_save()` like the other value rows.
- When toggled **on**, play a preview of the half pattern at the current
  strength (add `vibe_preview_half(uint8_t strength)` next to `vibe_preview` in
  `session_window.c/.h`, reusing `vibe_half`'s segments). Toggling off: no buzz.

### Home summary (`src/c/setup_window.c`, `src/c/locale.c/.h`)
The summary must make the halfway buzz visible. Use four strings (all six
languages), chosen by (ticks on/off) × (half on/off), e.g. EN:
- `"%d min, a tick every %d min, halfway buzz"`
- `"%d min, a tick every %d min"`  (half off — existing `MSG_HOME_SUMMARY`)
- `"%d min, no ticks, halfway buzz"`
- `"%d min, no ticks"`             (half off — existing `MSG_HOME_SUMMARY_OFF`)

Wording may be tuned, but keep it short: it must fit the home layout
(`relayout()` measures height; check wrapping on screen). Grow `s_summary`
beyond 64 bytes if any language needs it (UTF-8 accents count as 2 bytes).
Add the settings-row label `MSG_HALF_BUZZ` ("Halfway buzz") in all languages.

## Tests

- Host tests (`test/markers_test.c`): with `half_enabled = 0`, 60/0 yields
  `3600 END, 7200 END` only; 60/10 yields ticks at 1800/5400 instead of HALF;
  45/30 yields `1800 TICK, 2700 END, 3600 TICK`; with `half_enabled = 1` all
  existing expectations unchanged. Update existing test configs to set the new
  field explicitly.
- Emulator: script derived from `tools/generate-screenshots.sh` that opens
  settings, toggles the new row, returns home, and screenshots both — verify the
  row and the summary in **EN** at minimum; check the longest language string
  doesn't overflow (temporarily force the locale if needed, then revert).
- Fake-elapsed run (`SCREENSHOT_FAKE_ELAPSED=1770`) with half off: **no** buzz
  logged at 1800 s for a 60/off config.

## Acceptance criteria

- New setting persists across app restarts; default **off**; existing configs
  upgrade cleanly (no persisted key → off).
- With the default config after a factory reset (30 min / 10 min tick / half
  off) the home summary reads exactly as today: `30 min, a tick every 10 min`.
- With half off, no HALF marker is ever delivered; with it on, behaviour is
  identical to before.
- Home summary reflects all four combinations; settings row shows on/off.
- `tools/test-markers.sh` passes; `pebble build` has no new warnings.
