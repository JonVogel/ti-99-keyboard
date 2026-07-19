#!/usr/bin/env python3
"""
Generate KiCad 10 project files for TI-99/4A Keyboard Adapter carrier board.

This board is a simple carrier that sockets off-the-shelf modules:
  - ESP32-S3 DevKitC-1 LBGE (via 2x 1x22 pin sockets, 25.4mm row spacing)
  - 4x BSS138 4-channel level shifter breakouts (BOB-12009 style),
    each via 2x 1x6 pin sockets at 10mm row spacing
  - 1x15 pin header for TI-99/4A keyboard ribbon
  - 1x2 pin header for 5V power input

The PCB has no active components -- just headers and copper traces.

History: previous rev used 2x TXS0108E modules, but TXS auto-direction
detection fails on open-drain matrix emulation (latch-up, indeterminate
levels, documented pad damage). BSS138 + pull-ups is the correct topology
for a matrix; validated on bench 2026-04-20.

BOB-12009 (SparkFun BSS138) socket pin mapping, confirmed from physical board:
  LV socket pin: 1=LV4, 2=LV3, 3=GND, 4=LV(3V3), 5=LV2, 6=LV1
  HV socket pin: 1=HV4, 2=HV3, 3=GND, 4=HV(5V),  5=HV2, 6=HV1
Channel-to-socket-pin: CH1=pin 6, CH2=pin 5, CH3=pin 2, CH4=pin 1
GNDs at pin 3, rails at pin 4. GND and rails are directly opposite
across the board.

Net naming: the LV and HV sides of each BOB are electrically isolated
through the BSS138 MOSFET, so LV-side pins get a "<sig>_LV" net while
HV-side pins get the bare "<sig>" net. Same-name nets would short LV
to HV in the schematic.

Channel allocation (14 of 16 used). Chosen for contiguous, non-crossing
trace flow: each BOB takes a sequential block of GPIOs AND a sequential
block of TI pins, with BOBs ordered top-to-bottom matching both:
  BOB#1 (top):    GPIO 4-7   <-> TI 1-4
  BOB#2:          GPIO 15-18 <-> TI 5,7,8,9   (TI 6 NC, Alpha Lock)
  BOB#3:          GPIO 9-12  <-> TI 10-13
  BOB#4 (bottom): GPIO 13-14 <-> TI 14-15     (CH3/CH4 unused)

Usage:
  python generate_kicad.py
  Open ti99-kb-adapter.kicad_pro in KiCad 10.
  Press F8 (Update PCB from Schematic) to create the board layout.
  Footprint suggestion: Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical
  for each BOB side. Place the two sockets per BOB 10mm apart.
"""

import uuid
import json
import os
import sys

# Allow `python generate_kicad.py` from any cwd by ensuring this script's
# directory is on the import path so generate_parts can be imported.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate_parts import bob_12009_symbol, esp32_symbol  # noqa: E402

PROJECT = "ti99-kb-adapter"


def uid():
    return str(uuid.uuid4())


# ---------------------------------------------------------------------------
# KiCad symbol definition for Connector_Generic:Conn_01xNN
# ---------------------------------------------------------------------------
def conn_sym_def(n):
    """Return embedded symbol definition for a 1xN connector."""
    name = f"Connector_Generic:Conn_01x{n:02d}"
    sub = f"Conn_01x{n:02d}_1_1"
    fy = (n - 1) / 2 * 2.54          # y of pin 1 (top)
    rt = fy + 1.27                     # rectangle top
    rb = -(fy + 1.27)                  # rectangle bottom

    pins = []
    for k in range(1, n + 1):
        py = fy - (k - 1) * 2.54
        pins.append(
            f"        (pin passive line (at 5.08 {py:.2f} 180) (length 3.81)\n"
            f'          (name "Pin_{k}" (effects (font (size 1.27 1.27))))\n'
            f'          (number "{k}" (effects (font (size 1.27 1.27)))))'
        )

    return (
        f'    (symbol "{name}"\n'
        f"      (pin_names (offset 1.016) hide)\n"
        f"      (exclude_from_sim no) (in_bom yes) (on_board yes)\n"
        f'      (property "Reference" "J" (at 0 {rt + 1.27:.2f} 0)\n'
        f"        (effects (font (size 1.27 1.27))))\n"
        f'      (property "Value" "Conn_01x{n:02d}" (at 0 {rb - 1.27:.2f} 0)\n'
        f"        (effects (font (size 1.27 1.27))))\n"
        f'      (property "Footprint" "" (at 0 0 0)\n'
        f"        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        f'      (property "Datasheet" "~" (at 0 0 0)\n'
        f"        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        f'      (symbol "{sub}"\n'
        f"        (rectangle (start -1.27 {rt:.2f}) (end 1.27 {rb:.2f})\n"
        f"          (stroke (width 0.254) (type default))\n"
        f"          (fill (type background)))\n"
        + "\n".join(pins) + "\n"
        f"      )\n"
        f"    )"
    )


