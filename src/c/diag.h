#pragma once
#include <pebble.h>
#include "markers.h"

// On-watch marker diagnostics log, for test builds only. `MARKER_DIAG=1 pebble
// build` (wscript adds -DMARKER_DIAG) records, for each of the last 10 sessions,
// its settings, the start, every marker delivered (with its due second, so
// catch-up lateness shows) and the stop, each with battery / phone / Quiet Time
// / still-minutes context. Persisted on every record; after a sitting the user
// is asked "felt it?" per marker, and the history is readable with Down on the
// summary screen (Select = next older session). The settings menu also offers a
// vibration self-test whose result is stored as one more history entry.
// Without MARKER_DIAG every call below is an empty inline stub and diag.c
// compiles to nothing, so normal builds carry none of it.

#ifdef MARKER_DIAG

// New session: takes the next history slot, starts the accelerometer sampling
// used for still-minutes, and records START (`start` = wall-clock time,
// `elapsed` = the starting second, non-zero only in fake-elapsed builds).
void diag_session_start(time_t start, int elapsed);
// A marker was delivered at `elapsed`; it was due at second `due`.
void diag_marker(MarkerEvent ev, int due, int elapsed);
// A wall-clock minute boundary passed: sample the accelerometer for the
// still-minutes counter. Call only when the tick's units_changed has MINUTE_UNIT.
void diag_minute(void);
// The user stopped the session (summary reached): records STOP, stops the
// accelerometer and pushes the "felt it?" questions (none if no markers fired).
void diag_stop(int elapsed);
// The session window is closing: records an abnormal exit unless STOP was logged.
void diag_close(int elapsed);
// Push the scrollable history window.
void diag_window_push(void);
// Save a vibration self-test result as its own history entry: `pattern` was
// played `total` times and the user felt `felt` of them.
void diag_test_save(MarkerEvent pattern, int felt, int total);
// Push the vibration self-test window (vibe_test.c).
void vibe_test_push(void);

#else

static inline void diag_session_start(time_t start, int elapsed) {}
static inline void diag_marker(MarkerEvent ev, int due, int elapsed) {}
static inline void diag_minute(void) {}
static inline void diag_stop(int elapsed) {}
static inline void diag_close(int elapsed) {}

#endif
