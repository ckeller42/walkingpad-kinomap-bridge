# UI / UX review of the bridge screen, setup wizard and Wi-Fi setup — 2026-10-22

Read-only review by a fresh reviewer (most capable model) of `src/ui.*`, `lib/wizard`,
`lib/devmenu`, the tap handling in `src/main.cpp`, `tools/set_wifi.sh` and the docs.
Two findings (#4, part of #5) are inferred from coordinates/code, not seen on hardware.
Nothing has been changed yet. Ranked by impact on the walker; effort S/M/L.

## Top findings

1. **Two quick taps on the left button start the belt; a third leaves it running while the screen says ARM.** (M)
   `main.cpp:823-831`, `ui.cpp:389-392`, `bridge_state.cpp:94-101`, `:11-16`, `main.cpp:174-188`.
   `startFromUi` sets `running_` at once (face START→DISARM before the belt moves); the ~3 s countdown invites a second tap = `setArmed(false)`, which does not stop; `driveStart` never checks `armed()`, so the pending `setSpeed(2.0)` still goes out.
   Fix: ignore the left button ~1.5 s after a face change / until the start ends; `driveStart` drops a pending start when disarmed; move the face/`canStart` decision into `lib/` with tests (it is written twice: `ui.cpp:389`, `main.cpp:824`).
2. **DISARM looks like a safe action while the belt runs, and STOP is no easier to hit.** (S–M)
   `ui.cpp:385-393`, `docs/setup-wizard.md:76`. Both halves 160×60 px (~20×7.6 mm); STOP under the 9 mm target.
   Fix: while running give STOP ~2/3 of the bar (or all of it), shrink DISARM; optionally long-press for DISARM while running; raise `BAR_Y`/`HIT_Y` ~15 px.
3. **The setup wizard and the developer menu have no STOP.** (M)
   `ui.cpp:432-433`, `:532-534`, `main.cpp:843-849`. The menu opens without disarming; in Auto the belt starts on step-on, so it can move while a screen with no STOP owns the touch zones; the same corner is red STOP on the main screen and green NEXT/CONNECT here.
   Fix: disarm on menu open and refuse wizard/menu while Auto is observed or the belt is not parked — or keep a small red STOP strip on every screen.
4. **Battery percentage likely overlaps the BELT dot/label** (S, inferred). `ui.cpp:305`, `:134-139` vs `:92-96` — text at x≈32-70, y≈18-30 vs dot x 31-49, label from x 56. Fix: move the gauge to the free gap x≈100-125 beside the badge or below the bar; capture a screenshot with a fake 57 % battery.
5. **Wi-Fi menu does not update after CONNECT; no connecting/failed state.** (S+M)
   `main.cpp:503-511` redraws only on touch or while scanning; `devmenu.cpp:149-150` jumps to Root showing "offline"; a wrong password looks like "still connecting" forever; `devmenu::Inputs` (`devmenu.h:54-63`) has no connecting/auth-failed field. Scanning may stick the same way (`logsink.cpp:283-287`, inferred).
   Fix: redraw when the menu's screen changes (hash) or at 1 Hz; add `wifiConnecting`/`wifiFailed` to Inputs with "connecting…" / "wrong password?"; pin in `test_devmenu`.
6. **The keyboard cannot type common password characters.** (S) `ui.cpp:44-46`, `:240-247`, `:501-505`. `)` sits under DEL; `. , ; ' " ~ < > [ ] { } | \ `` ` `` missing (16 of 95); two ABC keys on the symbols page. Fix: second "#+=" page, reachable `)`, tables into `lib/devmenu` with a test that every printable ASCII char is reachable.
7. **A slip off the bottom key row submits or discards the password.** (S) `ui.cpp:13`, `:38-39`, `:200`: key rows end at y 183, bar hit zone starts at y 180, SP sits over CONNECT; BACK then re-pick clears the password (`devmenu.cpp:122-123`). Fix: 8 px dead band, `HIT_Y` ≥ 190 in keyboard mode, keep typed text on BACK to the same SSID.
8. **Refused taps give no on-screen feedback.** (S–M) badge `main.cpp:803-821` (log only), `+`/`-` `:833-834`, BELT `:840`, gear `:843`; Auto `+`/`-` look like disarmed (`ui.cpp:349-351`). Fix: a 1 s reason line above the readout ("ARM first", "STOP first", "AUTO: belt sets speed"); flash the badge outline on refusal (`o.count == 0` already exists).
9. **Armed state unreadable from 1 m.** (S) `ui.cpp:309-314`: 9 pt green tag under the gear; two greens (START bright, DISARM dark); green also = link up, M badge, NEXT/CONNECT, battery OK. Fix: a 3-4 px coloured frame around the readout or tinted top bar while armed; tag ≥ 12 pt bold.
10. **Armed-but-cannot-start shows DISARM with no reason.** (S) `ui.cpp:389-390`. Fix: reuse the reason line ("belt offline", "AUTO – step on to start").
11. **Mis-tapping the badge/BELT strip opens setup and disarms.** (S) `ui.cpp:17`, `:213-218`: strip x 0-103 beside badge x 104-175, boundary cuts through "BELT"; gear zone 30×40 px (~3.8×5.1 mm), dim. Fix: long-press for re-run setup or move it into the gear menu (also fixes discoverability, `wizard.cpp:172`); gear zone ≥ 44 px.
12. **Wizard's last step omits the 15 s auto-stop.** (S) `wizard.cpp:162-172`. Fix: "Stand on it – it stops after 15 s empty."
13. **Wizard scan states don't match reality.** (S–M) `main.cpp:296-302`, `wizard.cpp:92-118`: Linked-without-data maps to Idle ("Looking for the belt" → "not found"); failing connects flip Found/Scanning; no hint about another phone holding the belt; "Searching for 'RE' (FE00)" + MAC is jargon. Fix: `LinkedNoData` phase, sticky "found" wording, hint after N failed connects, pin in `test_wizard`.
14. **Scan list drops networks, no security/signal, header overlap.** (S) `devmenu.h:45` vs `:72`, `devmenu.cpp:84`: 6 kept, 5 shown; no RSSI/lock; no hidden SSID; SSID and password overlap past ~12 chars (`ui.cpp:480-492`). Fix: `kMaxNetworks == kMaxRows` or scroll, RSSI bars, clip the password from the left.

## Quick wins (S, high value)

- Input lockout after a face change + drop a pending start when disarmed (#1).
- Keyboard dead band + `HIT_Y` ≥ 190 in keyboard mode (#7).
- Reachable `)` `.` `,` + reachability test (#6).
- Redraw the developer menu when its inputs change (#5).
- One-second reason line for refused/muted taps (#8, #10).
- Wizard copy: 15 s auto-stop (#12).
- Stale docs: `devmenu.h:14`, `:97`, `docs/developer-menu.md:7` still say "tap PHONE"; `docs/setup-wizard.md:76` says "reads ARMED"; `docs/screens/main-*.png` predate steps/badge/battery/"SIM" — recapture.

## Leave alone (deliberate)

Password shown in clear; STOP also disarms; DISARM does not stop (fix the label/size, not the behaviour); badge armed+parked only and Auto mutes `+`/`-`; no `0x02` from Kinomap; 3-row keyboard; amber SIM everywhere; gear/BELT refused while running (needs feedback only).

## Open questions

1. Seen on hardware: CONNECT / scan leaving "offline" / "Scanning..." until the next tap (#5)?
2. Auto start on step-on while the bridge is disarmed — measured 2026-10-22 that Auto starts on step-on; whether the bridge was armed at that moment is not recorded. If yes, #3 is the top safety item.
3. Does the belt's own remote/panel give a STOP the walker can rely on when the bridge screen has none?
4. Is the 15 s empty-belt auto-stop also active in Auto?
5. `tools/set_wifi.sh:37` passes the password in argv (visible to `ps`) and the console line `wifi $SSID $PASS` is space-split — are SSIDs/passwords with spaces handled?
