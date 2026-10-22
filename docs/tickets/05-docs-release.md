# Docs, screenshots and version bump for 1.2.0

## Context

Part of the v1.2.0 plan (`docs/tickets/00-plan.md`). Do last, after 02, 03, 04
are merged.

## Task

1. **README.md**
   - *Home & settings*: list the new "Halfway buzz" setting.
   - *Vibration markers* table: note mid-cycle is optional and **off by
     default** (turn it on with the "Halfway buzz" setting).
   - Release notes text for the maintainer (put it in the PR description):
     must say the halfway buzz is now off by default, so users who relied on it
     need to switch it on after updating.
   - *Architecture*: scheduler paragraph reflects the tick-service + catch-up
     design (ticket 02 may already have done this — reconcile, don't duplicate).
   - Ensure the *Host tests* and *Diagnostics build* sections from 01/04 read
     well together.
2. **STORE.txt**: mention the halfway buzz is optional and that settings
   include it ("Press Down to adjust the duration, tick interval, halfway buzz,
   or vibration strength").
3. **CLAUDE.md** (AGENTS.md): update the layout/commands tables only where
   facts changed (new `test/`, `tools/test-markers.sh`, `src/c/diag.c`,
   `MARKER_DIAG` define). Keep it a summary that points to the README.
4. **Screenshots**: run `tools/generate-screenshots.sh`; the settings
   screenshot will now show 5 rows and the home summary text changes. Update the
   script's header comment if the shots' content description changed. Commit
   the regenerated PNGs.
5. **Version**: bump `version` in `package.json` to `1.2.0`.

## Out of scope

Do **not** run `pebble publish` or create/push tags — the maintainer does that
after trying the build on the watch.

## Acceptance criteria

- Docs consistent with the code; screenshots regenerated; `pebble build` and
  `tools/test-markers.sh` pass; version is 1.2.0.
