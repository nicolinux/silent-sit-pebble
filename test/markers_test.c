#include <stdio.h>
#include "markers.h"

// Minimal host test harness for the pure scheduler in src/c/markers.c.
// No framework: CHECK() prints file:line on failure and counts it; the exit
// code is non-zero if any check failed. Build/run via tools/test-markers.sh.

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do {                                              \
    g_checks++;                                                       \
    if (!(cond)) {                                                    \
      g_failures++;                                                   \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                 \
  } while (0)

#define HORIZON 7200   // seconds covered by the sequence tests

static SilentSitConfig make_cfg(int duration, int interval) {
  SilentSitConfig cfg;
  cfg.duration_min = (uint8_t)duration;
  cfg.interval_min = (uint8_t)interval;
  cfg.vibe_strength = VIBE_MEDIUM;
  return cfg;
}

typedef struct { int at; MarkerEvent ev; } Expected;

// Walk next_marker_after() from 0 and compare against a literal list.
static void check_prefix(int duration, int interval, const Expected *exp, int n) {
  SilentSitConfig cfg = make_cfg(duration, interval);
  int t = 0;
  for (int i = 0; i < n; i++) {
    MarkerEvent ev = MARKER_NONE;
    t = next_marker_after(t, cfg, &ev);
    if (t != exp[i].at || ev != exp[i].ev) {
      printf("  %d/%d step %d: got %d ev=%d, want %d ev=%d\n",
             duration, interval, i, t, (int)ev, exp[i].at, (int)exp[i].ev);
    }
    CHECK(t == exp[i].at);
    CHECK(ev == exp[i].ev);
  }
}

static void test_known_sequences(void) {
  static const Expected s60_0[] = {
    {1800, MARKER_HALF}, {3600, MARKER_END}, {5400, MARKER_HALF}, {7200, MARKER_END},
  };
  static const Expected s60_10[] = {
    {600, MARKER_TICK}, {1200, MARKER_TICK}, {1800, MARKER_HALF},
    {2400, MARKER_TICK}, {3000, MARKER_TICK}, {3600, MARKER_END},
  };
  static const Expected s30_10[] = {
    {600, MARKER_TICK}, {900, MARKER_HALF}, {1200, MARKER_TICK}, {1800, MARKER_END},
  };
  static const Expected s45_30[] = {
    {1350, MARKER_HALF}, {1800, MARKER_TICK}, {2700, MARKER_END}, {3600, MARKER_TICK},
  };
  static const Expected s15_10[] = {
    {450, MARKER_HALF}, {600, MARKER_TICK}, {900, MARKER_END},
    {1200, MARKER_TICK}, {1350, MARKER_HALF},
  };
  check_prefix(60, 0,  s60_0,  (int)(sizeof s60_0  / sizeof s60_0[0]));
  check_prefix(60, 10, s60_10, (int)(sizeof s60_10 / sizeof s60_10[0]));
  check_prefix(30, 10, s30_10, (int)(sizeof s30_10 / sizeof s30_10[0]));
  check_prefix(45, 30, s45_30, (int)(sizeof s45_30 / sizeof s45_30[0]));
  check_prefix(15, 10, s15_10, (int)(sizeof s15_10 / sizeof s15_10[0]));
}

// For every supported config, stepping with next_marker_after() over
// 0..HORIZON must visit exactly the seconds where marker_for_elapsed() is not
// NONE (a brute-force per-second scan), in order, with matching event types.
static void test_full_sequences(void) {
  static const int durations[] = {10, 15, 20, 30, 45, 60};
  static const int intervals[] = {0, 10, 30};

  for (size_t d = 0; d < sizeof durations / sizeof durations[0]; d++) {
    for (size_t i = 0; i < sizeof intervals / sizeof intervals[0]; i++) {
      SilentSitConfig cfg = make_cfg(durations[d], intervals[i]);
      int t = 0;
      int mismatches = 0;
      for (int s = 1; s <= HORIZON; s++) {
        MarkerEvent want = marker_for_elapsed(s, cfg);
        if (want == MARKER_NONE) continue;
        MarkerEvent got = MARKER_NONE;
        t = next_marker_after(t, cfg, &got);
        if (t != s || got != want) mismatches++;
      }
      // Nothing may be left over before the horizon.
      MarkerEvent tail = MARKER_NONE;
      int after = next_marker_after(t, cfg, &tail);
      if (mismatches) {
        printf("  %d/%d: %d mismatches\n", durations[d], intervals[i], mismatches);
      }
      CHECK(mismatches == 0);
      CHECK(after > HORIZON);
      CHECK(tail == marker_for_elapsed(after, cfg));
    }
  }
}

static void test_precedence(void) {
  SilentSitConfig cfg = make_cfg(20, 10);   // half = 600 s, end = 1200 s
  CHECK(marker_for_elapsed(600, cfg) == MARKER_HALF);    // half beats tick
  CHECK(marker_for_elapsed(1200, cfg) == MARKER_END);    // end beats tick
  CHECK(marker_for_elapsed(1800, cfg) == MARKER_HALF);   // next cycle's half
  CHECK(marker_for_elapsed(2400, cfg) == MARKER_END);

  MarkerEvent ev = MARKER_NONE;
  CHECK(next_marker_after(0, cfg, &ev) == 600);
  CHECK(ev == MARKER_HALF);
  CHECK(next_marker_after(600, cfg, &ev) == 1200);
  CHECK(ev == MARKER_END);

  // A tick-only second that is neither half nor end stays a plain tick/none.
  CHECK(marker_for_elapsed(300, cfg) == MARKER_NONE);
}

static void test_none_cases(void) {
  SilentSitConfig cfg = make_cfg(30, 10);
  CHECK(marker_for_elapsed(0, cfg) == MARKER_NONE);
  CHECK(marker_for_elapsed(-1, cfg) == MARKER_NONE);
  CHECK(marker_for_elapsed(-1800, cfg) == MARKER_NONE);
  CHECK(marker_for_elapsed(1, cfg) == MARKER_NONE);
  CHECK(marker_for_elapsed(599, cfg) == MARKER_NONE);

  SilentSitConfig zero = make_cfg(0, 10);
  CHECK(marker_for_elapsed(600, zero) == MARKER_NONE);
}

static void test_strictly_greater(void) {
  static const int durations[] = {10, 15, 20, 30, 45, 60};
  static const int intervals[] = {0, 10, 30};

  for (size_t d = 0; d < sizeof durations / sizeof durations[0]; d++) {
    for (size_t i = 0; i < sizeof intervals / sizeof intervals[0]; i++) {
      SilentSitConfig cfg = make_cfg(durations[d], intervals[i]);
      int bad = 0;
      for (int s = -5; s <= HORIZON; s++) {
        MarkerEvent ev = MARKER_NONE;
        int n = next_marker_after(s, cfg, &ev);
        if (n <= s || n <= 0 || ev == MARKER_NONE) bad++;
        // Nothing may fire strictly between s and n.
        for (int u = (s < 0 ? 1 : s + 1); u < n; u++) {
          if (marker_for_elapsed(u, cfg) != MARKER_NONE) { bad++; break; }
        }
      }
      CHECK(bad == 0);
    }
  }

  // Input that is itself a marker second must advance to the next one.
  SilentSitConfig cfg = make_cfg(60, 0);
  MarkerEvent ev = MARKER_NONE;
  CHECK(next_marker_after(1800, cfg, &ev) == 3600);
  CHECK(ev == MARKER_END);
  CHECK(next_marker_after(3600, cfg, &ev) == 5400);
  CHECK(ev == MARKER_HALF);

  // Negative input behaves like 0.
  CHECK(next_marker_after(-100, cfg, &ev) == 1800);
  CHECK(ev == MARKER_HALF);

  // A NULL out_event is allowed.
  CHECK(next_marker_after(0, cfg, NULL) == 1800);
}

static void test_no_cycle(void) {
  SilentSitConfig cfg = make_cfg(0, 10);
  MarkerEvent ev = MARKER_END;
  CHECK(next_marker_after(0, cfg, &ev) == -1);
  CHECK(ev == MARKER_NONE);
  ev = MARKER_END;
  CHECK(next_marker_after(1234, cfg, &ev) == -1);
  CHECK(ev == MARKER_NONE);
  CHECK(next_marker_after(0, cfg, NULL) == -1);
}

static void test_latest_marker_in(void) {
  SilentSitConfig cfg = make_cfg(60, 0);   // 1800 HALF, 3600 END, 5400 HALF ...
  MarkerEvent ev = MARKER_NONE;

  // None due.
  CHECK(latest_marker_in(0, 1799, cfg, &ev) == -1);
  CHECK(ev == MARKER_NONE);
  CHECK(latest_marker_in(0, 0, cfg, &ev) == -1);
  CHECK(latest_marker_in(1800, 3599, cfg, &ev) == -1);

  // Exactly one due; boundary m == elapsed included, m == after excluded.
  CHECK(latest_marker_in(0, 1800, cfg, &ev) == 1800);
  CHECK(ev == MARKER_HALF);
  CHECK(latest_marker_in(1799, 1800, cfg, &ev) == 1800);
  CHECK(latest_marker_in(1800, 1801, cfg, &ev) == -1);
  CHECK(latest_marker_in(1800, 3600, cfg, &ev) == 3600);
  CHECK(ev == MARKER_END);

  // after == elapsed (or after > elapsed, e.g. clock stepped back): nothing.
  CHECK(latest_marker_in(3600, 3600, cfg, &ev) == -1);
  CHECK(latest_marker_in(4000, 3700, cfg, &ev) == -1);
  CHECK(ev == MARKER_NONE);

  // Several due: only the latest is returned, with its own event.
  CHECK(latest_marker_in(0, 3700, cfg, &ev) == 3600);
  CHECK(ev == MARKER_END);
  CHECK(latest_marker_in(0, 5500, cfg, &ev) == 5400);
  CHECK(ev == MARKER_HALF);

  // Ticks: 60/10 -> markers every 600 s; precedence at collisions.
  SilentSitConfig t = make_cfg(60, 10);
  CHECK(latest_marker_in(0, 1000, t, &ev) == 600);
  CHECK(ev == MARKER_TICK);
  CHECK(latest_marker_in(0, 1850, t, &ev) == 1800);
  CHECK(ev == MARKER_HALF);                       // half beats the coincident tick
  CHECK(latest_marker_in(0, 3650, t, &ev) == 3600);
  CHECK(ev == MARKER_END);                        // end beats the coincident tick
  CHECK(latest_marker_in(3000, 3500, t, &ev) == -1);   // 3000 excluded, 3600 not yet

  // Multi-hour gap stays correct and is O(1).
  CHECK(latest_marker_in(0, 36000 + 5, cfg, &ev) == 36000);   // 10 h
  CHECK(ev == MARKER_END);
  CHECK(latest_marker_in(0, 36000 + 1805, cfg, &ev) == 37800);
  CHECK(ev == MARKER_HALF);

  // Cross-check against brute force over next_marker_after() for every config.
  static const int durations[] = {10, 15, 20, 30, 45, 60};
  static const int intervals[] = {0, 10, 30};
  for (unsigned d = 0; d < 6; d++) {
    for (unsigned i = 0; i < 3; i++) {
      SilentSitConfig c = make_cfg(durations[d], intervals[i]);
      for (int after = 0; after <= 7200; after += 137) {
        for (int el = after; el <= after + 4000; el += 61) {
          int want = -1;
          MarkerEvent wev = MARKER_NONE;
          for (int m = next_marker_after(after, c, NULL); m >= 0 && m <= el;
               m = next_marker_after(m, c, NULL)) {
            want = m;
            wev = marker_for_elapsed(m, c);
          }
          int got = latest_marker_in(after, el, c, &ev);
          CHECK(got == want);
          CHECK(ev == wev);
        }
      }
    }
  }

  // No cycle.
  CHECK(latest_marker_in(0, 5000, make_cfg(0, 10), &ev) == -1);
}

int main(void) {
  test_known_sequences();
  test_full_sequences();
  test_precedence();
  test_none_cases();
  test_strictly_greater();
  test_no_cycle();
  test_latest_marker_in();

  if (g_failures) {
    printf("FAILED: %d of %d checks\n", g_failures, g_checks);
    return 1;
  }
  printf("OK: %d checks passed\n", g_checks);
  return 0;
}
