# walkingpad-kinomap-bridge

[![CI](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/ci.yml/badge.svg)](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/ci.yml)
[![firmware](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/firmware.yml/badge.svg)](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/firmware.yml)

A BLE bridge for an M5Stack CoreS3 (ESP32-S3): it talks to a WalkingPad belt
as a BLE central and presents itself to Kinomap as a treadmill.

Protocol and policy logic lives in Arduino-free `lib/` modules with native
Unity tests; BLE and GUI adapters live in `src/`.

```sh
pio test -e native      # host tests
pio run -e cores3       # real belt (default)
pio run -e cores3_sim   # simulated belt (-DUSE_SIM_BELT=1)
pio run -e cores3_shot  # simulated belt + serial screenshots
```

On first boot the screen runs a setup wizard ([docs/setup-wizard.md](docs/setup-wizard.md),
with real screenshots). The bridge can be driven over USB serial for testing
([docs/serial-console.md](docs/serial-console.md)).

See `ENVIRONMENT.md` and `docs/` for details.
