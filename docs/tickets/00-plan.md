# Plan: reliable markers + optional halfway buzz (v1.2.0)

## Problem

Reported on a real Pebble Time 2 (`emery`), cycle = 60 min, tick interval = off:

1. **Markers go missing intermittently.** The END buzz at 60 min often never
   arrives; in another >70 min sitting the HALF buzz at 30 min was missing but
   END arrived. The app was in the foreground the whole time, and the on-screen
   minute counter kept counting correctly.
2. **"No ticks" still buzzes at 30 min.** That is the HALF marker, which is
   always on and not configurable or mentioned anywhere in the UI
   ("60 min, no ticks"). Users reasonably read that as "one buzz at 60 min".

## Root-cause analysis (issue 1)

- The scheduling math in `src/c/markers.c` is correct — verified on the host for
  every duration × interval combination (60/off → `30 HALF, 60 END, 90 HALF, 120 END`).
- The runtime in `src/c/session_window.c` drives markers with **one long
  `app_timer`** per marker (up to 30 min with ticks off), re-armed only from
  inside its own callback (`schedule_next_marker` / `marker_timer_cb`). If one
  timer is lost, that marker is lost **and the chain dies** — matching
  "nothing after 30".
- The on-screen counter uses a different mechanism, `tick_timer_service`
  (RTC-driven, `MINUTE_UNIT`), which the user confirms is reliable.
- Secondary defects of the current design: the readout (RTC) and markers
  (app_timer tick clock) use two different clocks; an `app_timer` that fires
  slightly early re-arms for the same marker 1 s later (double buzz); a
  backwards wall-clock correction can also double-fire.

## Solution

Drive markers entirely from `tick_timer_service` (the same mechanism as the
readout), with **catch-up** so a marker can never be silently dropped, and add a
**"Halfway buzz" on/off setting**.

## Tickets (in dependency order)

| # | Ticket | Depends on |
|---|--------|-----------|
| 01 | Host-side unit tests for `markers.c` | — |
| 02 | Drive markers from `tick_timer_service` with catch-up (fix missing buzzes) | 01 |
| 03 | "Halfway buzz" on/off setting | 01, 02 (touches the same files — do after 02) |
| 04 | On-watch marker diagnostics log (test builds only) | 02 |
| 05 | Docs, screenshots, version bump to 1.2.0 | 02, 03, 04 |

Tickets 03 and 04 can run in parallel after 02 if each agent works in its own
branch; expect a small merge in `session_window.c`.

## Ground rules for implementing agents

- Read `CLAUDE.md` / `AGENTS.md` and `README.md` first.
- Target platform is `emery` only. Build with `pebble build`; it must compile
  with **zero new warnings**.
- Keep `markers.c` **pure** (no Pebble runtime calls, no state). All timing is
  derived from absolute elapsed seconds — never accumulate time.
- User-facing strings go in `src/c/locale.c` for **all six languages**
  (EN/IT/ES/FR/DE/PT). `min`, `tick`, `off`, `on`, `Select`, `Quiet Time` stay
  untranslated.
- Exercise the app in the emulator via `tools/generate-screenshots.sh` or a
  script derived from it (see CLAUDE.md), not by hand.
- One PR per ticket, referencing the issue. Do **not** publish to the appstore
  and do not push tags — publishing is the maintainer's call.
