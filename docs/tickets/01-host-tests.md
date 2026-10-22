# Add host-side unit tests for markers.c

## Context

Part of the v1.2.0 plan (`docs/tickets/00-plan.md`). Tickets 02 and 03 change
the pure scheduler in `src/c/markers.c`; there is no Pebble test framework, so
we need a tiny host-compiled test harness first to make those changes safe.

`markers.c` only includes `markers.h`, which includes `<pebble.h>` for
`uint8_t`/`bool`. It compiles on the host against a stub header.

## Task

1. Add `test/pebble.h` — a host stub providing only what `markers.h` needs
   (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`).
2. Add `test/markers_test.c` — a plain C test program (no framework; a small
   `CHECK(cond)` macro that prints file:line on failure and counts failures;
   exit code non-zero on any failure).
3. Add `tools/test-markers.sh` — compiles with
   `cc -std=c99 -Wall -Wextra -Werror -Itest -Isrc/c test/markers_test.c src/c/markers.c`
   into `$TMPDIR` (or `build/host-test/`) and runs it. Make it executable.
4. Make sure `test/` is **not** picked up by `wscript` (it globs
   `src/c/**/*.c`, so a top-level `test/` is already excluded — verify).

## Test cases to cover (current behaviour)

- Full expected sequence over 0..7200 s via repeated `next_marker_after` for
  every duration {10,15,20,30,45,60} × interval {0,10,30}. Known-good examples:
  - 60/0  → 1800 HALF, 3600 END, 5400 HALF, 7200 END
  - 60/10 → 600 TICK, 1200 TICK, 1800 HALF, 2400 TICK, 3000 TICK, 3600 END, …
  - 30/10 → 600 TICK, 900 HALF, 1200 TICK, 1800 END, …
  - 45/30 → 1350 HALF, 1800 TICK, 2700 END, 3600 TICK, …
  - 15/10 → 450 HALF, 600 TICK, 900 END, 1200 TICK, 1350 HALF, …
- Precedence: END beats HALF beats TICK on the same second
  (e.g. 20/10 at 600 s → HALF, at 1200 s → END).
- `marker_for_elapsed(0, cfg) == MARKER_NONE`; negative elapsed → NONE.
- `next_marker_after` is strictly greater than its input, including when the
  input is itself a marker second.
- duration 0 → `next_marker_after` returns -1 and sets `MARKER_NONE`.

## Acceptance criteria

- `tools/test-markers.sh` passes on macOS with the system `cc`.
- `pebble build` still succeeds and the `.pbw` is unchanged in behaviour.
- A short "Host tests" note added to README's *Build & run* section and the
  command added to the CLAUDE.md commands table.