# ---------------------------------------------------------------------------
# Schematic builder
# ---------------------------------------------------------------------------
class Schematic:
    def __init__(self):
        self.sheet_uuid = uid()
        self.needed_sizes = set()
        self.needed_parts = set()   # custom parts (lib_id strings) we've placed
        self.components = []
        self.labels = []
        self.wires = []
        self.ncs = []
        self.texts = []

    # -- Add a connector component, returns {pin_num: (x, y)} --
    def add_conn(self, ref, n, x, y, footprint, value, mirror=False,
                 val_pos=None):
        """Add a Conn_01xNN symbol instance.

        val_pos: optional (vx, vy, vangle) for the Value label.
                 Defaults to below the symbol at angle 0.
        """
        self.needed_sizes.add(n)
        lib_id = f"Connector_Generic:Conn_01x{n:02d}"
        iid = uid()
        fy = (n - 1) / 2 * 2.54

        mirror_line = "\n    (mirror y)" if mirror else ""

        if val_pos is None:
            vx, vy, va = x, y - fy - 3.81, 0
        else:
            vx, vy, va = val_pos

        pin_lines = []
        pin_pos = {}
        for k in range(1, n + 1):
            # KiCad schematic Y increases downward; symbol pin 1 is at the
            # top (smallest y).  Pin k offset = (k-1)*2.54 below pin 1.
            py = y - fy + (k - 1) * 2.54
            px = (x - 5.08) if mirror else (x + 5.08)
            pin_pos[k] = (px, py)
            pin_lines.append(f'    (pin "{k}" (uuid "{uid()}"))')

        self.components.append(
            f"  (symbol\n"
            f'    (lib_id "{lib_id}")\n'
            f"    (at {x:.2f} {y:.2f} 0){mirror_line}\n"
            f"    (unit 1)\n"
            f"    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)\n"
            f'    (uuid "{iid}")\n'
            f'    (property "Reference" "{ref}" (at {x:.2f} {y:.2f} 0)\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Value" "{value}" (at {vx:.2f} {vy:.2f} {va})\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Footprint" "{footprint}" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            f'    (property "Datasheet" "~" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            + "\n".join(pin_lines) + "\n"
            f"    (instances\n"
            f'      (project "{PROJECT}"\n'
            f'        (path "/{self.sheet_uuid}" (reference "{ref}") (unit 1))\n'
            f"      )\n"
            f"    )\n"
            f"  )"
        )
        return pin_pos

    # -- Add a 2-unit multi-part symbol (e.g. BOB-12009, ESP32-S3-N16R8) --
    def add_part2(self, ref, lib_id, value, n_per_unit, fp,
                  x1, y1, x2, y2,
                  mirror1=False, mirror2=False,
                  val_pos1=None, val_pos2=None):
        """Place a 2-unit symbol. Returns (unit1_pins, unit2_pins).

        Each unit lays out like a 1xN connector. Returned dicts use LOCAL
        pin numbering (1..n_per_unit) so wiring code stays clean. KiCad
        pad numbers are 1..N for unit 1 and N+1..2N for unit 2.

        Both unit instances share the same Reference designator but have
        different (unit N) tags, which is how KiCad represents multi-unit
        components in the schematic.
        """
        self.needed_parts.add(lib_id)
        n = n_per_unit
        fy = (n - 1) / 2 * 2.54

        def emit_unit(unit_idx, x, y, mirror, val_pos, pad_offset):
            mirror_line = "\n    (mirror y)" if mirror else ""
            if val_pos is None:
                vx, vy, va = x, y - fy - 3.81, 0
            else:
                vx, vy, va = val_pos

            pin_lines = []
            pin_pos = {}
            for k in range(1, n + 1):
                py = y - fy + (k - 1) * 2.54
                px = (x - 5.08) if mirror else (x + 5.08)
                pin_pos[k] = (px, py)
                kicad_pad = k + pad_offset
                pin_lines.append(f'    (pin "{kicad_pad}" (uuid "{uid()}"))')

            iid = uid()
            self.components.append(
                f"  (symbol\n"
                f'    (lib_id "{lib_id}")\n'
                f"    (at {x:.2f} {y:.2f} 0){mirror_line}\n"
                f"    (unit {unit_idx})\n"
                f"    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)\n"
                f'    (uuid "{iid}")\n'
                f'    (property "Reference" "{ref}" (at {x:.2f} {y:.2f} 0)\n'
                f"      (effects (font (size 1.27 1.27))))\n"
                f'    (property "Value" "{value}" (at {vx:.2f} {vy:.2f} {va})\n'
                f"      (effects (font (size 1.27 1.27))))\n"
                f'    (property "Footprint" "{fp}" (at 0 0 0)\n'
                f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
                f'    (property "Datasheet" "~" (at 0 0 0)\n'
                f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
                + "\n".join(pin_lines) + "\n"
                f"    (instances\n"
                f'      (project "{PROJECT}"\n'
                f'        (path "/{self.sheet_uuid}" (reference "{ref}") '
                f"(unit {unit_idx}))\n"
                f"      )\n"
                f"    )\n"
                f"  )"
            )
            return pin_pos

        u1 = emit_unit(1, x1, y1, mirror1, val_pos1, pad_offset=0)
        u2 = emit_unit(2, x2, y2, mirror2, val_pos2, pad_offset=n)
        return u1, u2

    # -- Net label at a position --
    def label(self, name, x, y, angle=0):
        justify = "right" if angle == 180 else "left"
        self.labels.append(
            f'  (label "{name}" (at {x:.2f} {y:.2f} {angle})\n'
            f"    (effects (font (size 1.27 1.27)) (justify {justify}))\n"
            f'    (uuid "{uid()}"))'
        )

    # -- Global label (for power nets) --
    def glabel(self, name, x, y, angle=0, shape="passive"):
        justify = "right" if angle == 180 else "left"
        self.labels.append(
            f'  (global_label "{name}" (shape {shape}) (at {x:.2f} {y:.2f} {angle})\n'
            f"    (effects (font (size 1.27 1.27)) (justify {justify}))\n"
            f'    (uuid "{uid()}"))'
        )

    # -- Wire segment --
    def wire(self, x1, y1, x2, y2):
        self.wires.append(
            f"  (wire (pts (xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f}))\n"
            f"    (stroke (width 0) (type default))\n"
            f'    (uuid "{uid()}"))'
        )

    # -- No-connect flag --
    def nc(self, x, y):
        self.ncs.append(
            f'  (no_connect (at {x:.2f} {y:.2f}) (uuid "{uid()}"))'
        )

    # -- Text annotation --
    def text(self, txt, x, y, size=2.0, angle=0):
        self.texts.append(
            f'  (text "{txt}" (at {x:.2f} {y:.2f} {angle})\n'
            f"    (effects (font (size {size:.1f} {size:.1f})))\n"
            f'    (uuid "{uid()}"))'
        )

    # -- Convenience: wire + net label from a pin --
    def pin_label(self, pin_xy, name, mirror=False, wire_ext=7.62,
                  label_offset=None):
        """Short wire from pin + label at the end (or at label_offset)."""
        if label_offset is None:
            label_offset = wire_ext
        px, py = pin_xy
        if mirror:
            wire_end = px - wire_ext
            label_x = px - label_offset
            angle = 180
        else:
            wire_end = px + wire_ext
            label_x = px + label_offset
            angle = 0
        self.wire(px, py, wire_end, py)
        self.label(name, label_x, py, angle)

    def pin_glabel(self, pin_xy, name, mirror=False, shape="passive"):
        """Short wire from pin + global label."""
        px, py = pin_xy
        ext = 7.62
        if mirror:
            ex = px - ext
            angle = 180
        else:
            ex = px + ext
            angle = 0
        self.wire(px, py, ex, py)
        self.glabel(name, ex, py, angle, shape)

    # -- Render to string --
    def render(self):
        defs_list = [conn_sym_def(n) for n in sorted(self.needed_sizes)]
        # Custom parts are emitted with the library prefix in the symbol
        # name so they match the lib_id used at the call site.
        if "ti99-parts:BOB-12009" in self.needed_parts:
            defs_list.append(bob_12009_symbol(lib_prefix="ti99-parts:"))
        if "ti99-parts:ESP32-S3-N16R8" in self.needed_parts:
            defs_list.append(esp32_symbol(lib_prefix="ti99-parts:"))
        defs = "\n".join(defs_list)
        return (
            f"(kicad_sch\n"
            f"  (version 20260306)\n"
            f'  (generator "ti99_kb_gen")\n'
            f'  (generator_version "10.0")\n'
            f'  (uuid "{self.sheet_uuid}")\n'
            f'  (paper "A3")\n'
            f"  (lib_symbols\n{defs}\n  )\n"
            + "\n".join(self.texts) + "\n"
            + "\n".join(self.components) + "\n"
            + "\n".join(self.labels) + "\n"
            + "\n".join(self.wires) + "\n"
            + "\n".join(self.ncs) + "\n"
            f")\n"
        )


# ---------------------------------------------------------------------------
# Project file (.kicad_pro)
# ---------------------------------------------------------------------------
def make_project():
    return json.dumps({
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 3},
        "board": {
            "design_settings": {
                "defaults": {"board_outline_line_width": 0.05},
                # Pre-defined sizes for the PCB editor's toolbar dropdowns.
                # Regenerating this file overwrites KiCad-saved project
                # settings, so the board's standard sizes are baked in here
                # (0.5mm signal/power traces -- see CLAUDE.md).
                "track_widths": [0.5],
                "via_dimensions": [{"diameter": 0.8, "drill": 0.4}]
            }
        },
        "schematic": {"meta": {"version": 1}},
        "libraries": {
            "pinned_footprint_libs": [],
            "pinned_symbol_libs": []
        },
        "net_settings": {
            "meta": {"version": 5},
            "classes": [
                {
                    "name": "Default",
                    "clearance": 0.2,
                    "track_width": 0.5,
                    "via_diameter": 0.8,
                    "via_drill": 0.4,
                    "wire_width": 6,
                    "bus_width": 12
                }
            ]
        },
        "sheets": [[uid(), "Root"]],
        "text_variables": {}
    }, indent=2)


