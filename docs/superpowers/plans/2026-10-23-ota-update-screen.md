# Update Screen and CI-Published Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An Update entry in the gear menu that checks for and installs the firmware CI publishes to the GitHub Pages site, with docs and real screenshots.

**Architecture:** `lib/devmenu` gains a sixth root row and an `Update` screen that turn OTA state into rows and two actions; `src/main.cpp` performs them by calling `ota::`, rendering before each blocking call and from a progress callback. `ota::` learns a default manifest URL, an `env` field and a progress callback. CI builds the firmware in the Pages job, writes and verifies a manifest with `tools/make_manifest.py`, and ships both with the site.

**Tech Stack:** C++17 firmware (Unity host tests via `pio test -e native`), Python 3.9+ tool with `unittest`, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-23-ota-update-screen-design.md`

## Global Constraints

- `lib/devmenu` stays Arduino-free and host-tested; it includes no `ota.h`. `src/main.cpp` only wires actions to `ota::` calls.
- Every visually distinct screen has its own screenshot phase; the keyboard uses 64-66, so Update-screen phases are `70 + otaState`. Root stays 60.
- OTA stays developer-mode-only and needs Wi-Fi: with either missing the Update screen's right button is blank and row 2 reads `needs dev mode` / `needs Wi-Fi`.
- Default manifest URL `https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json`; image URL `https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/firmware.bin`. Both spelled out in `docs/ota.md`.
- Manifest fields: `version`, `env`, `url`, `sha256`, `size`. `env` is the PlatformIO env name (`cores3`, `cores3_sim`, `cores3_shot`); the device compiles its own in as `FW_ENV`. Version comparison stays string equality.
- Six root rows: `ROW_Y0 = 28`, `ROW_H = 25`, `ROW_N = 6` (rows end at y 178, below `HIT_Y = 180`). Row order: Developer mode, UDP log, Wi-Fi, stored network, Update, Re-run setup. `kMaxRows = 6`, `kMaxNetworks` stays 5.
- Update screen rows: `Running` / `Available` / `Status` / message (dim, no value) / `Source`; left `BACK`; right `CHECK` (Idle, UpToDate, Failed), `INSTALL` (Available and not env-mismatch), blank otherwise. Status words: `not checked`, `checking...`, `up to date`, `update available`, `image is for <env>`, `installing...`, `failed`.
- Nothing moves the belt unless armed; the menu's existing gate (refused unless a linked belt is parked; disarms on open) is unchanged.
- No BLE shutdown for the download in this plan; the spec's contingency applies only if Task 5 measures heap failures, and then as a ruling.
- Commit footer on every commit: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01BmiMYgcv4b45r7o9z27GZc`.

## Review Focus

1. A tap on INSTALL while the screen still says `update available` for a mismatched env: the button must be blank and the action `None`. Test in Task 1.
2. The Pages site answering 404 (repo still private): CHECK ends in `failed` with `manifest http 404` on row 3, and CHECK is offered again. Task 5 verifies on the bench by pointing at a bad URL.
3. A manifest missing `env` (an older manifest): treated as mismatch (`image is for ?`) rather than installable. Test in Task 2's host-side parse test (the field extractor is in `ota.cpp`; the test goes through `make_manifest.py --check` for the tool side and a unit test on `ota::envMatches("")` is not possible without Arduino, so Task 2 pins it with a console check on the bench in Task 5).
4. A version string longer than the row: `Running` and `Available` values are clipped to the row (the renderer already right-aligns values; a 20-char `v0.2.0-12-gabcdef1` fits at 9 pt). Task 2 checks on glass in Task 4's capture.
5. `ota url` with nothing after it previously cleared to empty; now it restores the default. Console reply says `manifest url <default>` so the change is visible. Task 2.

---

### Task 1: Update screen and sixth root row in `lib/devmenu`

**Files:**
- Modify: `lib/devmenu/devmenu.h`, `lib/devmenu/devmenu.cpp`
- Test: `test/test_devmenu/test_devmenu.cpp`

