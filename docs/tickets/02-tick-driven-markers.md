# Fix missing buzzes: drive markers from tick_timer_service with catch-up

## Context

Part of the v1.2.0 plan (`docs/tickets/00-plan.md`). Depends on ticket 01
(host tests).

**Bug:** on a real Pebble Time 2 (60-min cycle, ticks off), END and/or HALF
buzzes are intermittently never delivered, although the app stays in the
foreground and the on-screen minute counter keeps counting correctly.

**Cause (high confidence):** `src/c/session_window.c` schedules each marker
with a single long `app_timer` (up to 30 min), re-armed only inside its own
callback (`schedule_next_marker()` / `marker_timer_cb()`). A lost timer loses
that marker and kills every later marker in the sitting. Meanwhile the readout
uses `tick_timer_service` (RTC-driven), which is reliable on the device.

Secondary issues fixed by the same change:
- an `app_timer` firing slightly early re-arms for the same marker → double buzz;
- a backwards wall-clock correction (phone time sync) can re-fire a marker;
- readout and markers run on two different clocks.

## Design

Remove the marker `app_timer` entirely. One handler services both the readout
and the markers, driven only by `tick_timer_service`:

- Normally subscribe at `MINUTE_UNIT` (cheap — exactly what runs today).
- When the next marker is **≤ 60 s away**, resubscribe at `SECOND_UNIT` so it
  fires on its exact second; after it fires and the next marker is > 60 s away,
  drop back to `MINUTE_UNIT`. (Minute ticks are 60 s apart, so a marker > 60 s
  away at one minute tick is still in the future at the next one — it can never
  be overshot.) Only call `tick_timer_service_subscribe` when the unit actually
  changes.
- **Catch-up:** remember the elapsed second of the last marker delivered
  (`s_last_marker_sec`, starts at 0). On every service call, if a marker lies in
  `(s_last_marker_sec, elapsed]` it is due: fire **one** buzz for the **latest**
  such marker (never a burst), and set `s_last_marker_sec` to that marker's
  second. This makes a dropped/late wake-up deliver the buzz late instead of
  never, and makes double-firing impossible (a marker second is never fired
  twice, even if the wall clock steps backwards).

### Pure helper in `markers.c` / `markers.h`

Add (keep it pure, document it in the header in the same style):

```c
// The latest marker second m with after < m <= elapsed, or -1 if none; its
// event (precedence applied) goes to *out_event. Drives catch-up: a marker
// whose wake-up was missed is still found and delivered (once) late.
int latest_marker_in(int after, int elapsed, SilentSitConfig cfg, MarkerEvent *out_event);
```

Implementation may loop over `next_marker_after()`; it must stay bounded and
cheap even for a multi-hour gap (e.g. compute directly, or cap iterations —
6–12 markers/hour max).

### Runtime in `session_window.c`

Replace `schedule_next_marker`, `marker_timer_cb`, `s_marker_timer`,
`s_next_event` with something like:

```c
static void service_session(void) {
  int elapsed = clamp(time(NULL) - s_start_time, >= 0);
  update_minutes(elapsed);
  MarkerEvent ev;
  int due = latest_marker_in(s_last_marker_sec, elapsed, g_config, &ev);
  if (due >= 0) { fire_marker(ev, elapsed); s_last_marker_sec = due; }
  int next = next_marker_after(max(elapsed, s_last_marker_sec), g_config, NULL);
  set_tick_unit(next >= 0 && next - elapsed <= 60 ? SECOND_UNIT : MINUTE_UNIT);
}
```

- Call it from `window_load` (after anchoring `s_start_time`) and from the tick
  handler.
- Keep the existing state machine: markers keep running in `S_CONFIRM`;
  `enter_summary()` and `window_unload()` unsubscribe the tick service.
- Log late deliveries: `APP_LOG(DEBUG, "marker %s @ %ds (due %ds)")` — keep the
  existing log style. (Ticket 04 adds a persistent on-watch log; don't build
  that here, but keep `fire_marker` the single place a marker is delivered so
  04 can hook it.)
- `SCREENSHOT_FAKE_ELAPSED` back-dates the start; with catch-up, that must
  **not** buzz on session start for markers "missed" before the fake start.
  Initialise `s_last_marker_sec` to the elapsed value at start (i.e. markers in
  the past at start are considered delivered). Also correct for normal builds
  (elapsed is 0 there).
- Update the file's header comment ("Battery-friendly scheduling …") to
  describe the new mechanism; remove references to the marker app_timer.

## Tests

- Extend `test/markers_test.c` (ticket 01) for `latest_marker_in`:
  none due; exactly one due; several due → returns the latest with correct
  precedence; `after == elapsed` → -1; boundary `m == elapsed` included,
  `m == after` excluded; multi-hour gap stays correct.
- Emulator: build with `SCREENSHOT_FAKE_ELAPSED=3540` (59 min) and a script
  derived from `tools/generate-screenshots.sh` that sets config 60/off (or wipes
  persist and accepts defaults, then cycles settings with `press`), starts a
  session, and runs `pebble logs` for ~90 s. Expect exactly one
  `marker END @ 3600s`. Repeat with `1770` → one `HALF @ 1800s`. Paste the log
  lines in the PR.

## Acceptance criteria

- No `app_timer` is used for markers; `tools/test-markers.sh` passes;
  `pebble build` has no new warnings.
- Emulator logs show END at 3600 s and HALF at 1800 s on the exact second, each
  exactly once, with no buzz at session start in fake-elapsed builds.
- Behaviour/precedence of markers is otherwise unchanged.
- README *Architecture* paragraph about the scheduler updated to match.
