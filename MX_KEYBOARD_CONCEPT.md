# Future project: MX mechanical keyboard with built-in BLE/USB adapter

Status: **concept — parked until V5 is bench-validated.** Captured 2026-07-22.

## The idea

One keyboard-sized PCB that is both an in-case MX mechanical keyboard for the
TI-99/4A *and* the existing USB/BLE adapter: local MX switches, an ESP32-S3,
and the proven 16-channel BSS138 fabric driving the TI's 6×8 matrix. The TI
only ever sees the synthesized matrix; local keys, BLE keyboards, and USB
keyboards are all just input sources.

## Market context

- **shift838 838-MX99** (2026): drop-in MX replacement keyboard, ~$217
  assembled. Its micro does matrix synthesis (evidenced by alpha-lock memory
  and a destructive backspace — impossible with passive switch wiring), i.e.
  the same architecture class as this project's adapter, fed from local
  switches instead of HID. No BLE/USB.
- **Our differentiator:** the in-case mechanical keyboard is *also* the
  wireless adapter — couch typing over BLE, USB host, plus all existing
  firmware features (software alpha lock/joystick fix, FCTN combos,
  QUIT/CLEAR, type-ahead), field-updatable, on a battle-tested codebase.
- The two products can coexist today: an MX99 (or original keyboard) plugs
  into the adapter's J20. The fusion board makes that one PCB.

## Decisions already made

- **No backlight.** Deliberately scrapped — removes the LED chain, the
  brightness UI, and the TI-PSU current-budget problem entirely.
- **Power: regulated +5V straight from the TI PSU** via the J13/J14
  daisy-chain, same as the adapter (the originally planned 12V→5V buck
  proved unnecessary and is not part of any build).
- **TI-facing half is V5 verbatim** — same 16-channel topology (15 lines +
  SPARE), same firmware pin philosophy, but with the BSS138s + resistors as
  on-board parts (~$3) instead of BOB daughterboards.
- **Local keys are a third input source.** A timer-driven scan (structurally
  the same as the existing debounced column sampler) emits key events into
  the same pipeline `processHidReport` feeds. No new synthesis logic.

## Electrical design points

- **Switch matrix:** ~50 keys (TI's 48 + extras à la MX99's added keys),
  7×8 matrix, **one diode per switch** (NKRO).
- **GPIO budget:** S3 has ~28 comfortable GPIOs; the TI fabric uses 16,
  leaving ~12 — three short of a direct 7×8 scan. Answer: hang the switch
  matrix off **chained 74HC165/595 shift registers** (3–4 GPIOs, any matrix
  size) or an **MCP23017 I²C expander** (2 GPIOs, 16 lines). ~$1–2. The
  fabric keeps its pins, including SPARE.
- **USB-OTG (GPIO19/20) stays reserved** so USB host input remains available.

## The long poles (mechanical, not electrical)

- Switch layout matching the TI keyboard opening; plate; stabilizers
  (6U spacebar per MX99 precedent frees room for extra keys).
- Mounting bracket into the TI case (MX99 ships 3D-printed brackets —
  proves feasibility).
- **Keycaps are the hard/expensive part.** No off-shelf TI legends. Paths:
  generic MX caps (budget), or MX-to-ALPS adapters carrying original TI
  keycaps (838's approach — best authentic look).

## Cost sketch

Switches $15–25 · hot-swap sockets ~$5 · keyboard-size 2-layer PCB ~$5 (JLC)
· ESP32-S3 module ~$6 · on-board shifters ~$3 · connectors/stabs ~$10 ·
caps $20–40 → **BOM ≈ $60–90**, against a $217 assembled competitor with
fewer features.

## Path

1. Fab + bench-validate **V5** (proves fabric, firmware, straight ribbon).
2. Keyboard PCB = V5's TI half + switch matrix + scan chain on one board;
   firmware delta = local scan source.
3. Mechanical design (layout/plate/bracket/caps) in parallel — the real
   schedule driver.
4. End state: two SKUs (adapter, keyboard) sharing one firmware and one
   proven channel design.