**Interfaces:**
- Produces: `enum class OtaState : uint8_t { Idle, Checking, UpToDate, Available, Installing, Failed }` (mirrors `ota::State` order); `Inputs` fields `const char* otaVersion = ""`, `const char* otaAvailable = ""`, `OtaState otaState = OtaState::Idle`, `const char* otaMessage = ""`, `const char* otaHost = ""`, `const char* otaImageEnv = ""`, `bool otaEnvMismatch = false`; `Step::Update`; `Action::OpenUpdate`, `Action::CheckOta`, `Action::InstallOta`; `kMaxRows = 6`; root row 4 = Update (value `otaVersion`), row 5 = Re-run setup; `tapRow(4, in)` → `step_ = Step::Update`, returns `Action::OpenUpdate`; on the Update screen `press(true, in)` → `CheckOta` / `InstallOta` / `None` per the button rule, `press(false, in)` → back to Root, `Action::None`; `screen(in).phase == 70 + (int)in.otaState` on the Update screen.

- [ ] **Step 1: Write the failing tests** (Unity, in `test_devmenu.cpp`, using the file's `base(bool devOn)` helper):
  - `test_root_has_update_row_before_rerun_setup`: `screen(on).rowCount == 6`, `rows[4].label == "Update"`, `rows[4].value == "bd3819b"` when `on.otaVersion = "bd3819b"`, `rows[5].label == "Re-run setup"`, `tapRow(5, on) == Action::RerunSetup`.
  - `test_update_row_opens_update_screen`: `tapRow(4, on) == Action::OpenUpdate`; then `screen(on).title == "UPDATE"`, `left == "BACK"`, `rows[0].label == "Running"`, `rows[4].label == "Source"`, `rows[4].value == "ckeller42.github.io"` with `on.otaHost` set.
  - `test_update_button_follows_state`: table over `otaState` → `right`: Idle `CHECK`, Checking `""`, UpToDate `CHECK`, Available `INSTALL`, Installing `""`, Failed `CHECK`; `press(true, on)` returns `CheckOta` for Idle/UpToDate/Failed, `InstallOta` for Available, `None` for the blank cases.
  - `test_update_env_mismatch_disables_install`: `otaState = Available`, `otaEnvMismatch = true`, `otaImageEnv = "cores3"` → `rows[2].value == "image is for cores3"`, `right == ""`, `press(true, on) == Action::None`.
  - `test_update_needs_dev_and_wifi`: `base(false)` → `rows[2].value == "needs dev mode"`, `right == ""`, `press(true) == None`; `base(true)` with `wifiUp = false` → `rows[2].value == "needs Wi-Fi"`.
  - `test_update_status_words`: Idle `not checked`, Checking `checking...`, UpToDate `up to date`, Available `update available`, Installing `installing...`, Failed `failed`; `rows[3].label == otaMessage` and `rows[3].enabled == false`.
  - extend `test_screens_have_distinct_phases`: the Update screen in Idle and Available differ from each other and from root/scan/pass/keyboard pages.
- [ ] **Step 2: Run** `~/tools/venv-ble/bin/pio test -e native -f test_devmenu`. Expected: compile errors (`OtaState`, `Action::OpenUpdate`).
- [ ] **Step 3: Implement** in `devmenu.h`/`.cpp`: the enum, fields, step, actions, the root row, the Update screen builder (`s.phase = 70 + (int)in.otaState`), `tapRow` row 4, `press()` for `Step::Update`, `open()` unchanged (Root). Doc comments in the file's style; the header's top comment ("OTA is driven from the serial console") updated.
- [ ] **Step 4: Run** `~/tools/venv-ble/bin/pio test -e native`. Expected: all pass (214 + 6 new).
- [ ] **Step 5: Commit** `feat(devmenu): Update screen and root row for OTA`.

### Task 2: Firmware wiring: `ota::` additions, six-row layout, console, tour

**Files:**
- Modify: `src/ota.h`, `src/ota.cpp`, `src/config.h`, `platformio.ini`, `src/ui.cpp`, `src/main.cpp`, `docs/serial-console.md`

**Interfaces:**
- Consumes: Task 1's `Inputs` fields, actions and step.
- Produces: `config.h`: `#ifndef OTA_MANIFEST_URL` → `#define OTA_MANIFEST_URL "https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json"`; `platformio.ini`: `-DFW_ENV=\"cores3\"` in `env:cores3`, `\"cores3_sim\"` and `\"cores3_shot\"` in theirs (each env's `build_flags` already extend the parent's; the child sets its own `FW_ENV` after). `ota.h`: `const char* env()` (the compiled `FW_ENV`), `const char* imageEnv()` (the manifest's `env`, `""` if absent), `bool envMatches()` (`strcmp(imageEnv(), env()) == 0`), `void apply(void (*progress)(int percent))` (the old `apply()` stays as `apply(nullptr)`), `bool usingDefaultUrl()`. `ota::begin()`: an empty NVS `ota_url` means `OTA_MANIFEST_URL`; `setUrl("")` stores empty and reports `manifest url <default> (default)`. `check()`: parses `env`; a different or missing env still sets `State::Available` (so the screen can say which image is published) and `envMatches()` false; `apply()` refuses with `State::Failed`, `image is for <env>` when `!envMatches()`. Progress: inside the download loop, call `progress(pct)` whenever the integer percentage changes (the same place `logsink::line("ota: %d%%")` already is).
- `ui.cpp`: `ROW_Y0 = 28, ROW_H = 25, ROW_N = 6`; the row-tap formula and drawing use the constants (no other literal).
- `main.cpp`: `devInputs()` fills the seven `ota*` inputs (`otaHost` = the host part of `ota::url()`, computed into a static buffer; `otaEnvMismatch = !ota::envMatches()` when `state() == Available`); `devMenuAct()` handles `OpenUpdate` (redraw only), `CheckOta` (render the menu once, then `ota::check()`), `InstallOta` (render once, `ota::apply(otaProgress)` where `otaProgress` sets a static `char otaProgressMsg[32]` = `downloading NN%`, feeds it as `otaMessage` and renders). Console: `ota` prints `env=<env> url=<url><" (default)" if default>`. Tour (`demoAdvance`): after the keyboard phases insert four phases with `demoDevIn.otaVersion = "bd3819b"`, `otaHost = "ckeller42.github.io"`, `devMenu.tapRow(4, …)` then `otaState` Idle, then Available (`otaAvailable = "v0.2.0"`, `otaMessage = "v0.2.0 available (running bd3819b)"`), then Installing (`otaMessage = "downloading 37%"`), then Failed (`otaMessage = "manifest http 404"`); the later phases renumber (`kDemoLast` += 4; the two `demoHoldSetMode` comments follow). The badge phases are now 25-27.

- [ ] **Step 1: `ota::` changes** per the Produces block. `pio run -e cores3` builds.
- [ ] **Step 2: `config.h`, `platformio.ini`** per the Produces block; `pio run -e cores3_sim` and `-e cores3_shot` build; confirm `FW_ENV` differs per env with `grep -c 'cores3_sim' .pio/build/cores3_sim/firmware.bin` ≥ 1 and `grep -c 'cores3_sim' .pio/build/cores3/firmware.bin` == 0.
- [ ] **Step 3: `ui.cpp` six rows** and **`main.cpp` wiring** per the Produces block.
- [ ] **Step 4: Console doc** `docs/serial-console.md`: the `ota` row mentions `env` and the default URL; `ota url` with no argument restores the default. markdownlint clean.
- [ ] **Step 5: Verify** `pio test -e native` (count unchanged from Task 1), all three envs build, doxygen ratchet (`doxygen Doxyfile && python3 tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline`) does not go up.
- [ ] **Step 6: Commit** `feat(ota): default manifest URL, env field, progress callback; Update screen wired; six-row menu`.

### Task 3: Manifest tool and CI publishing

**Files:**
- Create: `tools/make_manifest.py`, `tools/test_make_manifest.py`
- Modify: `.github/workflows/pages.yml`, `.github/workflows/firmware.yml`, `.github/workflows/ci.yml` (unittest line)

**Interfaces:**
- Produces: `make_manifest.py FIRMWARE --version V --env E --url U` writes JSON `{"version": V, "env": E, "url": U, "sha256": <hex>, "size": <bytes>}` to stdout (sorted keys, trailing newline); `make_manifest.py --check MANIFEST FIRMWARE` exits 0 when `sha256` and `size` match the file and every field is present and non-empty, else prints the mismatch and exits 1. Python 3.9, stdlib only.

- [ ] **Step 1: Failing tests** (`unittest`, pattern of `tools/test_e2e_sim.py`): `test_manifest_fields_and_hash` (a temp file of known bytes → exact JSON), `test_check_passes_on_matching_manifest`, `test_check_fails_on_size_mismatch` (message names `size`), `test_check_fails_on_missing_env`.
- [ ] **Step 2: Run** `~/tools/venv-ble/bin/python -m unittest tools/test_make_manifest.py`. Expected: ImportError.
- [ ] **Step 3: Implement** the tool. Docstring in the style of `assert_capture.py`.
- [ ] **Step 4: Run** the tests. Expected: 4 pass.
- [ ] **Step 5: CI.** `pages.yml` build job: `actions/checkout` with `fetch-depth: 0`; after the Sphinx step add a PlatformIO cache + `pip install platformio`, `FW_VERSION=$(git describe --tags --always) pio run -e cores3` (export `FW_VERSION` via `echo "FW_VERSION=$(git describe --tags --always)" >> "$GITHUB_ENV"` first), `python3 tools/make_manifest.py .pio/build/cores3/firmware.bin --version "$FW_VERSION" --env cores3 --url https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/firmware.bin > manifest.json`, `python3 tools/make_manifest.py --check manifest.json .pio/build/cores3/firmware.bin`, `mkdir -p build/sphinx/firmware && cp .pio/build/cores3/firmware.bin manifest.json build/sphinx/firmware/`. Replace the "private plan" header comment with one sentence: the site carries `firmware/manifest.json` + `firmware.bin`, which the device's Update screen fetches. `firmware.yml`: after the cores3 build add the same two `make_manifest.py` lines (version `$GITHUB_SHA` short) and include `manifest.json` in the uploaded artifact. `ci.yml` test job: add `python3 -m unittest tools/test_make_manifest.py` next to the e2e unittest line.
- [ ] **Step 6: Verify** `actionlint` if available (`brew list actionlint` or `npx --yes action-validator`), else `python3 -c "import yaml,sys; [yaml.safe_load(open(f)) for f in sys.argv[1:]]" .github/workflows/*.yml`.
- [ ] **Step 7: Commit** `ci: publish firmware.bin and a verified manifest with the Pages site`.

### Task 4: Screenshots and documentation

**Files:**
- Modify: `docs/ota.md`, `docs/developer-menu.md`, `docs/developing.md`, `docs/index.md`
- Replace: `docs/screens/dev-menu-root.png`; Create: `docs/screens/dev-menu-update.png` (Available), `docs/screens/dev-menu-update-failed.png`

**Interfaces:**
- Consumes: Task 2's tour phases.

- [ ] **Step 1: Capture.** Bench: `~/tools/venv-ble/bin/pio run -e cores3_shot -t upload --upload-port /dev/cu.usbmodem101`, then `~/tools/venv-ble/bin/python tools/capture_shots.py --port /dev/cu.usbmodem101 --seconds 140 --outdir <scratch>` (28 phases × 4 s + boot), decode, identify the root (six rows) and the four Update frames by eye (a contact sheet), copy `dev-menu-root.png`, `dev-menu-update.png` (Available) and `dev-menu-update-failed.png` into `docs/screens/`. Check on the frames: six rows fit above the bar, `Running`/`Available` values not clipped, the message row readable. Then flash `cores3_sim` back (`pio run -e cores3_sim -t upload …`).
- [ ] **Step 2: `docs/ota.md`:** rewrite "Updating" (the Update screen: CHECK, INSTALL, the states, `image is for <env>`, the console equivalents), "What CI publishes" (every push to `main`; the two Pages URLs; the manifest fields; `env`; versions from `git describe`, tags optional), keep "Trust model" and "A note on private repositories" rewritten as "Serving locally" (still the way to test an image before it is published). Remove the sentence promising a menu entry that did not exist; add the two screenshots.
- [ ] **Step 3: `docs/developer-menu.md`:** row table gains Update; new section "Update" with `dev-menu-update.png` and the state words; `dev-menu-root.png` caption mentions six rows.
- [ ] **Step 4: `docs/developing.md`:** "Releasing" → "Publishing firmware": pushes to `main` publish; `git tag v0.x.y && git push --tags` names a build; the manifest tool and its `--check`. `docs/index.md`: the docs-site/private-plan note becomes the Pages URL.
- [ ] **Step 5: Verify** markdownlint on the four files; `sphinx-build -q -b html -W docs build/sphinx`; the Stop hook's screenshot check passes (GUI sources and `docs/screens` both changed on the branch).
- [ ] **Step 6: Commit** `docs: Update screen, published firmware, six-row menu screenshots`.

### Task 5: Bench verification of the OTA path

**Files:**
- Modify: `docs/test-log.md` (new section with the measured run)

**Interfaces:**
- Consumes: everything above; the board; `python3 -m http.server`.

- [ ] **Step 1: Local manifest.** Build `FW_VERSION=bench-ota pio run -e cores3`; `python3 tools/make_manifest.py .pio/build/cores3/firmware.bin --version bench-ota --env cores3 --url http://<mac-ip>:8099/firmware.bin > /tmp/ota/manifest.json`; copy the bin beside it; `python3 -m http.server 8099` in that directory (background).
- [ ] **Step 2: Mismatch path on the sim build** (the board runs `cores3_sim`): console `ota url http://<mac-ip>:8099/manifest.json`, then on the device open the gear menu → Update → CHECK. Expected: row 2 `image is for cores3`, right button blank; console `ota` shows `env=cores3_sim`. Record the `ota` line.
- [ ] **Step 3: Real install.** Flash `cores3` built with `FW_VERSION=before-ota`; set the URL as in Step 2; CHECK → `update available`, `Available bench-ota`; INSTALL → progress on screen (`downloading NN%` rising), reboot; after boot the Update row's value reads `bench-ota`. Capture the UDP log (`tools/logcat.py`) for the `ota:` lines and quote the first and last in the report.
- [ ] **Step 4: Failure path.** `ota url http://<mac-ip>:8099/missing.json`; CHECK → `failed`, row 3 `manifest http 404`, right button `CHECK` again.
- [ ] **Step 5: HTTPS with BLE up.** `ota url https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json`; CHECK. If the repository is public by now: expect `up to date` or `update available`; INSTALL is allowed only if `env` matches (`cores3`) and must complete. If still private: expect `failed` with `manifest http 404` (the TLS handshake succeeded, which is the heap question answered). If instead the UDP log shows a TLS/heap failure, stop and report BLOCKED with the lines: the spec's contingency is a controller ruling, not this task's call.
- [ ] **Step 6: Restore** `cores3_sim` on the board; `ota url` (no argument) to restore the default; note it in the report.
- [ ] **Step 7: `docs/test-log.md`:** a section "OTA from the Update screen, 2026-10-23" with the measured lines from Steps 2-5 (counted/observed wording per AGENTS.md).
- [ ] **Step 8: Commit** `docs(test-log): OTA Update screen measured on hardware`.
