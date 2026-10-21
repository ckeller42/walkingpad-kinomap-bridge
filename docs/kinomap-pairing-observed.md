# How Kinomap actually pairs a treadmill — observed, 2026-10-21

App: Kinomap 4.7.1 (versionCode 654), native Android, on a Fairphone 6 / Android 16.
Driven headlessly over ADB. **No belt or bridge present during this run** — this is
the baseline the bridge will be measured against.

## The flow

1. **Login wall.** No pairing UI at all before sign-in.
2. **"YOUR MACHINE"** — pick one of BIKE TRAINER / EXERCISE BIKE / **TREADMILL** /
   ELLIPTICAL / ROWING.
3. **Brand picker** — "If you have a connected machine, select the brand of your
   equipment", a search box, and a long brand list. Also an **"I CAN'T FIND MY
   BRAND"** button, and "Inform my machine later".
4. **"CONNECTION"** — choosing *I can't find my brand* gives:
   > "We will search for your machine using the most common protocols."
   > "This operation may take a little longer, but there is no guarantee that you
   > will be able to connect your machine."
5. **LAUNCH SEARCH** → asks for **location** (`ACCESS_FINE_LOCATION`) and
   **nearby devices** (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`), then scans.
6. **"FOUND EQUIPMENT"** — a list of devices, each as *name* + *MAC address*,
   with a radio button to select one.

## The three findings that matter for the bridge

**1. The brand list is not a gate.** There is an explicit unbranded path that
searches "the most common protocols". The bridge does **not** need to impersonate
a supported brand — which also means we never have to clone another vendor's
identity.

**2. The generic scan does not filter by service UUID.** The FOUND EQUIPMENT list
contained a power bank, two lighting controllers and an environment sensor —
none of which advertise FTMS. Kinomap lists essentially every advertiser and
lets the user choose, then presumably probes the chosen device's GATT.

*Consequence:* the bridge will appear in that list on the strength of advertising
at all. Discovery is not the risk. **Acceptance after connection is** — and that
is where `2ACC` feature bits and the `2AD9` control point have to be right.

*Second consequence:* give the bridge an unmistakable advertised name, because
the user picks it out of a list of neighbours' gadgets by name.

**3. Android shows MAC addresses, macOS does not.** The phone lists real BD_ADDRs;
`tools/blescan.py` on the Mac sees per-Mac CoreBluetooth UUIDs instead. So the
phone is the better instrument for confirming *which* radio we are looking at,
and the Mac is the better one for reading GATT tables.

## Not yet established

- What Kinomap does **after** you select a device: which characteristics it reads,
  whether it requires `2AD4` Supported Speed Range, and what it demands in the
  `2ACC` feature bitfield before it will send `2AD9` commands. That needs either
  the decompile (in progress, `~/src/kinomap-decompile`) or a live bridge.
- Whether the brand path and the generic path end up in the same protocol
  handler.

## Note on evidence

Screenshots of the scan screen show nearby third parties' device names and MAC
addresses, so they are deliberately **not** committed.
