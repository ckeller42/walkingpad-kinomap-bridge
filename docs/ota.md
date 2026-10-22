# Firmware updates (OTA)

The bridge can update itself over Wi-Fi, pulling from a published manifest.

**Pull, not push**: nothing has to be able to reach the bridge, which matters
because it lives on a home network behind NAT and its radio is off by default.

## Trust model, stated plainly

The downloaded image is hashed while it streams and compared with the SHA-256
in the manifest **before anything is committed**, so a corrupted or truncated
download cannot be installed.

That is an **integrity** check, not an **authenticity** one. Whoever controls
the manifest URL controls what the device installs, and TLS certificates are
not pinned. Signed images are the right answer and are not implemented.

Guarded accordingly:

- only in **developer mode**, so a shipped bridge never has the radio up;
- only with the **belt stopped**, since the download blocks for seconds and the
  belt link goes stale meanwhile;
- written to the **inactive app slot**, so a failure leaves the running
  firmware untouched and the bootloader can fall back.

## Updating

```text
dev on
ota url https://example.invalid/releases/download/v0.2.0/manifest.json
ota check
ota apply
```

`ota check` compares the manifest's version with the running one and reports
`up to date` or `<version> available`. `ota apply` downloads, verifies and
reboots. `ota` alone shows the current state at any time.

The same is reachable from the developer menu on the device.

## What CI publishes

Pushing a tag builds the firmware, stamps it with the tag, runs the host tests
and publishes two files:

| File | |
|---|---|
| `firmware.bin` | the image |
| `manifest.json` | `version`, `url`, `sha256`, `size` |

A release step then re-hashes the binary and asserts the manifest describes the
file beside it. A manifest whose hash does not match its own binary would
strand every device that tried to update, and it is exactly the kind of mistake
a release pipeline makes quietly.

## A note on private repositories

A **private** repository's release assets require authentication, which the
device does not have. Either publish the manifest and binary somewhere the
device can reach, or serve them on the local network:

```sh
python3 -m http.server 8099      # in a directory holding firmware.bin + manifest.json
```

Then point the device at `http://<your-ip>:8099/manifest.json`. This is also
how OTA was first verified end to end: the bridge fetched a 1.5 MB image,
checked its hash, wrote the inactive slot and rebooted into it.

## Versioning

`FW_VERSION` is stamped by CI from the git tag. A local build has no tag, and
rather than comparing as an empty string — which would match an empty manifest
field and report itself "up to date" — it reports `dev-local`, a name no
release can take.