# ---------------------------------------------------------------------------
# Build the schematic
# ---------------------------------------------------------------------------
def build_schematic():
    s = Schematic()

    # Footprint library references
    FP_H15 = "Connector_PinHeader_2.54mm:PinHeader_1x15_P2.54mm_Vertical"
    FP_H02 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
    FP_M04 = ("Connector_Molex:Molex_KK-396_5273-04A_"
             "1x04_P3.96mm_Vertical")
    # Project-specific footprints (see pcb/lib/ti99-parts.pretty/, generated
    # by generate_parts.py). The BOB and ESP32 footprints have proper row
    # spacing baked in; J13 has 1.1mm drill for 18-22 AWG hookup wire.
    FP_BOB = "ti99-parts:BOB-12009"
    FP_ESP = "ti99-parts:ESP32-S3-N16R8"
    FP_J13 = "ti99-parts:J13-CableHeader-1x04"

    # ---- Place components ----
    # Signal flow: ESP32 (right) -> BOB LV-side -> BOB HV-side -> TI (left)

    # Bench-test 5V power input (kept for development without TI PSU)
    j_pwr = s.add_conn("J9", 2, 35.56, 34.29, FP_H02, "PWR_5V_IN")

    # TI PSU daisy-chain.
    # TI 4-pin power pinout (per user-provided datasheet):
    #   pin 1 = -5V, pin 2 = +12V, pin 3 = GND, pin 4 = +5V
    #
    # J13: 4-pin solder-pad header. A pre-built cable is permanently
    #      soldered into these holes; the cable's far end has a female
    #      plug that mates with the TI PSU's male pin header. Plain
    #      0.1" header footprint is used as solder pads for hookup wire.
    # J14: 4-pin Molex KK-396 male header. The TI mainboard's existing
    #      female plug mates here. Adapter sits inline between PSU and
    #      MB; pins 1 and 2 pass straight through, pins 3 (GND) and 4
    #      (+5V) tap into the board's internal power nets.
    # Placed at the bottom of the schematic in the empty area below all
    # other components. Schematic position has no effect on PCB layout
    # -- you'll position the actual footprints in the PCB editor.
    j_psu_in  = s.add_conn("J13", 4, 50.80, 130.00, FP_J13, "PWR_TI_IN_CABLE")
    j_psu_out = s.add_conn("J14", 4, 86.36, 130.00, FP_M04, "PWR_TI_OUT_MB")

    # TI keyboard connector (left, pins face right)
    j_ti = s.add_conn("J10", 15, 35.56, 88.90, FP_H15, "TI_KBD")
    # J20: parallel 1x15 TI keyboard connector wired pin-1-to-pin-1 with
    # J10 so an original TI keyboard can plug in alongside the modern
    # adapter. Each pin shares a net with the corresponding J10 pin via
    # the same signal label.
    j_ti2 = s.add_conn("J20", 15, 5.08, 88.90, FP_H15, "TI_KBD_PARALLEL")

    # v4 straight-cable fix: the flat ribbon lands J10 pin p on TI-motherboard
    # pin 16-p, so rev 1-3 needed the ribbon twisted 180 degrees. In v4 the
    # firmware compensates for the mirror on every line (see the GPIO remap in
    # ti-99-keyboard.ino), and every J10 pin -- including alpha lock -- gets a
    # level-shifter channel and GPIO, so future re-pinning is firmware-only.
    # J10 pin 6 carries INT9 and pin 10 carries ALPHA_LOCK: through the flat
    # ribbon those land on TI-motherboard pins 10 (INT9) and 6 (alpha lock),
    # which is what puts the passive alpha-lock line where the TI expects it.

    # Signal name annotations (to the left of J_TI): the net at each J10 pin.
    ti_signals = [
        (1,  "INT5"),
        (2,  "INT6"),
        (3,  "INT8"),
        (4,  "INT4"),
        (5,  "INT3"),
        (6,  "INT9"),
        (7,  "INT7"),
        (8,  "1Y1"),
        (9,  "1Y0"),
        (10, "P5 (Alpha Lock)"),
        (11, "INT10"),
        (12, "2Y0"),
        (13, "2Y1"),
        (14, "2Y2"),
        (15, "2Y3"),
    ]
    for pin, sig in ti_signals:
        _, py = j_ti[pin]
        s.text(sig, 24.0, py, 1.27)

    # Four BOB-12009 modules, stacked vertically between TI (left) and
    # ESP32 (right). Each BOB is a single multi-unit component:
    #   unit 1 = LV row (pads  1- 6), placed on the right facing ESP32
    #   unit 2 = HV row (pads  7-12), placed on the left  facing TI
    # Both units share one Reference (J1..J4) and one footprint with
    # 10mm row spacing baked in -- "Update PCB from Schematic" gives one
    # footprint per BOB at the correct spacing, no manual realignment.
    BOB_X_HV = 87.63
    BOB_X_LV = BOB_X_HV + 10.00
    BOB_Y_START = 40.64
    BOB_Y_STEP = 20.32

    def place_bob(idx, ref):
        y = BOB_Y_START + idx * BOB_Y_STEP
        lv, hv = s.add_part2(
            ref=ref,
            lib_id="ti99-parts:BOB-12009",
            value="BOB-12009",
            n_per_unit=6,
            fp=FP_BOB,
            x1=BOB_X_LV, y1=y, mirror1=False,           # LV unit on right
            val_pos1=(BOB_X_LV - 3.302, y, 90),
            x2=BOB_X_HV, y2=y, mirror2=True,            # HV unit on left
            val_pos2=(BOB_X_HV + 3.556, y, 90),
        )
        return hv, lv

    j1_hv, j1_lv = place_bob(0, "J1")
    j2_hv, j2_lv = place_bob(1, "J2")
    j3_hv, j3_lv = place_bob(2, "J3")
    j4_hv, j4_lv = place_bob(3, "J4")

    # ESP32-S3-N16R8 dev board as a single multi-unit component.
    # unit 1 = left header (pads  1-22), placed mirrored facing BOBs
    # unit 2 = right header (pads 23-44), all NC (mechanical only)
    # Footprint has 25.4mm row spacing baked in.
    j_esp_l, j_esp_r = s.add_part2(
        ref="U1",
        lib_id="ti99-parts:ESP32-S3-N16R8",
        value="ESP32-S3-N16R8",
        n_per_unit=22,
        fp=FP_ESP,
        x1=151.13, y1=88.90, mirror1=True,    # left header faces BOBs
        x2=176.53, y2=88.90, mirror2=False,   # right header faces away
    )

    # ---- Section labels ----
    s.text("TI-99/4A Keyboard Adapter - Carrier Board (BSS138 rev)",
           70.866, 19.812, 3.0)
    s.text("POWER", 43.688, 27.178, 2.0)
    s.text("TI PSU DAISY-CHAIN  (J13 = cable to PSU,  "
           "J14 = MB plug)", 50.80, 122.0, 1.5)
    s.text("BOB #1 (TI 1-4)",  93.0, 32.0, 1.3)
    s.text("BOB #2 (TI 5-9)",  93.0, 52.3, 1.3)
    s.text("BOB #3 (TI 10-13)", 93.0, 72.6, 1.3)
    s.text("BOB #4 (TI 14-15)", 93.0, 92.9, 1.3)
    s.text("ESP32-S3 DevKitC-1 (Left Header)", 155.448, 87.376, 1.5,
           angle=90)
    s.text("TI-99/4A Keyboard", 31.24, 88.316, 1.5, angle=90)
    s.text("ESP32-S3 (Right Header - mechanical only)", 171.704, 90.678,
           1.5, angle=90)

    # ==================================================================
    # POWER NETS
    # ==================================================================

    # +3V3 sources: ESP32 pins 1, 2
    s.pin_glabel(j_esp_l[1], "+3V3", mirror=True)
    s.pin_glabel(j_esp_l[2], "+3V3", mirror=True)
    # +3V3 sinks: LV rail (socket pin 4) on each BOB LV side
    for lv in (j1_lv, j2_lv, j3_lv, j4_lv):
        s.pin_glabel(lv[4], "+3V3")

    # +5V sources: bench-test 2-pin header (J9) AND TI PSU daisy-chain
    # (J13/J14 pin 4). Both feed the same +5V net.
    s.pin_glabel(j_pwr[1], "+5V")
    s.pin_glabel(j_psu_in[4], "+5V")
    s.pin_glabel(j_psu_out[4], "+5V")
    # +5V sinks: ESP32 5V0 (pin 21), HV rail (socket pin 4) on each BOB HV side
    s.pin_glabel(j_esp_l[21], "+5V", mirror=True)
    for hv in (j1_hv, j2_hv, j3_hv, j4_hv):
        s.pin_glabel(hv[4], "+5V", mirror=True)

    # GND sources: bench-test 2-pin header (J9) AND TI PSU daisy-chain
    # (J13/J14 pin 3). Both tie to the same GND net.
    s.pin_glabel(j_pwr[2], "GND")
    s.pin_glabel(j_psu_in[3], "GND")
    s.pin_glabel(j_psu_out[3], "GND")
    # GND sinks: ESP32 GND; BOB GND is at socket pin 3 on both sides.
    # (Common ground between LV and HV is essential for the BSS138 to
    # work -- all grounds tie to the same net.)
    s.pin_glabel(j_esp_l[22], "GND", mirror=True)
    for lv in (j1_lv, j2_lv, j3_lv, j4_lv):
        s.pin_glabel(lv[3], "GND")
    for hv in (j1_hv, j2_hv, j3_hv, j4_hv):
        s.pin_glabel(hv[3], "GND", mirror=True)

    # TI PSU pass-through nets for -5V and +12V (not used by this board,
    # but routed straight from J13 to J14 so the TI mainboard still gets
    # all four rails when the adapter sits inline between PSU and MB).
    s.pin_label(j_psu_in[1],  "PSU_-5V")
    s.pin_label(j_psu_out[1], "PSU_-5V")
    s.pin_label(j_psu_in[2],  "PSU_+12V")
    s.pin_label(j_psu_out[2], "PSU_+12V")

    # ==================================================================
    # CHANNEL CONNECTIONS
    # ==================================================================
    # Each channel runs: ESP32 GPIO -> BOB LV pin -> [BSS138 MOSFET] ->
    # BOB HV pin -> TI keyboard pin.
    #
    # IMPORTANT: LV and HV sides are electrically isolated through the
    # MOSFET, so they MUST have different net names. We use:
    #   LV-domain net:  "<signal>_LV"  (ESP32 pin  + BOB LV-side pin)
    #   HV-domain net:  "<signal>"     (BOB HV-side pin + TI pin)
    #
    # Channel-to-socket-pin mapping (same on both LV and HV sides):
    #   CH1=pin 6, CH2=pin 5, CH3=pin 2, CH4=pin 1
    #
    # LBGE ESP32-S3 header pin -> GPIO map (left header):
    #   pin 4=GPIO4, 5=GPIO5, 6=GPIO6, 7=GPIO7,
    #   pin 8=GPIO15, 9=GPIO16, 10=GPIO17, 11=GPIO18,
    #   pin 12=GPIO8, pin 15=GPIO9, 16=GPIO10, 17=GPIO11, 18=GPIO12,
    #   pin 19=GPIO13, 20=GPIO14
    #
    # Format: (esp_pin, socket_pin, j10_pin, signal)

    # v4 channel allocation (straight-through ribbon rework):
    # ALL 15 J10 pins now get a level-shifter channel and a GPIO --
    # including alpha lock (J10 pin 10), which rev 1-3 left as a bare
    # J10<->J20 pass-through. GPIO8 (header pin 12) enters at BOB#3-CH4
    # and every channel from J10 pin 6 onward shifts over one relative
    # to rev 3. 15 of 16 channels used; BOB#4-CH1 spare.
    #
    # Each BOB gets a contiguous block of GPIOs AND a contiguous block
    # of J10 pins, with BOBs ordered top-to-bottom, so every wire runs
    # straight (no inter-BOB crossings):
    #
    #   BOB#1 (top):    GPIO 4-7   (J11 pins 4-7)     <-> J10 1-4
    #   BOB#2:          GPIO 15-18 (J11 pins 8-11)    <-> J10 5-8
    #   BOB#3:          GPIO 8-11  (J11 pins 12,15-17)<-> J10 9-12
    #   BOB#4 (bottom): GPIO 12-14 (J11 pins 18-20)   <-> J10 13-15
    #
    # Net names are J10-local (the signal name at that J10 position).
    # The straight ribbon lands J10 pin p on TI-motherboard pin 16-p;
    # firmware compensates by driving each TI function on the GPIO of
    # the mirrored J10 pin (see the v4 remap in ti-99-keyboard.ino).
    # J10 pins 6 (INT9) and 10 (ALPHA_LOCK) hold TI pin 10's and TI
    # pin 6's signals so the passive alpha-lock line lands correctly.
    #
    # Channel-to-socket-pin mapping (same on LV and HV sides):
    #   CH1=pin 6, CH2=pin 5, CH3=pin 2, CH4=pin 1

    # Each J10 pin in a BOB's block wires to the BOB's HV-side pin at
    # the SAME vertical position, top-to-bottom -- block's top pin ->
    # HV4 (top of BOB), then HV3, HV2, HV1 (bottom). This avoids
    # crossing wires in the schematic. Mapping uses socket pins
    # 1, 2, 5, 6 in that order (= CH4, CH3, CH2, CH1).
    #
    # BOB#1: J10 1-4
    bob1_nets = [
        (4, 1, 1, "INT5"),   # GPIO4 -> CH4 (HV4) -> J10 1
        (5, 2, 2, "INT6"),   # GPIO5 -> CH3 (HV3) -> J10 2
        (6, 5, 3, "INT8"),   # GPIO6 -> CH2 (HV2) -> J10 3
        (7, 6, 4, "INT4"),   # GPIO7 -> CH1 (HV1) -> J10 4
    ]
    # BOB#2: J10 5-8
    bob2_nets = [
        (8,  1, 5, "INT3"),  # GPIO15 -> CH4 (HV4) -> J10 5
        (9,  2, 6, "INT9"),  # GPIO16 -> CH3 (HV3) -> J10 6
        (10, 5, 7, "INT7"),  # GPIO17 -> CH2 (HV2) -> J10 7
        (11, 6, 8, "1Y1"),   # GPIO18 -> CH1 (HV1) -> J10 8
    ]
    # BOB#3: J10 9-12 (alpha lock now a real channel on CH3)
    bob3_nets = [
        (12, 1,  9, "1Y0"),        # GPIO8  -> CH4 (HV4) -> J10 9
        (15, 2, 10, "ALPHA_LOCK"), # GPIO9  -> CH3 (HV3) -> J10 10
        (16, 5, 11, "INT10"),      # GPIO10 -> CH2 (HV2) -> J10 11
        (17, 6, 12, "2Y0"),        # GPIO11 -> CH1 (HV1) -> J10 12
    ]
    # BOB#4: J10 13-15 on CH4/CH3/CH2; CH1 unused
    bob4_nets = [
        (18, 1, 13, "2Y1"),   # GPIO12 -> CH4 (HV4) -> J10 13
        (19, 2, 14, "2Y2"),   # GPIO13 -> CH3 (HV3) -> J10 14
        (20, 5, 15, "2Y3"),   # GPIO14 -> CH2 (HV2) -> J10 15
    ]

    for bob_lv, bob_hv, nets in (
        (j1_lv, j1_hv, bob1_nets),
        (j2_lv, j2_hv, bob2_nets),
        (j3_lv, j3_hv, bob3_nets),
        (j4_lv, j4_hv, bob4_nets),
    ):
        for esp_pin, socket_pin, j10_pin, sig in nets:
            net_lv = f"{sig}_LV"   # ESP32 -> BOB LV (3V3 domain)
            net_hv = sig           # BOB HV -> TI    (5V domain)
            s.pin_label(j_esp_l[esp_pin], net_lv, mirror=True)
            s.pin_label(bob_lv[socket_pin], net_lv)
            s.pin_label(bob_hv[socket_pin], net_hv, mirror=True)
            s.pin_label(j_ti[j10_pin], net_hv,
                        wire_ext=10.16, label_offset=5.08)
            # Parallel TI keyboard connector — same net via shared label
            s.pin_label(j_ti2[j10_pin], net_hv,
                        wire_ext=10.16, label_offset=5.08)

    # ==================================================================
    # NO-CONNECTS
    # ==================================================================

    # ESP32 left header: RST (pin 3), GPIO46 (pin 14)
    # GPIO3 (pin 13) usable on LBGE, left open. GPIO8 (pin 12) is used
    # by the v4 fabric (BOB#3-CH4).
    for p in [3, 14]:
        s.nc(*j_esp_l[p])

    # BOB#4 unused channel: CH1 (socket pin 6) on both LV and HV sides
    for p in [6]:
        s.nc(*j4_lv[p])
        s.nc(*j4_hv[p])

    # Alpha lock (J10/J20 pin 10) is now a full BOB channel from GPIO9
    # (see bob3_nets) -- no standalone passive tie anymore. Firmware
    # parks the GPIO as INPUT; a parallel TI keyboard on J20 still
    # passes its alpha-lock switch through the shared ALPHA_LOCK net.

    # ESP32 right header: GND on pins 1, 21, 22; rest NC (mechanical only)
    s.pin_glabel(j_esp_r[1], "GND")
    s.pin_glabel(j_esp_r[21], "GND")
    s.pin_glabel(j_esp_r[22], "GND")
    for p in range(2, 21):
        s.nc(*j_esp_r[p])

    return s.render()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))

    sch_path = os.path.join(out_dir, f"{PROJECT}.kicad_sch")
    pro_path = os.path.join(out_dir, f"{PROJECT}.kicad_pro")

    with open(sch_path, "w", newline="\n") as f:
        f.write(build_schematic())
    print(f"  wrote {sch_path}")

    with open(pro_path, "w", newline="\n") as f:
        f.write(make_project())
    print(f"  wrote {pro_path}")

    print()
    print("Next steps:")
    print(f"  1. Open {PROJECT}.kicad_pro in KiCad 10")
    print("  2. Review the schematic. BOB socket pin mapping:")
    print("     LV side: 1=LV4, 2=LV3, 3=GND, 4=LV(3V3), 5=LV2, 6=LV1")
    print("     HV side: 1=HV4, 2=HV3, 3=GND, 4=HV(5V),  5=HV2, 6=HV1")
    print("     LV and HV nets are intentionally separate ('<sig>_LV' vs '<sig>').")
    print("  3. Press F8 to 'Update PCB from Schematic'")
    print("  4. Place footprints: 4x PinSocket_1x06 pairs at 10mm row spacing")
    print("     for BOBs; ESP32 sockets at 25.4mm row spacing. Previous TXS")
    print("     layout will need a full redo.")
    print("  5. File > Fabrication Outputs > Gerbers to export for fab")


if __name__ == "__main__":
    main()
