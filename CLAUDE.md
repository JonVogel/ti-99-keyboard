# TI-99/4A Keyboard Adapter

USB/BLE keyboard adapter for the TI-99/4A. ESP32-S3 module translates modern keyboard HID reports into the TI's 6x8 keyboard matrix via discrete BSS138 level shifters. Installed inside the TI with the original keyboard removed; powered from the TI's 12V rail through a buck converter.

Sketch: `ti-99-keyboard.ino` (Arduino-ESP32 3.3.7).
PCB: `pcb/ti99-kb-adapter.kicad_pro` (KiCad 10), regeneratable via `pcb/generate_kicad.py`.

## Hardware

- **MCU:** ESP32-S3-DevKitC-1 (older revision with mini-USB ports).
- **Level shifters:** BSS138 discrete MOSFET shifters, BOB-12009 topology (SparkFun). 14 channels total, 10kΩ pull-ups to 3V3 (LV) and 5V (HV).
- **Power:** standalone 12V→5V buck converter from TI's 12V rail; PSU daisy-chains via J13 solder pads + J14 Molex KK-396 male header to the MB plug.

### Why BSS138 and not TXS0108E

The earlier PCB rev used TXS0108E (HW-221 breakouts). It does **not** work for this application:

- TXS0108E's auto-direction detector can't handle open-drain matrix emulation or LED/resistive loads — channels latch, oscillate, or sit indeterminate.
- One TXS0108E experiment damaged GPIO 16 on an ESP32-S3 dev board (latch-up current killed the output pad). Symptom: sketch runs and registers set fine, but the pad doesn't drive. Confirmed by swapping boards.
- See TI app note SCEA040 — TXS0108E is really only meant for push-pull-to-push-pull (I²C/SPI/UART).

BSS138 + pull-ups is the natural topology for a keyboard matrix: bidirectional via MOSFET physics, handles open-drain cleanly, no auto-direction fight. **Do not** suggest TXS0108E anywhere near the keyboard matrix or open-drain GPIO.

Bench validated 2026-04-20 on a 4-channel BOB: clean push-pull 500Hz (0↔3.3V LV, 0↔5V HV), open-drain matrix emulation (sharp falls, RC rises, full-rail swing both sides), static HIGH/LOW hold. No latch-up, no oscillation. Test sketch: `test-level-shifter/test-level-shifter.ino`.

### BOB-12009 footprint reference

Pin silkscreen layout (confirmed from physical board):

- HV side (one long edge): HV1, HV2, HV, GND, HV3, HV4
- LV side (other long edge): LV1, LV2, LV, GND, LV3, LV4

GND and rail pins are directly opposite across the board. Rails (HV/LV) are at position 3 of 6 — **not** at the ends. Channels split left/right around the central power pair.

Row-to-row spacing between LV and HV pin rows is **10.00mm** (measured — not a multiple of 2.54mm). Within each row, pins are 2.54mm pitch.

## Keyboard: RoyalAxe L75 / ProtoArc L75

Same physical product, dual branding. BLE HID keyboard with TFT display.

Quirks:

- TFT defaults to Chinese; needs the Windows driver to change language.
- Advertises as both BT3.0 and BT5.0 — adapter uses BT5.0.
- Changes BLE MAC address per channel (Fn+1/2/3) but shares OUI prefix `f4:ee:25:`.
- After sleep / power cycle / reconnect, advertises with `haveUUID=0` and no name in the primary packet — a UUID-based scan filter cannot match it.

### Reconnect strategy

Save peer address to NVS on first successful connect (Preferences API, namespace `ti99kb`, key `peer_addr`). On subsequent boots, load the saved address and match advertisements **by address rather than UUID**. Reconnection after power-cycling either device is confirmed working.

### BLE library: Bluedroid only

Use the Bluedroid BLE library (Arduino-ESP32 default). **NimBLE-Arduino was tried and abandoned** because reconnect never worked correctly. Do not suggest porting to NimBLE.

### BLE bond management gotcha

Arduino-ESP32 3.3.7's built-in BLE library does **not** expose bond management to sketch code:

- No `removeBond`, no public `getBondedDevices`.
- `esp_gap_ble_api.h` is not on the sketch include path on this distribution.
- Bonds accumulate in NVS until the bond store rotates them out (~8–10 entries max).
- Calling `BLEDevice::deinit(true)` followed by `init()` to clear bonds **crashes** the ESP32 with `LoadProhibited`.

Pairing mode therefore cannot clear individual bonds. It just clears the saved peer address from NVS and restarts the scanner in place.

## Software Alpha Lock (joystick bug fix)

Caps Lock toggles `alphaLockActive`. When set, `processHidReport` injects SHIFT into letter keypresses (HID 0x04–0x1D). `PIN_ALPHA_LOCK` is **never driven** — it stays as `INPUT` for the life of the sketch. This sidesteps the original TI bug where the Alpha Lock line shares electrical state with joystick UP.

## Prior art (independent convergence — not lineage)

Both prior projects were discovered **after** this design was made. Frame the comparison as convergent design (the TI keyboard interface is fixed, so there are only so many sensible solutions), not as inspiration:

- **RAVE 99** (1988) — pure TTL + dual EPROMs, ~20+ chip board, IBM XT/AT keyboards via DIN-5. No longer made. mainbyte.com/ti99/hardware/rave/keyboard.html
- **Jedimatt42 TI-99-USB-Keys** (~2017) — Teensy 3.2 + MAX3421E USB host shield, USB only, custom carrier PCB. Drives the dedicated Alpha Lock line, so it **inherits** the joystick UP bug. Uses active-drive row outputs (digitalWrite HIGH/LOW), not open-drain mode-switching. github.com/jedimatt42/TI-99-usb-keys
- **This project** (2026) — single ESP32-S3 module, USB+BLE, software Alpha Lock (fixes joystick bug), open-drain mode-switching for true matrix coexistence.

## Code style

- Allman braces.
- Mandatory braces on **all** control flow (no single-statement `if`/`for`/`while` without braces).

## Workflow

After a clean Arduino compile, upload automatically — don't ask first.

## PCB history

- **Rev 1 (TXS0108E):** 5 boards from JLCPCB, ~$6.37, ordered 2026-04-12. Superseded — TXS0108E doesn't work for this load (see above).
- **Rev 2 (BSS138):** 5 boards from JLCPCB, $7.69, ordered 2026-04-25. Replaces TXS0108E with BOB-12009 daughterboards, adds TI PSU daisy-chain (J13/J14) and 4 mounting holes. Gerbers committed to repo.

Board: 2-layer, all through-hole, no active components. 0.5mm signal and power traces, GND pour on F.Cu and B.Cu, ~65×65mm.

Next: 3D printed enclosure/mount, test fit with real modules, then a larger order if verified.

When regenerating the schematic from `generate_kicad.py`, note that KiCad's schematic Y increases downward — pin 1 is at the smallest y. Pin formula: `py = y - fy + (k-1) * 2.54`.
