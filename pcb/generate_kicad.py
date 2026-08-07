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
# Mounting-hole symbol (pinless mechanical part)
# ---------------------------------------------------------------------------
def mounting_hole_sym_def():
    return (
        '    (symbol "Mechanical:MountingHole"\n'
        "      (pin_names (offset 1.016))\n"
        "      (exclude_from_sim yes) (in_bom no) (on_board yes)\n"
        '      (property "Reference" "H" (at 0 5.08 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Value" "MountingHole" (at 0 3.05 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Footprint" "" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (property "Datasheet" "~" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (symbol "MountingHole_0_1"\n'
        "        (circle (center 0 0) (radius 1.27)\n"
        "          (stroke (width 0.508) (type default))\n"
        "          (fill (type none)))\n"
        "      )\n"
        "    )"
    )


# ---------------------------------------------------------------------------
# Discrete level-shifter parts (V7): BSS138 SOT-23 + 10k 0805
# ---------------------------------------------------------------------------
def bss138_sym_def():
    """N-MOSFET, SOT-23. Pins: 3=D (left upper), 1=G (left lower),
    2=S (right). Drawn as a simple box like the rest of the generated
    parts; D/G on the left (HV/J10 side), S on the right (LV/ESP side)."""
    pins = [
        ("3", "D", -5.08, -2.54, 0),
        ("1", "G", -5.08, 2.54, 0),
        ("2", "S", 5.08, 0.00, 180),
    ]
    pin_txt = "\n".join(
        f"        (pin passive line (at {px:.2f} {py:.2f} {ang}) (length 2.54)\n"
        f'          (name "{nm}" (effects (font (size 1.27 1.27))))\n'
        f'          (number "{num}" (effects (font (size 1.27 1.27)))))'
        for num, nm, px, py, ang in pins)
    return (
        '    (symbol "ti99-parts:BSS138"\n'
        "      (pin_names (offset 0.508))\n"
        "      (exclude_from_sim no) (in_bom yes) (on_board yes)\n"
        '      (property "Reference" "Q" (at 0 -5.08 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Value" "BSS138" (at 0 5.08 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Footprint" "" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (property "Datasheet" "~" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (symbol "BSS138_1_1"\n'
        "        (rectangle (start -2.54 -3.81) (end 2.54 3.81)\n"
        "          (stroke (width 0.254) (type default))\n"
        "          (fill (type background)))\n"
        + pin_txt + "\n"
        "      )\n"
        "    )"
    )


