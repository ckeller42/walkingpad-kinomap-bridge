# UX review of the on-device screens

Against freshly captured screenshots from a `cores3_shot` build, 2026-10-21.
The panel is 320×240 and is read at arm's length by someone who is walking, so
the bar for legibility is higher than for a desk UI and the tolerance for
hidden controls is lower.

## Two layout defects found and fixed

The screenshots in the repository had been captured **before** the `+`/`−`
buttons were added, so they showed a main screen that no longer existed. Once
re-captured, two real defects were visible that nobody had seen:

1. **The `+` button erased half the `km/h` label.** The unit was drawn at
   y≈134–152 and the button spans y 44–140. Both were correct in isolation;
   the readout is simply painted first, so the button paints over it.
2. **The speed digits were clipped by both side buttons.** Font7 at size 2.3
   made `0.0` wider than the 176 px gap between the buttons.

Both are now fixed: the readout is sized to fit the gap, and `km/h` moved clear
of the `+` button. This is the whole argument for screenshots being real
captures rather than mock-ups — a mock-up would have been drawn correctly.

It is also why the screenshots now have a `Stop` hook guarding them
(`.claude/hooks/gui-docs-check.sh`): the GUI being newer than its PNGs is
exactly how this happened, and nothing in CI could see it.

## What works, and why

- **The speed readout is the right thing to make huge.** It is the one value a
  walker checks mid-stride; everything else can be squinted at.
- **STOP is always present, always red, always live.** It works whether or not
  the bridge is armed. A stop control that can be in the wrong mode is not a
  stop control.
- **`SIM BELT` is amber, not green.** A simulated belt must never read as a
  healthy real one — colour alone carries that, before any text is read.
- **The wizard template is consistent**: title, `STEP n OF 5`, the action in
  bold white, the reasoning in grey, two buttons. Action and rationale are
  visually separated, so the screen can be obeyed without being read.
- **Failure screens stay actionable.** "Belt not found yet" says what to do
  ("Switch belt OFF and ON, close its phone app"), shows how long it has been
  trying, and offers SKIP rather than trapping the user.

## Open issues, in order of how much they would cost a user

### 1. `ARMED` is a state word on an action button

The left button cycles **ARM → START → ARMED**. The first two name an action;
the third names a state, and tapping it *disarms*. A user who wants to stop
arming has no label telling them that tapping is how.

Worth considering: keep the state in the top bar and let the button always name
its action (`ARM` / `START` / `DISARM`).

### 2. The two hidden taps are undiscoverable

Tapping the **BELT** indicator re-opens setup; tapping **PHONE** opens the
developer menu. Neither has any visual affordance. The BELT tap is mentioned
once, on the last wizard screen, which a user sees exactly once. The PHONE tap
is documented nowhere on the device.

These are reasonable as *expert* gestures, but at least the developer menu
should be findable without reading the source.

### 3. Pressing `+` appears to do nothing for a second or two

The big number shows the **actual** belt speed, which lags a command by a poll
or two. Press `+` and nothing changes, so the natural response is to press
again — and overshoot. The bridge already tracks the commanded value
(`lastCmdTenths`); the screen does not show it.

Showing the target faintly beside or under the actual speed while they differ
would close the loop without adding a mode.

### 4. Distance and time are unlabelled

`0.00 km` carries its unit; `00:08` does not say whether it is elapsed or
remaining. Minor, and probably inferable, but free to fix.

### 5. On the idle screen the brightest control is the one you do not want

Disarmed, `STOP` is bright red and `ARM` is plain grey, so the eye lands on the
control that does nothing useful yet. Defensible as a safety stance — STOP
should never be hard to find — but the visual hierarchy is inverted relative to
intent.

## Not a defect

- **`km/h` sits close to the readout's lower right.** It is legible and no
  longer clipped; the tightness is deliberate, to keep the digits as large as
  possible.
- **`PHONE` showing red in these captures** is correct: no phone is paired to a
  screenshot build.
