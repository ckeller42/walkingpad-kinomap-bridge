# Update screen and CI-published firmware — design (2026-10-23)

## Intent

The owner asked why the settings menu has no OTA entry, and then asked for
three things at once: an Update entry in the menu, firmware images that CI
builds and that the bridge installs directly over Wi-Fi, and the docs and
screenshots brought up to date. Today the OTA module works (verified end to
end against a local HTTP server), but it is reachable only from the serial
console, `docs/ota.md` promises a menu entry and a tag-driven release that do
not exist, and nothing CI builds is reachable by the device: the repository
is private, so GitHub Pages is off and release assets need a login.

The owner decided to **make the repository public**. That is what makes the
rest simple: GitHub Pages comes alive, the existing `pages.yml` deploys the
docs site, and the same deployment can carry `firmware/firmware.bin` and
`firmware/manifest.json` at a stable public URL the device can fetch with a
plain HTTPS GET. No secrets, no tokens on the device, no second repository.

## Goals

- From the gear menu, the owner sees which firmware runs, whether a newer
  build is published, and installs it with two taps, with progress and the
  outcome on the screen.
- Every push to `main` publishes the firmware it built, with its manifest, to
  the Pages site. The device's default manifest URL points there, so a
  bridge works out of the box with no `ota url` typed on a console.
- `docs/ota.md`, `docs/developer-menu.md` and `docs/developing.md` describe
  what exists; `docs/screens/` carries real captures of the new root row and
  the Update screen.

## Non-goals

- Signed images. The trust model stays "integrity, not authenticity"; the
  docs keep saying so. Making the manifest URL public does not change it:
  whoever controls the Pages deployment controls the image, which is the
  repository's CI.
- Automatic update checks or installs. Nothing happens unless the owner
  taps CHECK and then INSTALL.
- Rollback UI. The bootloader's fallback to the previous slot on a failed
  boot is unchanged and not surfaced.
- Keeping the OTA path usable while the repository is private. The owner
  chose public; the design does not carry a token path.

## Anti-goals

- Installing while the belt can move. The menu already cannot be opened
  unless a linked belt is parked, opening it disarms, and `ota::apply()`
  blocks for seconds. The Update screen adds nothing that relaxes this.
- A screen that says "installing" forever. Every state the OTA module can be
  in (`Idle`, `Checking`, `UpToDate`, `Available`, `Installing`, `Failed`)
  has a visible word and, for `Failed`, the module's message.
- Shipping a manifest whose hash does not match the binary beside it. CI
  generates the manifest from the binary it just built and re-verifies it
  before deploying.

## Constraints (from the project)

- `lib/devmenu` stays Arduino-free and host-tested: the menu produces rows,
  labels and `Action`s; `src/main.cpp` performs them by calling `ota::`.
- Every visually distinct screen gets its own screenshot phase, and
  `docs/screens/` holds real captures from the `cores3_shot` tour.
- OTA only in developer mode and with the radio up (unchanged): the Update
  screen's CHECK/INSTALL buttons are disabled with a reason otherwise.
- `FW_VERSION` is stamped at build time; a local build is `dev-local` and
  never matches a manifest. Version comparison is string equality: any
  published version other than the running one is "available".
- The OTA client does not follow HTTP redirects. Pages URLs are direct, which
  is one reason to use Pages rather than GitHub release downloads.

## What gets built

### 1. The menu

**Root screen** gains a sixth row, `Update`, whose value is the running
version (`ota::version()`, e.g. `bd3819b` or `v0.2.0`; `dev-local` for a
bench build). Enabled always (the screen itself explains what is missing).
Six rows need `ROW_H` 26 px (rows end at y 186, bar starts at 184 today →
`BAR_Y` stays; rows are `ROW_Y0 + 6*26 = 186`, so the last row overlaps the
bar by 2 px: move `ROW_Y0` to 28 or shrink to 25 px; the builder picks and
checks on glass). Row order: Developer mode, UDP log, Wi-Fi, stored network,
Update, Re-run setup.

