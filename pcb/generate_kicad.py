#!/usr/bin/env python3
"""
Generate KiCad 10 project files for TI-99/4A Keyboard Adapter carrier board.

This board is a simple carrier that sockets off-the-shelf modules:
  - ESP32-S3 DevKitC-1 LBGE (via 2x 1x22 pin sockets)
  - 2x TXS0108E breakout boards (via 1x10 pin sockets per side)
  - 1x15 pin header for TI-99/4A keyboard ribbon
  - 1x2 pin header for 5V power input

The PCB has no active components -- just headers and copper traces.

TXS0108E HW-221 breakout pin mapping (top to bottom):
  A-side (1x10): OE, A8, A7, A6, A5, A4, A3, A2, A1, VA
  B-side (1x10): GND, B8, B7, B6, B5, B4, B3, B2, B1, VB

Usage:
  python generate_kicad.py
  Open ti99-kb-adapter.kicad_pro in KiCad 10.
  Press F8 (Update PCB from Schematic) to create the board layout.
"""

import uuid
import json
import os

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
        defs = "\n".join(conn_sym_def(n) for n in sorted(self.needed_sizes))
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
                "defaults": {"board_outline_line_width": 0.05}
            }
        },
        "schematic": {"meta": {"version": 1}},
        "libraries": {
            "pinned_footprint_libs": [],
            "pinned_symbol_libs": []
        },
        "net_settings": {
            "meta": {"version": 5},
            "classes": []
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
    FP_S22 = "Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical"
    FP_S10 = "Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical"
    FP_H15 = "Connector_PinHeader_2.54mm:PinHeader_1x15_P2.54mm_Vertical"
    FP_H02 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"

    # ---- Place components ----
    # Positions match user's cleaned-up KiCad layout.
    # Signal flow: ESP32 (right) -> TXS A-side -> TXS B-side -> TI (left)

    # Power input (top-left)
    j8 = s.add_conn("J8", 2, 35.56, 34.29, FP_H02, "PWR_5V_IN")

    # TI keyboard connector (left, pins face right)
    j7 = s.add_conn("J7", 15, 35.56, 72.39, FP_H15, "TI_KBD")

    # TI signal name annotations (to the left of J7)
    ti_signals = [
        (1,  "INT5"),
        (2,  "INT6"),
        (3,  "INT8"),
        (4,  "INT4"),
        (5,  "INT3"),
        (6,  "P5 (Alpha Lock)"),
        (7,  "INT7"),
        (8,  "1Y1"),
        (9,  "1Y0"),
        (10, "INT9"),
        (11, "INT10"),
        (12, "2Y0"),
        (13, "2Y1"),
        (14, "2Y2"),
        (15, "2Y3"),
    ]
    for pin, sig in ti_signals:
        _, py = j7[pin]
        s.text(sig, 24.0, py, 1.27)

    # TXS#1 B-side (mirrored: pins face left toward TI)
    j4 = s.add_conn("J4", 10, 87.63, 58.42, FP_S10, "TXS1_B", mirror=True,
                     val_pos=(91.186, 58.42, 90))
    # TXS#1 A-side (pins face right toward ESP32)
    j3 = s.add_conn("J3", 10, 99.06, 58.42, FP_S10, "TXS1_A",
                     val_pos=(95.758, 58.42, 90))

    # TXS#2 B-side (mirrored: pins face left toward TI)
    j6 = s.add_conn("J6", 10, 87.63, 90.17, FP_S10, "TXS2_B", mirror=True,
                     val_pos=(90.932, 89.916, 90))
    # TXS#2 A-side (pins face right toward ESP32)
    j5 = s.add_conn("J5", 10, 99.06, 90.17, FP_S10, "TXS2_A",
                     val_pos=(95.25, 90.17, 90))

    # ESP32 left header (mirrored: pins face left toward TXS)
    j1 = s.add_conn("J1", 22, 151.13, 76.2, FP_S22, "ESP32_Left",
                     mirror=True)
    # ESP32 right header (pins face right, all NC, mechanical only)
    j2 = s.add_conn("J2", 22, 175.26, 76.2, FP_S22, "ESP32_Right")

    # ---- Section labels ----
    s.text("TI-99/4A Keyboard Adapter - Carrier Board", 70.866, 19.812, 3.0)
    s.text("POWER", 43.688, 27.178, 2.0)
    s.text("TXS0108E #1", 93.726, 42.672, 1.5)
    s.text("TXS0108E #2", 93.218, 75.438, 1.5)
    s.text("ESP32-S3 DevKitC-1 (Left Header)", 155.448, 74.676, 1.5,
           angle=90)
    s.text("TI-99/4A Keyboard", 31.24, 71.816, 1.5, angle=90)
    s.text("ESP32-S3 (Right Header - mechanical only)", 170.434, 77.978, 1.5,
           angle=90)

    # ==================================================================
    # POWER NETS
    # ==================================================================

    # +3V3 sources: ESP32 pins 1, 2
    s.pin_glabel(j1[1], "+3V3", mirror=True)
    s.pin_glabel(j1[2], "+3V3", mirror=True)
    # +3V3 sinks: TXS OE (pin 1) and VA/VCCA (pin 10) on both A-side boards
    s.pin_glabel(j3[1], "+3V3")
    s.pin_glabel(j3[10], "+3V3")
    s.pin_glabel(j5[1], "+3V3")
    s.pin_glabel(j5[10], "+3V3")

    # +5V source: external power input
    s.pin_glabel(j8[1], "+5V")
    # +5V sinks: ESP32 5V0 (pin 21), TXS VB/VCCB (pin 10) on both B-side
    s.pin_glabel(j1[21], "+5V", mirror=True)
    s.pin_glabel(j4[10], "+5V", mirror=True)
    s.pin_glabel(j6[10], "+5V", mirror=True)

    # GND source: external power input
    s.pin_glabel(j8[2], "GND")
    # GND sinks: ESP32 GND, TXS GND (pin 1) on both B-side boards
    s.pin_glabel(j1[22], "GND", mirror=True)
    s.pin_glabel(j4[1], "GND", mirror=True)
    s.pin_glabel(j6[1], "GND", mirror=True)

    # ==================================================================
    # ESP32 -> TXS#1 A-SIDE CONNECTIONS (all 8 channels)
    # ==================================================================
    # GPIO-to-channel mapping:
    #   GPIO4->A8, GPIO5->A7, GPIO6->A6, GPIO7->A5,
    #   GPIO15->A4, GPIO16->A3, GPIO17->A2, GPIO18->A1
    #
    # J1 pins face left (mirrored), J3 pins face right (not mirrored).
    # Labels connect matching nets across the schematic.
    txs1_a = [
        (4,  2, "GPIO4_A8"),     # GPIO4  -> TXS1 A8 (pin 2)
        (5,  3, "GPIO5_A7"),     # GPIO5  -> TXS1 A7 (pin 3)
        (6,  4, "GPIO6_A6"),     # GPIO6  -> TXS1 A6 (pin 4)
        (7,  5, "GPIO7_A5"),     # GPIO7  -> TXS1 A5 (pin 5)
        (8,  6, "GPIO15_A4"),    # GPIO15 -> TXS1 A4 (pin 6)
        (9,  7, "GPIO16_A3"),    # GPIO16 -> TXS1 A3 (pin 7)
        (10, 8, "GPIO17_A2"),    # GPIO17 -> TXS1 A2 (pin 8)
        (11, 9, "GPIO18_A1"),    # GPIO18 -> TXS1 A1 (pin 9)
    ]

    for j1p, j3p, net in txs1_a:
        s.pin_label(j1[j1p], net, mirror=True)
        s.pin_label(j3[j3p], net)

    # ==================================================================
    # TXS#1 B-SIDE -> TI KEYBOARD CONNECTOR
    # ==================================================================
    # B8->TI pin 1, B7->pin 2, ..., B4->pin 5, (pin 6 NC), B3->pin 7,
    # B2->pin 8, B1->pin 9
    #
    # J4 pins face left (mirrored), J7 pins face right (not mirrored).
    # J7 wires extend 10.16mm with labels at 5.08mm offset.
    txs1_b = [
        (2, 1,  "TXS1_B8"),     # TXS1 B8 (pin 2) -> TI pin 1
        (3, 2,  "TXS1_B7"),     # TXS1 B7 (pin 3) -> TI pin 2
        (4, 3,  "TXS1_B6"),     # TXS1 B6 (pin 4) -> TI pin 3
        (5, 4,  "TXS1_B5"),     # TXS1 B5 (pin 5) -> TI pin 4
        (6, 5,  "TXS1_B4"),     # TXS1 B4 (pin 6) -> TI pin 5
        # TI pin 6 = P5 / Alpha Lock -- NOT CONNECTED
        (7, 7,  "TXS1_B3"),     # TXS1 B3 (pin 7) -> TI pin 7
        (8, 8,  "TXS1_B2"),     # TXS1 B2 (pin 8) -> TI pin 8
        (9, 9,  "TXS1_B1"),     # TXS1 B1 (pin 9) -> TI pin 9
    ]

    for j4p, j7p, net in txs1_b:
        s.pin_label(j4[j4p], net, mirror=True)
        s.pin_label(j7[j7p], net, wire_ext=10.16, label_offset=5.08)

    # ==================================================================
    # ESP32 -> TXS#2 A-SIDE CONNECTIONS (6 of 8 channels)
    # ==================================================================
    # GPIO-to-channel mapping:
    #   GPIO9->A8, GPIO10->A7, GPIO11->A6,
    #   GPIO12->A5, GPIO13->A4, GPIO14->A3
    txs2_a = [
        (15, 2, "GPIO9_A8"),     # GPIO9  -> TXS2 A8 (pin 2)
        (16, 3, "GPIO10_A7"),    # GPIO10 -> TXS2 A7 (pin 3)
        (17, 4, "GPIO11_A6"),    # GPIO11 -> TXS2 A6 (pin 4)
        (18, 5, "GPIO12_A5"),    # GPIO12 -> TXS2 A5 (pin 5)
        (19, 6, "GPIO13_A4"),    # GPIO13 -> TXS2 A4 (pin 6)
        (20, 7, "GPIO14_A3"),    # GPIO14 -> TXS2 A3 (pin 7)
    ]

    for j1p, j5p, net in txs2_a:
        s.pin_label(j1[j1p], net, mirror=True)
        s.pin_label(j5[j5p], net)

    # ==================================================================
    # TXS#2 B-SIDE -> TI KEYBOARD CONNECTOR
    # ==================================================================
    # B8->TI pin 10, B7->pin 11, B6->pin 12, B5->pin 13,
    # B4->pin 14, B3->pin 15
    txs2_b = [
        (2, 10, "TXS2_B8"),     # TXS2 B8 (pin 2) -> TI pin 10
        (3, 11, "TXS2_B7"),     # TXS2 B7 (pin 3) -> TI pin 11
        (4, 12, "TXS2_B6"),     # TXS2 B6 (pin 4) -> TI pin 12
        (5, 13, "TXS2_B5"),     # TXS2 B5 (pin 5) -> TI pin 13
        (6, 14, "TXS2_B4"),     # TXS2 B4 (pin 6) -> TI pin 14
        (7, 15, "TXS2_B3"),     # TXS2 B3 (pin 7) -> TI pin 15
    ]

    for j6p, j7p, net in txs2_b:
        s.pin_label(j6[j6p], net, mirror=True)
        s.pin_label(j7[j7p], net, wire_ext=10.16, label_offset=5.08)

    # ==================================================================
    # NO-CONNECTS
    # ==================================================================

    # ESP32 left header: RST (pin 3), GPIO46 (pin 14)
    # GPIO8 (pin 12) and GPIO3 (pin 13) are usable on LBGE, left open
    for p in [3, 14]:
        s.nc(*j1[p])

    # TXS#2 unused channels: A2/B2 (pin 8), A1/B1 (pin 9)
    for p in [8, 9]:
        s.nc(*j5[p])
        s.nc(*j6[p])

    # TI pin 6: Alpha Lock (not connected -- software implementation)
    s.nc(*j7[6])

    # ESP32 right header: GND on pins 1, 21, 22; rest NC (mechanical only)
    s.pin_glabel(j2[1], "GND")
    s.pin_glabel(j2[21], "GND")
    s.pin_glabel(j2[22], "GND")
    for p in range(2, 21):
        s.nc(*j2[p])

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
    print("  2. Review the schematic (check TXS0108E breakout pinout!)")
    print("  3. Press F8 to 'Update PCB from Schematic'")
    print("  4. Place footprints and route traces")
    print("  5. File > Fabrication Outputs > Gerbers to export for manufacturing")


if __name__ == "__main__":
    main()
