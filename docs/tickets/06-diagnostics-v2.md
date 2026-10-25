# Diagnostics v2: settings, "did you feel it?", stillness, last 10 sessions

## Context

Follow-up to issue #4 (`docs/tickets/04-diagnostics-log.md`). The 1.2.0
diagnostics build was worn for a week (60-min cycle). Result:

- The log **always** showed `60:00 END` exactly on time — also on the 3–4 days
  when the user felt **no** vibration at 60 min. So the app now delivers every
  marker on time; the vibration is lost *after* `vibes_enqueue_custom_pattern`.
- Leading hypothesis (from the PebbleOS source, `src/fw/services/stationary/service.c`
  and `services_common/service.c` in github.com/coredevices/PebbleOS): firmware
  **Stand-By Mode** ("stationary mode") checks the accelerometer once a minute;
  after **30 consecutive motionless minutes** (sum of |Δx|+|Δy|+|Δz| between
  one-minute samples < 50) it switches to `RunLevel_Stationary`, in which the
  **vibe service is disabled** — every vibe request is silently dropped. Any
  motion or button press wakes it; it then re-engages after only 5 still
  minutes. A motionless meditator hits this; tiny movements explain why it is
  intermittent. Apps cannot query or inhibit it.

This ticket extends the `MARKER_DIAG` build so each sitting records enough to
confirm or refute that, and so the user doesn't have to photograph the log
after every sitting. Everything stays `MARKER_DIAG`-only: normal builds must be
byte-for-byte unaffected (same check as ticket 04).

## Requirements

### 1. Settings line
Each session stores its config (duration, tick interval, halfway on/off,
strength). The log shows it as one line, e.g. `60m tick off half on Med`.

### 2. "Did you feel it?" after each sitting
When a session stops (Back, Back → summary), push a question window for each
marker delivered in that session, oldest first:
`60:00 END` / `felt it?` / `Up = yes  Down = no  Select = not sure`.
Store the answer in that marker's record. After the last question, return to the
summary. Back on a question skips the remaining ones (answers stay "not sure").
English only (debug build). If the session had no markers, ask nothing.

### 3. Context at each marker
Record with each marker (and at START/STOP):
- battery percent and charging flag (`battery_state_service_peek()`),
- phone connected (`connection_service_peek_pebble_app_connection()`),
- Quiet Time active (`quiet_time_is_active()`),
- **still minutes**: the app's own replica of the firmware heuristic — on each
  minute boundary of the session, `accel_service_peek()` and compare with the
  previous minute's sample; if |Δx|+|Δy|+|Δz| < 50 increment a counter, else
  reset it to 0. Record the counter's value at the marker. (A value ≥ 30 means
  the firmware's Stand-By Mode had most likely engaged.) This needs the
  accelerometer running during the session in the diag build:
  `accel_data_service_subscribe(0, NULL)` with the lowest sampling rate
  (`ACCEL_SAMPLING_10HZ`), unsubscribed on stop/unload. Sample only once per
  minute (the session already gets minute ticks; with SECOND_UNIT active near
  a marker, only act on `units_changed & MINUTE_UNIT`). Verify on the SDK docs
  that `accel_service_peek` is valid with a 0-sample subscription; if not, use
  the smallest batch size and ignore the handler.

### 4. History of the last 10 sessions
- One persist key per session slot (keys 100–109, ring buffer; key 110 holds
  the index of the newest slot). Each slot ≤ 256 B (`_Static_assert`).
  Suggested record (9 B): `elapsed u16, due u16, kind u8, felt u8, battery u8
  (bit7 = charging), flags u8 (bit0 quiet, bit1 phone connected), still_min u8`;
  slot header: start time i32, config 4 × u8, count u8 → ~24 records per slot.
  When a slot is full, drop the oldest *marker* record but keep START.
- Ticket 04's keys 100/101 change meaning — migrate by simply ignoring/
  overwriting old data (debug build, no compatibility needed).

### 5. Viewer
Down on the summary opens the newest session; **Select** steps to the next
older one (wrapping), Up/Down scroll. Layout per session:
```
#1 10-25 17:13            (newest = #1)
60m tick off half on Med
start bat 82% ph QT
30:00 HALF yes s29 82%
60:00 END NO s59 81% ph QT
stop 60:58
```
(`s29` = still minutes; `ph` = phone connected; `QT` = Quiet Time;
late markers keep ticket 04's `late Ns` suffix; felt = `yes`/`NO`/`?`.)
Keep it legible at FONT_KEY_GOTHIC_18 or larger; lines must not wrap at 200 px
(check in the emulator).

## Tests

- `tools/test-markers.sh` passes (markers.c untouched).
- Plain `pebble build`: no diag symbols (`arm-none-eabi-nm`), `.pbw` size equal
  to the current plain build (record it before your change).
- `MARKER_DIAG=1 pebble build`: no new warnings.
- Emulator (headless: every pebble command that talks to the emulator gets
  `--emulator emery --vnc`; `pebble kill` takes no flags), **capped at ~3 min,
  no waiting for real markers**: use `SCREENSHOT_FAKE_ELAPSED=1790` so HALF
  (60m cycle with half on, set via settings presses) fires ~10 s after start;
  stop, answer the question with Up, open the log, screenshot; start a second
  short session, stop, answer with Down, open the log, step with Select to
  confirm both sessions show. Screenshot each and view them with Read.

## Acceptance criteria

- The diag build shows settings, per-marker felt answer, battery/phone/QT and
  still minutes, for each of the last 10 sessions, readable on-watch.
- Normal builds byte-for-byte unaffected.
- README "Diagnostics build" section updated (what the fields mean, how to
  answer the questions, how to page sessions).