**Update screen** (`Step::Update`, title `UPDATE`, left `BACK`, right
`CHECK` / `INSTALL` / blank):

| row | label | value |
|---|---|---|
| 0 | Running | `ota::version()` |
| 1 | Available | the manifest's version after a check, else `-` |
| 2 | Status | `not checked` / `checking...` / `up to date` / `update available` / `installing...` / `failed` |
| 3 | the module's message (`ota::message()`), dim, no value | |
| 4 | Source | the manifest URL's host (`ckeller42.github.io`) or `not set` |

Right button: `CHECK` in `Idle`, `UpToDate`, `Failed`; `INSTALL` in
`Available`; blank (grey, no label) in `Checking` and `Installing`. When
developer mode is off or Wi-Fi is down the button is drawn dim and a tap
produces no action; row 2's value then reads `needs dev mode` or
`needs Wi-Fi` instead of the state word, so the reason is on the screen.

`Action::CheckOta` and `Action::InstallOta` are the two new actions.
`Inputs` gains `otaVersion`, `otaAvailable`, `otaState` (a `devmenu::OtaState`
enum mirroring `ota::State`, so `lib/` does not include `ota.h`),
`otaMessage`, `otaHost`, `otaEnvMismatch` (see "Which builds" below).

**Progress.** `ota::check()` and `ota::apply()` block `loop()` (10 s and the
whole download respectively), so the 1 Hz redraw cannot show progress by
itself. Two small changes make it honest: `main.cpp` renders the Update
screen once *before* calling either (so `checking...` / `installing...` is
on the glass while the call blocks), and `ota::apply()` takes a progress
callback `void (*)(int percent)` that it calls from its download loop (same
task, no threading); `main.cpp`'s callback updates the message
(`downloading 37%`) and renders. After `apply()` succeeds the device reboots;
the screen is simply gone. On failure the screen shows `failed` and the
module's message; BLE links are kept up throughout (nothing is shut down for
the download), see the contingency under "Calls I made".

**Which builds.** CI publishes only the `cores3` image, and the version check
is string equality, so a bench board running `cores3_sim` or `cores3_shot`
would see `update available` and INSTALL would replace it with real-belt
firmware. The manifest therefore carries `env` (`cores3`), each PlatformIO
env compiles its name in (`-DFW_ENV="cores3_sim"` etc.), and `ota::check()`
reports a mismatch as `State::Available` with `otaEnvMismatch` set: the row
reads `image is for cores3` and INSTALL stays disabled. A `dev-local` build
compares versions like any other.

Screenshot phases: the keyboard pages already use 64-66, so the Update
screen uses 70 + state (`Idle`, `Available`, `Installing`, `Failed` are the
four the tour shows).

The root screen keeps phase 60 (its content changed, the tour recaptures
it); the tour's Update-screen phases use synthetic inputs like the Wi-Fi
rows do.

### 2. Firmware default URL

`src/config.h`: `OTA_MANIFEST_URL` defaults to
`https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json`
and is overridable with `-DOTA_MANIFEST_URL=...`. `ota::begin()` uses it when
NVS holds no `ota_url`; `ota url …` on the console still overrides and
`ota url` with nothing clears back to the default (today it clears to empty).
`ota` on the console and the Update screen's Source row show which is in use.

### 3. CI

`pages.yml`'s build job also builds the firmware and ships it with the site:

1. `pio run -e cores3` with `FW_VERSION=$(git describe --tags --always)`.
2. `tools/make_manifest.py .pio/build/cores3/firmware.bin --version "$FW_VERSION" --env cores3 --url https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/firmware.bin > manifest.json` (fields `version`, `env`, `url`, `sha256`, `size`). The checkout fetches full history (`fetch-depth: 0`) so the describe step sees tags.
3. `tools/make_manifest.py --check manifest.json firmware.bin` re-hashes and
   fails the job on any mismatch.
4. Copy both into `build/sphinx/firmware/` before `upload-pages-artifact`.

