# Handoff: missing 60-minute buzz investigation

Status as of 2026-10-25. Read this first when picking the work up in a new
session; it is the single summary of what was found, what was built, and what is
still open. Ticket specs live next to it in `docs/tickets/`.

## The problem

User wears a Pebble Time 2 (`emery`) with a 60-min cycle. The END buzz at 60 min
was often never felt; occasionally the HALF buzz at 30 min was missing instead.
The app was always in the foreground.

## Findings, in order

1. **The 30-min buzz with "no ticks" was by design** — the HALF marker was always
   on and invisible in the UI. Fixed by making it a setting (issue #3, default off).
2. **Original scheduler was fragile.** Each marker was one long `app_timer`
   (up to 30 min) re-armed only from its own callback; a lost timer lost that
   marker and every later one. Replaced by `tick_timer_service` + catch-up
   (issue #2). Pure scheduling math was always correct (host-tested).
3. **On-wrist test of the 1.2.0 diagnostics build (one week):** the on-watch log
   **always** showed `60:00 END` on time — including the 3–4 days the user felt
   nothing. So the app now delivers every marker; **the vibration is dropped
   below the app**, in the firmware.
4. **Leading root cause: PebbleOS Stand-By Mode** ("stationary mode"), from
   reading github.com/coredevices/PebbleOS:
   - `src/fw/services/stationary/service.c`: once a minute the accelerometer
     orientation is compared with the previous minute (|Δx|+|Δy|+|Δz| < 50 =
     still). After **30 consecutive still minutes** it calls
     `services_set_runlevel(RunLevel_Stationary)`. Any motion/button wakes it;
     it then re-engages after only **5** still minutes.
   - `src/fw/services/services_common/service.c`: `vibe_service_set_enabled`
     has `enable_mask = R_LowPower | R_FirmwareUpdate | R_Normal` — **not
     `R_Stationary`**, so in Stand-By every vibe request is silently dropped
     (`vibe_pattern/service.c`, `prv_vibes_set_vibe_strength` returns early).
   - Apps cannot query or inhibit it. Only system alarms call
     `stationary_wake_up()` (`shell/normal/shell_event_loop.c`).
   - User setting: **Settings → System → Stand-By Mode**, default **On**
     (`shell/normal/prefs.c`, `s_stationary_mode_enabled = true`).
   - Fits the symptoms: a meditator's wrist is motionless, so by 60 min the
     watch is usually in Stand-By; at 30 min it often isn't yet; small
     movements reset the count → intermittent.
   - Also verified: `vibes_enqueue_custom_pattern` copies the durations
     synchronously (`applib/ui/vibes.c`), so the app's stack-buffer pattern is
     fine. A pattern is also rejected while another is in progress
     (`prv_vibe_pattern_enqueue_step_raw`) — a minor secondary possibility.

**Not yet confirmed on the wrist.** The user was asked to turn Stand-By Mode off
and sit a few times.

## Branch state: `fix/reliable-markers` (pushed to origin up to ecdf58e; later commits are local)

Committed (all SSH-signed), issues #1–#5 on GitHub:

| Commit | Issue | What |
|---|---|---|
| 32e4a71 | #1 | Host unit tests: `tools/test-markers.sh` (256351 checks) |
| f7b882a | #2 | Markers via `tick_timer_service` (MINUTE, SECOND when < 60 s away) + catch-up (`latest_marker_in`) |
| 73f5338 | #3 | "Halfway buzz" setting, default off (also for upgrading users) |
| 64a019e | #4 | `MARKER_DIAG=1` on-watch marker log (Down on summary) |
| a32cade | — | German wording "Halbzeit-Vibration" |
| d9b1109 | #5 | Docs, settings screenshot, version 1.2.0 |
| ecdf58e | — | Ticket specs 00–05 |
| (this) | — | This handoff + ticket specs 06/07 (issues #6/#7) |

Diagnostics v2 + vibe self-test (issues #6/#7), committed together because
both extend `diag.c`; **checked on the watch by the user (2026-10-25)**:

- Ticket 06: per-session settings line, "felt it?" yes/no questions after each
  sitting, battery / phone / Quiet Time / **still-minutes** (app replica of the
  firmware rule, via `accel_data_service_subscribe(25, …)` at 10 Hz) per
  marker, history of the last 10 sessions (persist keys 100–109, newest index
  in 110; Select pages sessions in the viewer).
- Ticket 07: `src/c/vibe_test.c` + "Vibe test" settings row (diag builds only)
  — plays TICK/HALF/END 10× every 10 s, asks how many were felt, stores a TEST
  entry in the history.
- Plain `.pbw` unchanged (21619 B, no diag symbols); host tests pass.
- `build/SilentSit-1.2.0-diag2.pbw` = this diag build.

## Next steps

1. Get the user's result with **Stand-By Mode off**. If the END buzz is then
   reliable, the root cause is confirmed.
2. Read the diag v2 history after sittings: unfelt markers with still-minutes
   ≥ 30 confirm Stand-By Mode; the vibe self-test separates motor issues.
3. If Stand-By is confirmed, propose to the user:
   - an in-app hint (e.g. on the Quiet Time reminder / first run: "Turn off
     Stand-By Mode in Settings → System for reliable buzzes") + README/STORE
     note;
   - an upstream PebbleOS issue: Stand-By should not silence a foreground app
     (vibe service disabled in `R_Stationary`; only alarms wake it).
4. Then: PR `fix/reliable-markers` → `main` (closes #1–#5), drop the
   `MARKER_DIAG` bullet from the release notes, publish 1.2.0 (README →
   Publishing; user's decision).

## Environment notes (Claude Code sandbox on the user's Mac)

- `pebble` is in `~/.local/bin` (uv tool, SDK 4.33.1); prefix commands with
  `export PATH="$HOME/.local/bin:$PATH"`. The tool's update checks to
  `sdk.repebble.com`/`pypi.org` are blocked — harmless.
- Emulator only runs headless here: add `--emulator emery --vnc` to
  install / emu-button / screenshot / logs; `pebble kill` takes no flags. It
  needs `sandbox.network.allowLocalBinding: true` (currently **off**). Always
  cap emulator scripts (background + kill after a few minutes) — the user
  dislikes long-running commands.
- Commit signing goes through 1Password (`op-ssh-sign`); needs the agent socket
  allowed **and** read access to `~/Library/Group Containers/2BUA8C4S2C.com.1password/t`
  (currently denied → "failed to fill whole buffer").
- `gh` can't read its config/Keychain token in the sandbox: give the user
  commands to run, e.g.
  `for f in docs/tickets/0[67]-*.md; do gh issue create --repo nicolinux/silent-sit-pebble --title "$(head -1 "$f" | sed 's/^# //')" --body-file "$f"; done`
- `~/.ssh` is off-limits; remote is SSH (`git@github.com:nicolinux/silent-sit-pebble.git`).
- Implementation work was delegated to Sonnet sub-agents, one ticket each, then
  reviewed; keep that pattern if the user asks for it.
- Getting a build on the watch: AirDrop the `.pbw` to the iPhone and open it in
  the Pebble app (replaces the store version).
