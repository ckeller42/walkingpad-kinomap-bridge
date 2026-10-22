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
7. **Remember the preference**: after a STOP (which parks the belt in Standby
   and so leaves Auto), the next START while armed re-requests Auto.
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

- New state: `observedMode` (last status byte 4, as `wilink::Mode`) and
  `wantAuto` (the walker's preference, a bool).
- Derived: `autoActive() == (observedMode == Mode::Auto)`.
- New entry point `requestMode(Mode m, bool beltRunning)` → `Outcome`.
  Gate: `armed && beltUp && !beltRunning`; otherwise emits nothing. Sets
  `wantAuto = (m == Auto)` and emits `set_mode m`.
- New entry point `observeMode(Mode m)` → `Outcome`. Updates `observedMode`.
  An **external** change (one the bridge did not request) emits nothing —
  hands-off. Transitions are logged by `main.cpp`.
- **Structural mute**: every speed-producing entry point (`onTarget` /
  SetSpeed, UI `+`/`-`, the phone `0x07` translation) returns an empty
  `Outcome` while `autoActive()`, exactly as they do while disarmed. One gate,
  in policy, not scattered `if`s in `src/`.
- STOP (UI, phone, link loss) emits Standby as today in every mode, and
  **preserves** `wantAuto`.
- START while `armed && wantAuto && !autoActive()` emits `set_mode Auto`
  (re-request) instead of the Manual start sequence. The walker then steps
  into the front zone to start the belt.
- **Unconfirmed request**: after emitting `set_mode Auto`, if no status frame
  reports mode 0 within the confirmation window, the badge stays *unconfirmed*
  and the bridge keeps its Manual personality. The walker may retry or use the
  belt's A key. Nothing else changes.

### Screen

- Badge in the **top-bar centre** (between BELT and PHONE; the battery gauge
  is top-left). States: **`M`** green = Manual · **`A`** cyan = Auto
  (observed) · **`A?`** amber = Auto requested, not yet confirmed. Greyed while
  disarmed; a tap then does nothing.
- While Auto is observed: `+`/`-` drawn inert (grey), the `ARMED` tag reads
  `AUTO`, the left bottom button shows `DISARM` only (no `START`). `STOP`
  unchanged.
- The badge is a **tap** target (visible affordance; no hidden gesture).
- The screenshot tour (`cores3_shot`) gains phases for Manual, Auto and
  unconfirmed.

### Kinomap

- Read path unchanged.
- FTMS StartResume (`0x07`): while `autoActive()` → no-op (no `set_speed`).
  While `armed && wantAuto && !autoActive()` → same as START (re-request Auto).
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
- `requestMode` emits nothing when disarmed, when belt down, or when running;
  emits `set_mode` otherwise and records `wantAuto`.
- `autoActive()` is true only after `observeMode(Auto)`, never after a bare
  `requestMode(Auto)`.
- While `autoActive()`: `onTarget`, UI `+`/`-` and phone start emit nothing;
  STOP emits Standby.
- External `observeMode(Auto)` while disarmed emits nothing (hands-off).
- After STOP with `wantAuto`, START emits `set_mode Auto`, not the Manual
  start sequence.
- `wantAuto` survives STOP; is cleared by `requestMode(Manual)`.

## Left to the builder

- The confirmation window for an unconfirmed Auto request (suggest ~3 s or 5
  status frames — pick one, name the constant).
- Exact badge geometry in the top bar (centre gap is roughly x 106..173,
  y 0..27).
- Whether leaving Auto needs an explicit `set_mode Manual` or whether the
  existing Manual start sequence (which already writes Manual) suffices.
- Log line format for mode transitions (verbatim hex + decoded mode, per the
  evidence discipline).
