# Diagnostics: vibration self-test

## Context

Companion to ticket 06 (`docs/tickets/06-diagnostics-v2.md`). The app now
delivers every marker on time, but the user sometimes doesn't feel the END
buzz. Besides firmware Stand-By Mode (see 06), the other candidate is the
vibration itself: END is the longest pattern (two ~0.5 s pulses at Medium,
~0.8 s at Strong). A short, attended test separates "the motor/firmware drops
this pattern" from "Stand-By Mode after 30 still minutes" — Stand-By can't
engage during a few-minute test the user is actively doing.

`MARKER_DIAG` builds only; normal builds byte-for-byte unaffected.

## Requirements

- In the diag build, a **"Vibe test"** row at the bottom of the settings menu
  (only compiled in with `MARKER_DIAG`). Select opens the test window.
- Test window: choose the pattern with Up/Down (`TICK` / `HALF` / `END`,
  default END) and Select to start. It then plays that pattern at the current
  strength **10 times, 10 s apart**, showing `END 3/10` (large text). Reuse the
  existing `vibe_tick/vibe_half/vibe_end` patterns from `session_window.c`
  (expose a small `vibe_play_marker(MarkerEvent ev)` or similar — don't
  duplicate the segment tables). Use an `app_timer` for the 10 s spacing
  (short timers are fine here).
- After the 10th, ask `How many did you feel?` with Up/Down to pick 0–10
  (default 10) and Select to save. Back during the run cancels (no result).
- Store the result in the ticket-06 session history as a special entry
  (e.g. kind `TEST` with pattern, count felt, battery %, charging) so it shows in
  the same viewer: `TEST END felt 7/10 bat 64%`. If ticket 06 isn't merged
  yet, coordinate: this ticket is implemented **after** 06.

## Tests

- Plain build unaffected (nm + size check as in 06); diag build no new warnings.
- Emulator (headless `--emulator emery --vnc`, capped ~3 min): open settings,
  scroll to "Vibe test", start END, let it run to 2/10, screenshot; press Back to
  cancel; start again and let it finish (≈100 s), pick 7 with Down×3, Select;
  open the log from a session summary and confirm the TEST line. View
  screenshots with Read.

## Acceptance criteria

- Diag build offers the test; results land in the history viewer.
- Normal builds byte-for-byte unaffected; README "Diagnostics build" section
  mentions the test.
