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

BOB-12009 (SparkFun BSS138) socket pin mapping, confirmed from photo:
  LV socket pin: 1=LV1, 2=LV2, 3=LV(3V3), 4=GND, 5=LV3, 6=LV4
  HV socket pin: 1=HV1, 2=HV2, 3=HV(5V),  4=GND, 5=HV3, 6=HV4
Channel-to-socket-pin: CH1=pin 1, CH2=pin 2, CH3=pin 5, CH4=pin 6
Rails at pin 3, GNDs at pin 4. GND and rails are directly opposite
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
    FP_S06 = "Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical"
    FP_H15 = "Connector_PinHeader_2.54mm:PinHeader_1x15_P2.54mm_Vertical"
    FP_H02 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"

    # ---- Place components ----
    # Signal flow: ESP32 (right) -> BOB LV-side -> BOB HV-side -> TI (left)

    # Power input (top-left)
    j_pwr = s.add_conn("J9", 2, 35.56, 34.29, FP_H02, "PWR_5V_IN")

    # TI keyboard connector (left, pins face right)
    j_ti = s.add_conn("J10", 15, 35.56, 88.90, FP_H15, "TI_KBD")

    # TI signal name annotations (to the left of J_TI)
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
        _, py = j_ti[pin]
        s.text(sig, 24.0, py, 1.27)

    # Four BOB-12009 modules, stacked vertically between TI (left) and
    # ESP32 (right). Each module: 2x 1x6 sockets, HV-side on left facing
    # TI, LV-side on right facing ESP32. Row-to-row spacing within the
    # physical BOB is 10mm (measured from board).
    BOB_X_HV = 87.63
    BOB_X_LV = BOB_X_HV + 10.00
    BOB_Y_START = 40.64
    BOB_Y_STEP = 20.32

    def place_bob(idx, ref_hv, ref_lv, value_hv, value_lv):
        y = BOB_Y_START + idx * BOB_Y_STEP
        hv = s.add_conn(ref_hv, 6, BOB_X_HV, y, FP_S06, value_hv,
                        mirror=True, val_pos=(BOB_X_HV + 3.556, y, 90))
        lv = s.add_conn(ref_lv, 6, BOB_X_LV, y, FP_S06, value_lv,
                        val_pos=(BOB_X_LV - 3.302, y, 90))
        return hv, lv

    j1_hv, j1_lv = place_bob(0, "J1", "J2", "BOB1_HV", "BOB1_LV")
    j2_hv, j2_lv = place_bob(1, "J3", "J4", "BOB2_HV", "BOB2_LV")
    j3_hv, j3_lv = place_bob(2, "J5", "J6", "BOB3_HV", "BOB3_LV")
    j4_hv, j4_lv = place_bob(3, "J7", "J8", "BOB4_HV", "BOB4_LV")

    # ESP32 left header (mirrored: pins face left toward BOBs)
    # LBGE row-to-row spacing is 25.4mm (10 pitches).
    j_esp_l = s.add_conn("J11", 22, 151.13, 88.90, FP_S22, "ESP32_Left",
                         mirror=True)
    # ESP32 right header (pins face right, all NC, mechanical only)
    j_esp_r = s.add_conn("J12", 22, 176.53, 88.90, FP_S22, "ESP32_Right")

    # ---- Section labels ----
    s.text("TI-99/4A Keyboard Adapter - Carrier Board (BSS138 rev)",
           70.866, 19.812, 3.0)
    s.text("POWER", 43.688, 27.178, 2.0)
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
    # +3V3 sinks: LV rail (socket pin 3) on each BOB LV side
    for lv in (j1_lv, j2_lv, j3_lv, j4_lv):
        s.pin_glabel(lv[3], "+3V3")

    # +5V source: external power input
    s.pin_glabel(j_pwr[1], "+5V")
    # +5V sinks: ESP32 5V0 (pin 21), HV rail (socket pin 3) on each BOB HV side
    s.pin_glabel(j_esp_l[21], "+5V", mirror=True)
    for hv in (j1_hv, j2_hv, j3_hv, j4_hv):
        s.pin_glabel(hv[3], "+5V", mirror=True)

    # GND source: external power input
    s.pin_glabel(j_pwr[2], "GND")
    # GND sinks: ESP32 GND; BOB GND is at socket pin 4 on both sides.
    # (Common ground between LV and HV is essential for the BSS138 to
    # work -- all grounds tie to the same net.)
    s.pin_glabel(j_esp_l[22], "GND", mirror=True)
    for lv in (j1_lv, j2_lv, j3_lv, j4_lv):
        s.pin_glabel(lv[4], "GND")
    for hv in (j1_hv, j2_hv, j3_hv, j4_hv):
        s.pin_glabel(hv[4], "GND", mirror=True)

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
    #   CH1=pin 1, CH2=pin 2, CH3=pin 5, CH4=pin 6
    #
    # LBGE ESP32-S3 header pin -> GPIO map (left header):
    #   pin 4=GPIO4, 5=GPIO5, 6=GPIO6, 7=GPIO7,
    #   pin 8=GPIO15, 9=GPIO16, 10=GPIO17, 11=GPIO18,
    #   pin 15=GPIO9, 16=GPIO10, 17=GPIO11, 18=GPIO12,
    #   pin 19=GPIO13, 20=GPIO14
    #
    # Format: (esp_pin, socket_pin, ti_pin, signal)

    # Channel allocation chosen to avoid trace crossings:
    # ESP32 GPIOs are sequential top-to-bottom on J11 (pins 4-11, then
    # a gap at 12-14, then 15-20). TI connector pins are sequential 1-15.
    # By giving each BOB a contiguous block of GPIOs AND a contiguous
    # block of TI pins, with BOBs ordered top-to-bottom, every wire
    # runs straight (no inter-BOB crossings).
    #
    #   BOB#1 (top):    GPIO 4-7   (J11 pins 4-7)   <-> TI 1-4
    #   BOB#2:          GPIO 15-18 (J11 pins 8-11)  <-> TI 5,7,8,9
    #                   (TI 6 = Alpha Lock, NC)
    #   BOB#3:          GPIO 9-12  (J11 pins 15-18) <-> TI 10-13
    #   BOB#4 (bottom): GPIO 13-14 (J11 pins 19-20) <-> TI 14-15
    #
    # Channel-to-socket-pin mapping (same on LV and HV sides):
    #   CH1=pin 1, CH2=pin 2, CH3=pin 5, CH4=pin 6

    # BOB#1: TI 1-4
    bob1_nets = [
        (4, 1, 1, "INT5"),   # GPIO4 -> CH1 -> TI 1
        (5, 2, 2, "INT6"),   # GPIO5 -> CH2 -> TI 2
        (6, 5, 3, "INT8"),   # GPIO6 -> CH3 -> TI 3
        (7, 6, 4, "INT4"),   # GPIO7 -> CH4 -> TI 4
    ]
    # BOB#2: TI 5,7,8,9 (TI 6 Alpha Lock NC, so CH2 jumps from 5 to 7)
    bob2_nets = [
        (8,  1, 5, "INT3"),  # GPIO15 -> CH1 -> TI 5
        (9,  2, 7, "INT7"),  # GPIO16 -> CH2 -> TI 7
        (10, 5, 8, "1Y1"),   # GPIO17 -> CH3 -> TI 8
        (11, 6, 9, "1Y0"),   # GPIO18 -> CH4 -> TI 9
    ]
    # BOB#3: TI 10-13
    bob3_nets = [
        (15, 1, 10, "INT9"),  # GPIO9  -> CH1 -> TI 10
        (16, 2, 11, "INT10"), # GPIO10 -> CH2 -> TI 11
        (17, 5, 12, "2Y0"),   # GPIO11 -> CH3 -> TI 12
        (18, 6, 13, "2Y1"),   # GPIO12 -> CH4 -> TI 13
    ]
    # BOB#4: TI 14-15 on CH1/CH2; CH3/CH4 unused
    bob4_nets = [
        (19, 1, 14, "2Y2"),   # GPIO13 -> CH1 -> TI 14
        (20, 2, 15, "2Y3"),   # GPIO14 -> CH2 -> TI 15
    ]

    for bob_lv, bob_hv, nets in (
        (j1_lv, j1_hv, bob1_nets),
        (j2_lv, j2_hv, bob2_nets),
        (j3_lv, j3_hv, bob3_nets),
        (j4_lv, j4_hv, bob4_nets),
    ):
        for esp_pin, socket_pin, ti_pin, sig in nets:
            net_lv = f"{sig}_LV"   # ESP32 -> BOB LV (3V3 domain)
            net_hv = sig           # BOB HV -> TI    (5V domain)
            s.pin_label(j_esp_l[esp_pin], net_lv, mirror=True)
            s.pin_label(bob_lv[socket_pin], net_lv)
            s.pin_label(bob_hv[socket_pin], net_hv, mirror=True)
            s.pin_label(j_ti[ti_pin], net_hv,
                        wire_ext=10.16, label_offset=5.08)

    # ==================================================================
    # NO-CONNECTS
    # ==================================================================

    # ESP32 left header: RST (pin 3), GPIO46 (pin 14)
    # GPIO8 (pin 12) and GPIO3 (pin 13) are usable on LBGE, left open
    for p in [3, 14]:
        s.nc(*j_esp_l[p])

    # BOB#4 unused channels: CH3 (socket pin 5) and CH4 (socket pin 6)
    # on both LV and HV sides
    for p in [5, 6]:
        s.nc(*j4_lv[p])
        s.nc(*j4_hv[p])

    # TI pin 6: Alpha Lock (not connected -- software implementation)
    s.nc(*j_ti[6])

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
    print("     LV side: 1=LV1, 2=LV2, 3=LV(3V3), 4=GND, 5=LV3, 6=LV4")
    print("     HV side: 1=HV1, 2=HV2, 3=HV(5V),  4=GND, 5=HV3, 6=HV4")
    print("     LV and HV nets are intentionally separate ('<sig>_LV' vs '<sig>').")
    print("  3. Press F8 to 'Update PCB from Schematic'")
    print("  4. Place footprints: 4x PinSocket_1x06 pairs at 10mm row spacing")
    print("     for BOBs; ESP32 sockets at 25.4mm row spacing. Previous TXS")
    print("     layout will need a full redo.")
    print("  5. File > Fabrication Outputs > Gerbers to export for fab")


if __name__ == "__main__":
    main()
