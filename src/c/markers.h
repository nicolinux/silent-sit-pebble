#pragma once
#include <pebble.h>

// How forceful the marker buzzes feel. Intensity is fixed by hardware, so each
// level lengthens the "on" pulses (see session_window.c) rather than changing
// amplitude. Order matters: it is the cycle order in the settings screen.
typedef enum {
  VIBE_LIGHT = 0,   // short, gentle pulses
  VIBE_MEDIUM,      // the default
  VIBE_STRONG,
  VIBE_STRENGTH_COUNT,
} VibeStrength;

// The seduta configuration: a cycle length and a periodic tick interval.
// Both in minutes. interval_min == 0 means "tick off".
typedef struct {
  uint8_t duration_min;   // cycle length: 10|15|20|30|45|60
  uint8_t interval_min;   // periodic tick: 0|10|30  (0 = off)
  uint8_t vibe_strength;  // buzz strength: VIBE_LIGHT|VIBE_MEDIUM|VIBE_STRONG
  uint8_t half_enabled;   // 0|1: buzz at mid-cycle (0 = off, so "no ticks" = one buzz per cycle)
} SilentSitConfig;

// What (if anything) happens at a given second of the seduta.
typedef enum {
  MARKER_NONE = 0,
  MARKER_TICK,   // periodic tick   -> single light pulse
  MARKER_HALF,   // mid-cycle (only if half_enabled) -> triple
  MARKER_END,    // end of cycle    -> triple, then the cycle restarts
} MarkerEvent;

// Pure function of (elapsed seconds, config): the marker firing at exactly
// `elapsed`. Everything — tick/half/end, the infinite loop, and collision
// precedence (end > half > tick; no half if disabled) — is derived here from absolute elapsed time,
// so there is zero drift across long sedute.
MarkerEvent marker_for_elapsed(int elapsed, SilentSitConfig cfg);

// The elapsed-second of the next marker strictly after `elapsed`, with its
// event type written to *out_event (precedence applied, so a tick coinciding
// with half/end yields HALF/END). Returns -1 if the config has no cycle.
// This tells the runtime how close the next marker is (to pick the tick-service
// unit) and what it will be, always recomputed from absolute time so timing
// never drifts.
int next_marker_after(int elapsed, SilentSitConfig cfg, MarkerEvent *out_event);

// The latest marker second m with after < m <= elapsed, or -1 if none; its
// event (precedence applied) goes to *out_event. Drives catch-up: a marker
// whose wake-up was missed is still found and delivered (once) late. Computed
// directly (latest end, half and tick at or before `elapsed`, take the max), so
// it is O(1) however long the gap — no looping over missed markers.
int latest_marker_in(int after, int elapsed, SilentSitConfig cfg, MarkerEvent *out_event);