`firmware.yml` keeps building on pull requests (the 30-day artifact) and
additionally runs the manifest tool with `--check` so a broken manifest is
caught before merge. The pages workflow's "private plan" comment is removed.
A git tag (`v0.2.0` style) is optional: `git describe` makes untagged builds
`<sha7>` and tagged ones `v0.2.0` or `v0.2.0-3-g<sha7>`; the owner tags when
they want a name. `FW_VERSION` is also shown on the wizard's last step? No:
only on the Update row (keeps the wizard untouched).

### 4. Documentation and screenshots

- `docs/ota.md`: rewrite the "Updating" and "What CI publishes" sections for
  the Update screen, the Pages URL, the default manifest URL, the version
  scheme, and keep the trust model and local-serving sections.
- `docs/developer-menu.md`: the row table gains Update; a new section shows
  the Update screen and its states.
- `docs/developing.md`: "Releasing" becomes "Publishing firmware" (every
  push to main; tagging is naming).
- `README.md`: one line under features or setup: updates over Wi-Fi from the
  gear menu.
- `docs/screens/dev-menu-root.png` recaptured (six rows);
  `dev-menu-update.png` (Available) and `dev-menu-update-failed.png` added.
- The repository's `pages.yml` comment and `docs/index.md` reference to the
  docs-site artifact updated once Pages is live.

## Calls I made

- Pages, not GitHub Releases, as the public location: no redirects for the
  OTA client, one deployment that already exists, and the docs site comes
  with it. Releases can be added later for humans without touching the
  device.
- Publish on every push to `main`, not only on tags. The owner asked for
  "CI generates the images and they can be installed directly"; a tag step
  in between is a ceremony the project does not need yet.
- Six root rows at a smaller height rather than a merge of the two Wi-Fi
  rows or a scrolling list. The rows stay readable at 9 pt; the touch targets
  shrink to ~3.3 mm, which the UX review already noted as tight for the 30 px
  rows, so this is checked on glass and the tour captures it.
- The install button says `INSTALL`, not `APPLY`: the console keeps
  `ota apply` for compatibility, the screen says what will happen.
- No automatic check on menu open: a network fetch on a tap the owner meant
  as "look at the switches" is a surprise; CHECK is one tap away.
- Progress by callback from the blocking download, not a FreeRTOS task:
  it is a dozen lines and keeps `ota::` single-threaded.
- Bench builds refuse the real image by an `env` field rather than by not
  using the default URL: the sim board should still be able to *see* what
  is published, it just must not install it.
- HTTPS with BLE up is attempted as is. OTA was verified only over plain
  HTTP to a local server; the TLS handshake to github.io with NimBLE, the
  display and the log sinks running has not been measured. Contingency,
  only if the bench run shows heap failures: the install path disconnects
  both BLE links before the download (the reboot after success drops them
  anyway) and on failure the screen says so and offers `REBOOT`, which is
  the clean way back to a linked bridge. This contingency is a ruling the
  plan carries, not a feature to build up front.
- This merges before the repository is public. Until the owner flips
  visibility the default URL answers 404 and CHECK shows
  `failed: manifest http 404`, which is honest; `pages.yml` already fails at
  its deploy step on the private plan today, so adding the firmware step to
  its build job makes nothing redder. The `docs/index.md` note and the
  `pages.yml` comment are updated in this change, worded for the public
  state.

## Left to the builder

- Exact `ROW_Y0`/`ROW_H` for six rows and the touch zone update in `ui.cpp`.
- Where the Pages base URL lives in CI (a workflow `env:` is enough; it also
  appears in `config.h`, both spelled out once in `docs/ota.md`).
- The tour's synthetic inputs for the Update screen states.

## Order and dependencies

The owner flips the repository to public (a GitHub setting; nothing in the
repository changes). Until then the Pages workflow still cannot deploy, but
everything else builds and is testable: the Update screen against a local
`http.server` manifest (as OTA was first verified), the manifest tool on the
host, and the screenshots from the tour. The sim-belt E2E harness plan is
in flight on the same branch and uses the board; this work waits for it to
finish before touching the bench.
