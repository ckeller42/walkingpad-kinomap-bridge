# Belt Auto Mode — Design

Date: 2026-10-22. Status: draft for review.

## Intent and why

Let the walker use the WalkingPad R1 Pro's **Auto mode** — the belt sets its own
speed from where the walker stands on the deck — while the bridge keeps feeding
Kinomap live data, and let the walker switch **Manual ⇄ Auto from the bridge
screen**.

Why: Auto is the natural Kinomap experience. Kinomap only *reads* a generic FTMS
treadmill (it never commands speed, see `AGENTS.md` / issue #2), so today the
walker sets speed with `+`/`-` and Kinomap follows a fixed setpoint. In Auto the
walker just walks at their own pace, the belt matches them, and Kinomap follows
the real speed. No fiddling.

## Evidence base

From `protocol/wilink.yaml` and the KS Fit app decompile (Flutter `libapp`
strings, `ks_blue/src/wilink/*`). Provenance is recorded per fact.

| Fact | Provenance |
|---|---|
| Modes: Manual = 1, Auto = 0, Standby = 2; set with `A2 02 <mode>` (`set_mode`); reported in status byte 4. | Manual/Standby **measured**; Auto **community** — settable, never produced by the bridge, never observed in a status frame on this belt. |
| Auto is **position-driven**: stand in the *front* zone → the belt starts and accelerates by itself; drift to the *rear* zone → it decelerates. **No `start_belt` is needed.** | community (app strings: *"Enter the front area to start the treadmill to automatically accelerate"*, *"Step onto the rear area; Automatic deceleration"*) |
| **Speed commands are rejected in Auto.** | community (app: *"Speed adjustment is unavailable in Automatic Mode"*, *"The equipment is in automatic mode and cannot be adjusted for speed"*, flag `isAutoRejectSpeed`; courses/programs declared incompatible with Auto) |
| The belt panel has **physical "M" and "A" keys** and an "A" indicator light, so the mode can change **without the bridge**. | community (app setup text) |
| Auto requires the handrail down and walking mode (R1 Pro is walk/run dual-mode). | community |
| An auto **max-speed cap** (`setMaxSpeed`) and `autoModeSensitivity` exist, but ride a **non-`0xA2`** extension command group. | community |
| `set_mode 0` is `A2 02 00` — inside the only command group the bridge may emit. | by construction |

Open unknowns on **this** belt: whether it accepts `set_mode 0`, and what status
byte 4 reports in Auto. The design below does not depend on either for safety.

## Goals

1. A **mode badge** on the main screen that reflects the belt's **observed**
   mode (status byte 4), including changes made with the belt's own keys.
2. **Toggle Manual ⇄ Auto by tapping the badge**, honoured only while the bridge
   is **armed** and the belt is **stationary**.
3. While Auto is **observed**, the bridge's whole **command path is muted
   structurally**: `+`/`-` inert, speed-policy outcomes refused, Kinomap
   StartResume a no-op, START hidden. **STOP always works**.
4. The **read path is unchanged**: FTMS speed/distance/steps keep flowing.
5. **Fail-safe on the provenance gap**: the bridge behaves as "in Auto" only
   when a status frame **reports** mode 0 — never because it *sent* the
   request. A request the belt does not confirm shows as *unconfirmed* and the
   bridge keeps its Manual personality.
6. **Hands-off on external mode changes**: if the walker presses the belt's
   **A** key (even while the bridge is disarmed) the bridge shows it and does
   not fight it.
7. **STOP is plain STOP.** Standby is the belt's only stop (measured), so an
   explicit STOP leaves Auto as the belt dictates; Auto comes back with one
   badge tap. Normal Auto walking never involves Standby: stepping off
   self-stops the belt to idle and it stays in Auto.
8. Policy is host-tested in `lib/`; hardware effects live in `src/`.

## Non-goals

- The auto max-speed cap and sensitivity settings (non-`0xA2` group; the write
  choke point forbids it by design).
- A settings-menu placement for the toggle (the main-screen badge was chosen).
- Changing anything on the Kinomap side (it only reads).
- Validating Auto on hardware *within this build*. That is a separate capture
  task (below); the build is safe without it.

## Anti-goals — what would make this a failure even if it "works"

- Any bridge path that moves the belt while disarmed.
- Treating "I sent `set_mode 0`" as "I am in Auto".
- Emitting any frame whose group byte is not `0xA2`.
- The bridge force-stopping a belt the walker put in Auto by hand.
- A mode flip from an accidental tap mid-walk.
- Silently losing evidence: every mode transition must be logged as verbatim
  hex plus the decoded mode.

## Constraints

- `BeltLink::send()` is the single write choke point and refuses anything but
  `0xA2`. `set_mode` is `A2 02 <mode>`; nothing new is needed there.
- `lib/` is Arduino-free and host-tested; `src/` holds adapters. Every
  state-changing `BridgeState` entry point returns a `[[nodiscard]] Outcome`
  that exactly one `applyOutcome()` drains. Mode gating joins that model.
- STOP works whether armed or not.
- `protocol/wilink.yaml` is the single source of truth. Auto's provenance
  **stays `community`** until measured; the yaml notes get updated and the
  protocol docs regenerated (`tools/gen_protocol_reference.py`,
  `gen_frame_layouts.py`, `check_protocol_views.py`).
- Nobody on the belt → it auto-stops in ~15 s. Auto's "start on step-on" is
  consistent with this; tests must not assume motion without a walker.

## Design — the parts that are decided

### Model: `BridgeState` learns the belt mode

State (in `lib/policy/bridge_state.h`, host-tested):

- `observedMode` — last status byte 4 as `wilink::Mode`, plus an **`Unknown`**
  sentinel (not a wire value) used before the first status frame and after a
  belt link loss.
- `pendingMode` — the mode the bridge last emitted and is awaiting, with a
  **status-frame counter**. `BridgeState` keeps no clock: each `observeMode()`
  call is one tick, so the confirmation window is counted in frames.

Derived: `autoActive() == (observedMode == Mode::Auto)` and
`autoPending() == (pendingMode == Mode::Auto)`.

Entry points (each returns a `[[nodiscard]] Outcome`):

- **`requestMode(Mode m)`** — gate: `armed && beltUp && !observedRunning`,
  where *observedRunning* is the belt's reported state/speed, **not** the
  optimistic `running_`. Otherwise emits nothing. Sets `pendingMode = m` and
  emits the new **`Emit::SetMode`** (the mode travels in
  the `Outcome`). **Both directions switch only at rest** — to leave Auto
  mid-walk, STOP first. That is the "no mid-walk flip" anti-goal made
  structural.
- **`observeMode(Mode m)`** — called once per status frame.
  - `m == pendingMode` → confirmed; clear pending.
  - a *different* mode arrives while pending (e.g. Manual after an Auto
    request) → the belt rejected it; clear pending, **no automatic retry**,
    log it.
  - no pending and `m != observedMode` → an **external** change (belt key).
    Update `observedMode` and emit nothing else: hands-off, even while
    disarmed.
  - frame counter passes the window with no confirmation → clear pending
    (badge back to `M`), log it; the walker may tap again.
  - on **leaving** Auto by any route, also emit `ResetPolicy`, so the first
    target after Auto is not deadbanded against a stale reference.
- **`onBeltObservedRunning()`** — new, the mirror of `onBeltObservedStopped()`.
  In Auto the belt starts on step-on with **no bridge emission**, so `running_`
  must follow *observed* status there: set `running_` and emit `NotifyStarted`,
  which is what makes Kinomap begin its session. Outside Auto the existing
  optimistic path is unchanged.
- **Structural mute**: `apply()`, `setSpeedFromUi()`, `stepSpeedFromUi()`, the
  speed path of `startFromUi()` and the phone `0x07` translation return an
  empty `Outcome` while **`autoActive() || autoPending()`**, exactly as while
  disarmed. Muting while merely *pending* closes the window in which a belt
  that really is in Auto, but not yet confirmed, could be fought by `+`/`-`.
- **STOP** (`stopFromUi()`, phone stop, link loss) emits `Stop` (Standby) as
  today in every mode and clears `pendingMode`. As today, UI STOP also
  disarms. Standby is the belt's **only** stop, so an explicit STOP leaves
  Auto; that is the belt's rule, not the bridge's.
- **START** (`startFromUi()`) is unchanged: the Manual `StartSequence`. While
  `autoActive()` it is hidden and emits nothing — in Auto the walker starts by
  stepping into the front zone. After a STOP, Auto comes back by re-ARMing and
  tapping the badge (two taps, the same as ARM → START today).
- **`onBeltLink(false)`** resets `observedMode` to `Unknown` and clears
  `pendingMode`.
- **Disarm** (`setArmed(false)`) behaves as today — it stops a moving belt —
  which in Auto parks it in Standby and so leaves Auto.

Leaving Auto therefore has two routes: the badge at rest (`SetMode(Manual)`),
or STOP → Standby followed by today's Manual start sequence, which already
writes mode Manual first.

**Accepted unknown.** If this belt accepts `set_mode 0` but reports some byte-4
value other than 0, the bridge will show `A?` then fall back to `M` while the
belt is actually in Auto. Two things make this safe rather than dangerous: the
command path is muted while pending, and the app evidence says the belt itself
rejects speed commands in Auto, so a stray `+` could not fight it. The capture
(below) is what would reveal this; it is the reason the provenance stays
`community`.

### Screen

- Badge in the **top-bar centre** (between BELT and PHONE; the battery gauge
  is top-left). States: **`M`** green = Manual · **`A`** cyan = Auto
  (observed) · **`A?`** amber = Auto requested, not yet confirmed. Greyed while
  disarmed; a tap then does nothing.
- While Auto is observed: `+`/`-` drawn inert (grey), the `ARMED` tag reads
  `AUTO`, the left bottom button shows `DISARM` only (no `START`). `STOP`
  unchanged.
- The badge is a **tap** target (visible affordance; no hidden gesture).
- Left bottom button faces, extending today's three:
  - disarmed → `ARM`
  - armed, `autoActive` → `DISARM` (no START: stepping on starts the belt)
  - armed, Manual, can start → `START`; armed otherwise → `DISARM` (as today)
- The screenshot tour (`cores3_shot`) gains phases for Manual, Auto and
  unconfirmed.

### `Outcome` / `Emit`

- Add `Emit::SetMode`, with the requested mode carried in `Outcome` (a new
  `mode` field beside `tenthKmh`). `kMaxEmits` stays 3: no new outcome emits
  more than one thing plus a policy hint.

### Kinomap

- Read path unchanged.
- FTMS StartResume (`0x07`): while `autoActive()` (or pending) → no-op (no
  `set_speed`); otherwise unchanged.
- FTMS Stop/Pause → STOP → Standby, as today.

### Protocol documentation

- `set_mode` notes: "Mode 0 (auto) is produced by the bridge only on explicit
  user request, and the bridge treats Auto as active only once a status frame
  reports mode 0." Provenance of mode 0 stays `community`.
- Add a bridge-side fact `auto_requires_observed_confirmation`.
- Regenerate `docs/protocol/*.md`; never hand-edit them.

### Validation on hardware (separate task, recommended before relying on Auto)

No risky write is needed to prove the read side: press the belt's own **A**
key with `tools/logcat.py` running and confirm status byte 4 == 0 and
start-on-step. Then exercise the bridge's `set_mode 0`. Upgrade the yaml
provenance to `measured` with the log as evidence.

### Tests (host, `lib/`)

`BridgeState`:
- `requestMode` emits nothing when disarmed, when the belt link is down, or
  while the belt is *observed* running (both directions); otherwise emits one
  `SetMode` and records `pendingMode`.
- `autoActive()` is true only after `observeMode(Auto)`, never after a bare
  `requestMode(Auto)`; `autoPending()` is true in between.
- While `autoActive()` **or** `autoPending()`: `apply()`, `setSpeedFromUi()`,
  `stepSpeedFromUi()` and the phone start emit nothing; STOP still emits
  `Stop`.
- `observeMode` confirms a pending request; a different mode while pending
  clears it with no retry; the frame-window timeout clears it.
- External `observeMode(Auto)` while disarmed emits nothing (hands-off).
- Leaving Auto by any route emits `ResetPolicy`.
- `onBeltObservedRunning()` while `autoActive()` sets `running_` and emits
  `NotifyStarted` exactly once; outside Auto it leaves the optimistic path
  alone.
- `startFromUi()` while `autoActive()` emits nothing; after a STOP it is the
  plain `StartSequence` (Auto is not remembered).
- `observedMode` resets to `Unknown` on link loss and `pendingMode` clears.
- No outcome exceeds `kMaxEmits` (extend the existing pinning test).

## Left to the builder

- The confirmation window, **in status frames** (clock-free; suggest 5 — name
  the constant).
- Exact badge geometry in the top bar (centre gap is roughly x 106..173,
  y 0..27).
- Log line format for mode transitions (verbatim hex + decoded mode, per the
  evidence discipline).
- `AGENTS.md` quotes two stale host-test counts (145 / 153; it is 168 now).
  Nothing in CI ratchets on the count — refresh the numbers as part of this
  work.

## Builder-check resolutions

A fresh reviewer read the draft and raised ten points; all were settled here
rather than sent back, because each is either implied by the agreed intent or
is a builder-level call. Recorded so the calls can be checked:

1. Requested vs external mode change → distinguished by `pendingMode`. (A
   remembered Auto preference was first designed, then **removed at the
   owner's request**: it saved no taps and only added state.)
2. Auto starts without a bridge emission → `onBeltObservedRunning()` sets
   `running_` from status and emits `NotifyStarted`; the `requestMode` gate
   uses *observed* running.
3. Auto → Manual while moving → refused; STOP first (anti-goal made
   structural).
4. Post-STOP flow → re-ARM, then tap the badge; START is unchanged;
   left-button faces defined above.
5. Unconfirmed state → `pendingMode` + a frame counter, no clock; timeout or
   a rejecting frame clears it; no automatic retry.
6. Belt in Auto but unconfirmed → command path muted while pending; residual
   case recorded as an accepted unknown that the capture resolves.
7. Initial / link-down `observedMode` → `Unknown` sentinel; reset on link
   loss.
8. Stale test counts in `AGENTS.md` → informational only; refresh.
9. `set_mode` outcome → new `Emit::SetMode` with the mode in `Outcome`;
   `kMaxEmits` unchanged.
10. Policy reference after Auto → `ResetPolicy` emitted on leaving Auto.
