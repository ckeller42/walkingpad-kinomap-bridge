# First-time setup over a hotspot

**Status:** design. The owner opted out of the brainstorming questions and asked
for this built and tested end to end on the Fairphone. Everything marked
*(my call)* is a decision I made on their behalf.

## Why

Wi-Fi setup currently happens on the bridge's 320×240 panel, by tapping out a
password on a ten-column on-screen keyboard. Measured against the research, a
key there is **4.1 mm** against the 9–10 mm recommended for thumb input, and
9 mm on this panel would allow four and a half columns — a full keyboard at a
usable size is physically impossible. Typing a WPA2 password on it is a chore
at best and wrong-character-at-a-time at worst, on a credential that fails
silently when mistyped.

The reference is `calictl`'s satellite setup, which this follows closely: the
device opens its own hotspot, the phone's captive-portal notice opens the setup
page by itself, and the user types on **their own keyboard**.

## Goals

1. A bridge with no saved Wi-Fi opens a setup hotspot at boot.
2. Joining it pops the setup page automatically, via the captive-portal check.
3. The page lists nearby networks, takes a password, connects, and reports the
   resulting address.
4. The panel loses Wi-Fi setup entirely and the on-screen keyboard with it.
5. Verified end to end on the Fairphone, not described.

## Non-goals

- **Replacing the panel's operational controls.** ARM, START, STOP and `+`/`−`
  stay. "Status display" means *setup* leaves the panel, not that the thing you
  stand on loses its controls *(my call — removing them would break the working
  product, and they are the only way to drive the belt)*.
- **Moving belt pairing to the web.** The belt wizard needs no typing: it scans
  and connects. It stays on the panel.
- **A general web UI for the bridge.** This is a setup portal. Status, logs and
  control stay where they are.
- **5 GHz, open networks, or WPA-Enterprise.** The ESP32-S3 radio is 2.4 GHz
  only, and the page should say so rather than failing opaquely.

## Anti-goals

- **A setup flow that strands the user.** Every failure needs a way back: a
  wrong password must return to the form with the error, not a blank page.
- **The hotspot staying up forever.** Once connected, it is off — an open-ish
  AP left running is an attack surface and a confusion.
- **Losing the USB fallback.** `wifi <ssid> <pass>` on the console stays, for
  when there is no phone or the portal misbehaves.
- **Breaking the radio policy.** Wi-Fi is still gated behind developer mode for
  *logging*; the setup hotspot is a separate, deliberate exception (below).

## The one real design tension

Today Wi-Fi is off unless developer mode is on, so a shipped bridge never has a
radio up. A setup hotspot contradicts that by definition.

**Resolution *(my call)*:** the hotspot runs only when **no credentials are
stored** *and* the user asks for it. It is not automatic on every boot of a
configured device, and it stops as soon as setup completes or after an idle
timeout. Developer mode still gates the *station* side — joining a network for
UDP logging and OTA — so the policy "a shipped bridge does not sit on your
network" holds.

That means the first-boot experience is: belt wizard on the panel as now, and
Wi-Fi only if and when the user wants logging or OTA.

## Shape

### Hotspot and portal

- SSID `walkingpad-setup`, password `walkingpadsetup` (WPA2 needs 8–63 chars;
  an open AP would be worse). Both shown on the panel while the AP is up, since
  the user needs to read them from the device *(my call)*.
- `DNSServer` answering every query with `192.168.4.1`, which is what makes the
  captive-portal check fire.
- `WebServer` on port 80. Core libraries, no new dependencies.

### Pages

| Route | Purpose |
|---|---|
| `/` | network list, password, Connect |
| `/connect` (POST) | attempt, then redirect to `/status` |
| `/status` | connecting / connected with IP / failed with reason |
| `/rescan` | re-run the scan |
| the captive-portal probe URLs | redirect to `/` |

The probe URLs matter and are easy to get wrong: Android requests
`/generate_204`, iOS `/hotspot-detect.html`. A 302 to `/` on **any** unknown
host is what makes the notice appear.

### What leaves the panel

`devmenu`'s `WifiScan` and `WifiPass` steps, the on-screen keyboard in
`ui.cpp`, and their tests. The developer menu keeps the dev-mode and UDP
switches, the protocol row, and gains a row that starts the setup hotspot.

## How it gets verified

End to end on the Fairphone, with evidence rather than assertion:

1. Bridge with credentials cleared opens the hotspot; the panel shows the name
   and password.
2. The Fairphone joins it and the setup page appears **by itself** — screenshot.
3. The page lists the real network; the password is typed on the phone.
4. The bridge joins; the page reports the address — screenshot.
5. `log` over USB confirms `wifi=1` and the same IP.
6. The hotspot is gone afterwards.

Step 2 is the one that actually distinguishes this from "a web page on a
device": if the captive-portal notice does not fire, the user has to be told an
IP address, which is the thing the whole design exists to avoid.
