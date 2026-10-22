#pragma once
#include <pebble.h>
#include "markers.h"

// On-watch marker diagnostics log, for test builds only. `MARKER_DIAG=1 pebble
// build` (wscript adds -DMARKER_DIAG) records the last session's start, every
// marker delivered (with its due second, so catch-up lateness shows) and the
// stop, persisted on every record and readable with Down on the summary screen.
// Without MARKER_DIAG every call below is an empty inline stub and diag.c
// compiles to nothing, so normal builds carry none of it.

#ifdef MARKER_DIAG

// New session: clears the log and records START (`start` = wall-clock time,
// `elapsed` = the starting second, non-zero only in fake-elapsed builds).
void diag_session_start(time_t start, int elapsed);
// A marker was delivered at `elapsed`; it was due at second `due`.
void diag_marker(MarkerEvent ev, int due, int elapsed);
// The user stopped the session (summary reached).
void diag_stop(int elapsed);
// The session window is closing: records an abnormal exit unless STOP was logged.
void diag_close(int elapsed);
// Push the scrollable log window.
void diag_window_push(void);

#else

static inline void diag_session_start(time_t start, int elapsed) {}
static inline void diag_marker(MarkerEvent ev, int due, int elapsed) {}
static inline void diag_stop(int elapsed) {}
static inline void diag_close(int elapsed) {}

#endif