def r10k_sym_def():
    """10k pull-up, 0805, horizontal box. Pins: 1=left, 2=right."""
    pin_txt = "\n".join(
        f"        (pin passive line (at {px:.2f} 0.00 {ang}) (length 2.54)\n"
        f'          (name "{nm}" (effects (font (size 1.27 1.27))))\n'
        f'          (number "{num}" (effects (font (size 1.27 1.27)))))'
        for num, nm, px, ang in
        [("1", "1", -5.08, 0), ("2", "2", 5.08, 180)])
    return (
        '    (symbol "ti99-parts:R10k"\n'
        "      (pin_names (offset 0.508) hide)\n"
        "      (exclude_from_sim no) (in_bom yes) (on_board yes)\n"
        '      (property "Reference" "R" (at 0 -3.81 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Value" "10k" (at 0 3.81 0)\n'
        "        (effects (font (size 1.27 1.27))))\n"
        '      (property "Footprint" "" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (property "Datasheet" "~" (at 0 0 0)\n'
        "        (effects (font (size 1.27 1.27)) (hide yes)))\n"
        '      (symbol "R10k_1_1"\n'
        "        (rectangle (start -2.54 -1.27) (end 2.54 1.27)\n"
        "          (stroke (width 0.254) (type default))\n"
        "          (fill (type background)))\n"
        + pin_txt + "\n"
        "      )\n"
        "    )"
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

    # -- Add a mounting hole (pinless mechanical footprint, no nets) --
    def add_mounting_hole(self, ref, x, y):
        self.needed_parts.add("Mechanical:MountingHole")
        self.components.append(
            f"  (symbol\n"
            f'    (lib_id "Mechanical:MountingHole")\n'
            f"    (at {x:.2f} {y:.2f} 0)\n"
            f"    (unit 1)\n"
            f"    (exclude_from_sim yes) (in_bom no) (on_board yes) (dnp no)\n"
            f'    (uuid "{uid()}")\n'
            f'    (property "Reference" "{ref}" (at {x:.2f} {y - 3.81:.2f} 0)\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Value" "MountingHole_2.2mm_M2" (at {x:.2f} {y + 3.81:.2f} 0)\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Footprint" "MountingHole:MountingHole_2.2mm_M2" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            f'    (property "Datasheet" "~" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            f"    (instances\n"
            f'      (project "{PROJECT}"\n'
            f'        (path "/{self.sheet_uuid}" (reference "{ref}") (unit 1))\n'
            f"      )\n"
            f"    )\n"
            f"  )"
        )

    # -- Add a simple single-unit part (FET, resistor). Returns pin coords --
    def add_simple(self, ref, lib_id, value, fp, x, y, pin_offsets,
                   lcsc=None):
        """Place a single-unit symbol. pin_offsets: {num: (dx, dy)} are the
        pin CONNECTION points relative to (x, y). Returns {num: (ax, ay)}.
        lcsc: optional LCSC part number, stored as a hidden "LCSC" property
        for the JLCPCB assembly BOM export."""
        self.needed_parts.add(lib_id)
        pin_lines = "\n".join(
            f'    (pin "{k}" (uuid "{uid()}"))' for k in pin_offsets)
        lcsc_prop = ""
        if lcsc:
            lcsc_prop = (
                f'    (property "LCSC" "{lcsc}" (at 0 0 0)\n'
                f"      (effects (font (size 1.27 1.27)) (hide yes)))\n")
        self.components.append(
            f"  (symbol\n"
            f'    (lib_id "{lib_id}")\n'
            f"    (at {x:.2f} {y:.2f} 0)\n"
            f"    (unit 1)\n"
            f"    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)\n"
            f'    (uuid "{uid()}")\n'
            f'    (property "Reference" "{ref}" (at {x:.2f} {y - 5.08:.2f} 0)\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Value" "{value}" (at {x:.2f} {y + 5.08:.2f} 0)\n'
            f"      (effects (font (size 1.27 1.27))))\n"
            f'    (property "Footprint" "{fp}" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            f'    (property "Datasheet" "~" (at 0 0 0)\n'
            f"      (effects (font (size 1.27 1.27)) (hide yes)))\n"
            + lcsc_prop
            + pin_lines + "\n"
            f"    (instances\n"
            f'      (project "{PROJECT}"\n'
            f'        (path "/{self.sheet_uuid}" (reference "{ref}") (unit 1))\n'
            f"      )\n"
            f"    )\n"
            f"  )"
        )
        return {k: (x + dx, y + dy) for k, (dx, dy) in pin_offsets.items()}

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
        if "Mechanical:MountingHole" in self.needed_parts:
            defs_list.append(mounting_hole_sym_def())
        if "ti99-parts:BSS138" in self.needed_parts:
            defs_list.append(bss138_sym_def())
        if "ti99-parts:R10k" in self.needed_parts:
            defs_list.append(r10k_sym_def())
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
                "via_dimensions": [{"diameter": 0.8, "drill": 0.4}],
                # The generated footprint pin labels are 0.6/0.7mm silk
                # text; KiCad's default 0.8mm min-text-height constraint
                # flags all ~100 of them. They print fine at JLCPCB
                # (verified on the fabbed rev-2/3 boards), so relax the
                # constraint to match reality.
                "rules": {"min_text_height": 0.6}
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
    FP_H01 = "Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical"
    FP_M04 = ("Connector_Molex:Molex_KK-396_5273-04A_"
             "1x04_P3.96mm_Vertical")
    # Project-specific footprints (see pcb/lib/ti99-parts.pretty/, generated
    # by generate_parts.py). The ESP32 footprint has proper row spacing
    # baked in; J13 has 1.1mm drill for 18-22 AWG hookup wire. (The BOB
    # footprint remains in the library for rev-3/5/6 boards but is no
    # longer used -- V7 uses discrete channel cells.)
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
    #      MB. NOTE: J14 is MIRROR-wired relative to J13 (1<->4, 2<->3)
    #      so the keyed MB plug mates in its natural orientation -- see
    #      the passthrough-mirror explanation in the POWER NETS section.
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
    # J15: single solder pad for the SPARE repair channel. If any matrix
    # GPIO or BSS138 channel dies, jumper this pad to the dead line's
    # pad and remap that line's firmware define to PIN_SPARE.
    j_spare = s.add_conn("J15", 1, 35.56, 124.46, FP_H01, "SPARE_PAD")

    # Mounting holes (2.2mm / M2, self-tapping into the printed mount).
    # In the schematic so F8 owns them -- they can never be silently
    # dropped by "delete footprints with no symbols" again. Migrating an
    # existing board: rename its board-only hole footprints (UL/UR/LL/LR)
    # to H1-H4, untick "Not in schematic" on each, and run F8 with
    # "re-link footprints to symbols by reference" checked -- the holes
    # are adopted in place, positions preserved.
    for i, (hx, hy) in enumerate(
            [(20.32, 142.24), (30.48, 142.24),
             (40.64, 142.24), (50.80, 142.24)], start=1):
        s.add_mounting_hole(f"H{i}", hx, hy)

    # v5 straight-cable fix: the flat ribbon lands J10 pin p on TI-motherboard
    # pin 16-p, so rev 1-3 needed the ribbon twisted 180 degrees. In v5 the
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

    # V7: the four BOB-12009 daughterboards are replaced by 16 discrete
    # on-board channel cells (BSS138 + 2x 10k each), generated below in
    # the CHANNEL CELLS section after the ESP32 is placed.

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
    s.text("LEVEL SHIFTERS - 16x BSS138 + 10k pull-up pairs "
           "(Q1-Q16 / R1-R32, JLC-assembled SMT)", 93.0, 24.5, 1.3)
    s.text("ESP32-S3 DevKitC-1 (Left Header)", 155.448, 87.376, 1.5,
           angle=90)
    s.text("TI-99/4A Keyboard", 31.24, 88.316, 1.5, angle=90)
    s.text("ESP32-S3 (Right Header - mechanical only)", 171.704, 90.678,
           1.5, angle=90)

    # ==================================================================
    # POWER NETS
    # ==================================================================

    # +3V3 sources: ESP32 pins 1, 2. Sinks: the 16 channel cells' gates
    # and LV pull-ups (labeled in the CHANNEL CELLS section).
    s.pin_glabel(j_esp_l[1], "+3V3", mirror=True)
    s.pin_glabel(j_esp_l[2], "+3V3", mirror=True)

    # +5V sources: bench-test 2-pin header (J9) AND TI PSU daisy-chain
    # (J13 pin 4 / J14 pin 1 -- J14 is MIRROR-wired, see below).
    s.pin_glabel(j_pwr[1], "+5V")
    s.pin_glabel(j_psu_in[4], "+5V")
    s.pin_glabel(j_psu_out[1], "+5V")
    # +5V sinks: ESP32 5V0 (pin 21) and the cells' HV pull-ups.
    s.pin_glabel(j_esp_l[21], "+5V", mirror=True)

    # GND sources: bench-test 2-pin header (J9) AND TI PSU daisy-chain
    # (J13 pin 3 / J14 pin 2 -- J14 is MIRROR-wired, see below).
    s.pin_glabel(j_pwr[2], "GND")
    s.pin_glabel(j_psu_in[3], "GND")
    s.pin_glabel(j_psu_out[2], "GND")
    # GND sinks: ESP32 GND. (The discrete cells have no GND pin -- the
    # common LV/HV ground the BSS138 topology needs is the board's
    # shared GND plane, unchanged.)
    s.pin_glabel(j_esp_l[22], "GND", mirror=True)

    # TI PSU pass-through nets for -5V and +12V (12V feeds the buck; -5V
    # is not used by this board but passes through to the TI mainboard).
    #
    # J14 is MIRROR-wired relative to J13 (pin 1<->4, 2<->3). Passthrough
    # geometry: in the original PSU<->MB joint the two connector halves
    # FACE each other; a passthrough board mounts both of its interfaces
    # facing the SAME direction, so exactly one must be mirror-wired to
    # compensate. J13 is hand-soldered (self-correcting); J14 mates the
    # keyed MB plug, so J14 carries the mirror:
    #   J14: 1=+5V, 2=GND, 3=+12V, 4=-5V   (J13: 1=-5V, 2=+12V, 3=GND, 4=+5V)
    # Earlier revs wired J14 straight -- the MB plug only worked flipped
    # 180 (discovered on rev 3). Fix lands at the V5 spin.
    s.pin_label(j_psu_in[1],  "PSU_-5V")
    s.pin_label(j_psu_out[4], "PSU_-5V")
    s.pin_label(j_psu_in[2],  "PSU_+12V")
    s.pin_label(j_psu_out[3], "PSU_+12V")

    # ==================================================================
    # CHANNEL CELLS (V7: discrete level shifters)
    # ==================================================================
    # Each channel: ESP32 GPIO -> Q source (LV) -> [BSS138] -> Q drain
    # (HV) -> TI keyboard pin, with the gate on +3V3 and a 10k pull-up
    # on each side (LV->3V3, HV->5V). Same topology as the BOB-12009
    # modules they replace; JLCPCB assembles all 48 SMT parts.
    #
    # Refs: channel n = Q{n}, R{2n-1} (LV pull-up), R{2n} (HV pull-up).
    # LCSC parts: Q = C78284 (JSCJ BSS138 SOT-23), R = C17414
    # (UNI-ROYAL 10k 0805 1%, JLC Basic part).
    #
    # IMPORTANT: LV and HV sides are electrically isolated through the
    # MOSFET, so they MUST have different net names:
    #   LV-domain net:  "<signal>_LV"  (ESP32 pin + Q source + R pull-up)
    #   HV-domain net:  "<signal>"     (Q drain + R pull-up + TI pin)
    #
    # LBGE ESP32-S3 header pin -> GPIO map (left header):
    #   pin 4=GPIO4, 5=GPIO5, 6=GPIO6, 7=GPIO7,
    #   pin 8=GPIO15, 9=GPIO16, 10=GPIO17, 11=GPIO18,
    #   pin 12=GPIO8, pin 13=GPIO3, pin 15=GPIO9, 16=GPIO10, 17=GPIO11,
    #   18=GPIO12, pin 19=GPIO13, 20=GPIO14
    #
    # v5 channel allocation (unchanged in V7 -- same nets, same GPIOs):
    # all 15 J10 pins have a channel (alpha lock included, GPIO3 on the
    # never-driven line where its strapping quirk is benign), plus the
    # SPARE repair channel (GPIO14 -> Q16 -> J15 solder pad). Net names
    # are J10-local; the straight ribbon lands J10 pin p on TI-mb pin
    # 16-p and firmware compensates (see remap in ti-99-keyboard.ino).
    #
    # Format: (esp_pin, j10_pin (None = SPARE pad J15), signal)
    CHANNELS = [
        (4,  1,  "INT5"),        # GPIO4  -> J10 1
        (5,  2,  "INT6"),        # GPIO5  -> J10 2
        (6,  3,  "INT8"),        # GPIO6  -> J10 3
        (7,  4,  "INT4"),        # GPIO7  -> J10 4
        (8,  5,  "INT3"),        # GPIO15 -> J10 5
        (9,  6,  "INT9"),        # GPIO16 -> J10 6
        (10, 7,  "INT7"),        # GPIO17 -> J10 7
        (11, 8,  "1Y1"),         # GPIO18 -> J10 8
        (12, 9,  "1Y0"),         # GPIO8  -> J10 9
        (13, 10, "ALPHA_LOCK"),  # GPIO3  -> J10 10
        (15, 11, "INT10"),       # GPIO9  -> J10 11
        (16, 12, "2Y0"),         # GPIO10 -> J10 12
        (17, 13, "2Y1"),         # GPIO11 -> J10 13
        (18, 14, "2Y2"),         # GPIO12 -> J10 14
        (19, 15, "2Y3"),         # GPIO13 -> J10 15
        (20, None, "SPARE"),     # GPIO14 -> J15 SPARE pad
    ]
    FP_Q = "Package_TO_SOT_SMD:SOT-23"
    FP_R = "Resistor_SMD:R_0805_2012Metric"
    Q_PINS = {3: (-5.08, -2.54), 1: (-5.08, 2.54), 2: (5.08, 0.00)}
    R_PINS = {1: (-5.08, 0.00), 2: (5.08, 0.00)}

    # Cell layout: [+5V]-[R_hv]-[Q]-[R_lv]-[+3V3] in a horizontal strip;
    # the pull-ups connect to the FET by DRAWN WIRES (the pin_label stub
    # from the Q pin ends exactly on the R pin), so each net carries one
    # label, not three. Two columns of eight cells.
    #
    # COLLISION SAFETY: every connector's pins sit on the 2.54mm grid
    # (or, for the ESP32's 22-pin units, the half-grid) at specific y
    # values; overlapping collinear wires in KiCad MERGE NETS. Cell rows
    # use y = 31.75 + n*10.16 (half-grid) with x-extents chosen clear of
    # every connector stub's x-range, verified by the netlist checker.
    for n, (esp_pin, j10_pin, sig) in enumerate(CHANNELS, start=1):
        net_lv = f"{sig}_LV"
        net_hv = sig
        # Column 1 is staggered 5.08 down so the two columns never share
        # a wire row, and sits left of the ESP32 header's stub x-range;
        # both columns use half-grid rows so they can't share a row with
        # J9/J10/J20 (grid) or J13/J14 (off-grid) stubs either. The ESP32
        # pins are ALSO half-grid, so column x-extents must stay clear of
        # x >= 138.43 -- enforced by the collinear-overlap check.
        col, row = divmod(n - 1, 8)
        cx = 76.20 + col * 30.48
        cy = 31.75 + col * 5.08 + row * 10.16

        q = s.add_simple(f"Q{n}", "ti99-parts:BSS138", "BSS138", FP_Q,
                         cx, cy, Q_PINS, lcsc="C78284")
        # LV pull-up right of Q at the source's y; HV pull-up left of Q
        # at the drain's y. Pin-to-pin gaps equal the label wire stubs.
        r_lv = s.add_simple(f"R{2 * n - 1}", "ti99-parts:R10k", "10k",
                            FP_R, cx + 15.24, cy, R_PINS, lcsc="C17414")
        r_hv = s.add_simple(f"R{2 * n}", "ti99-parts:R10k", "10k",
                            FP_R, cx - 15.24, cy - 2.54, R_PINS,
                            lcsc="C17414")

        # LV domain: Q source --wire--> R_lv pin 1; one label on the
        # wire; ESP32 GPIO joins by net name.
        s.pin_label(q[2], net_lv, wire_ext=5.08, label_offset=2.54)
        s.pin_label(j_esp_l[esp_pin], net_lv, mirror=True)
        s.pin_glabel(r_lv[2], "+3V3")
        s.pin_glabel(q[1], "+3V3", mirror=True)
        # HV domain: Q drain --wire--> R_hv pin 2; one label on the
        # wire; J10/J20 (or J15 for SPARE) join by net name.
        s.pin_label(q[3], net_hv, mirror=True, wire_ext=5.08,
                    label_offset=2.54)
        s.pin_glabel(r_hv[1], "+5V", mirror=True)
        if j10_pin is not None:
            s.pin_label(j_ti[j10_pin], net_hv,
                        wire_ext=10.16, label_offset=5.08)
            # Parallel TI keyboard connector — same net via shared label
            s.pin_label(j_ti2[j10_pin], net_hv,
                        wire_ext=10.16, label_offset=5.08)

    # ==================================================================
    # NO-CONNECTS
    # ==================================================================

    # ESP32 left header: RST (pin 3), GPIO46 (pin 14). Every GPIO
    # broken out on the left header is now in service.
    for p in [3, 14]:
        s.nc(*j_esp_l[p])

    # SPARE repair channel: GPIO14 (header pin 20) -> Q16 -> J15 solder
    # pad (the cell itself is generated in the CHANNEL CELLS loop; only
    # the pad's net label lives here). Lets any single dead channel be
    # recovered with one jumper wire + a firmware remap.
    s.pin_label(j_spare[1], "SPARE", wire_ext=10.16, label_offset=5.08)

    # Alpha lock (J10/J20 pin 10) is a full channel from GPIO3 (see
    # CHANNELS) -- no standalone passive tie. Firmware parks the GPIO as
    # INPUT; a parallel TI keyboard on J20 still passes its alpha-lock
    # switch through the shared ALPHA_LOCK net.

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
