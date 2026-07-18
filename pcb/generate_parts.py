"""
generate_parts.py — One-time generator for project-specific KiCad parts.

Writes:
  lib/ti99-parts.kicad_sym                          (symbol library)
  lib/ti99-parts.pretty/BOB-12009.kicad_mod         (footprint)
  lib/ti99-parts.pretty/ESP32-S3-N16R8.kicad_mod    (footprint)
  lib/ti99-parts.pretty/J13-CableHeader-1x04.kicad_mod  (footprint)

Run ONCE. After that, the generated files are committed to the repo
and this script is kept as a record of how they were made. Do not
re-run unless you intentionally want to regenerate.

------------------------------------------------------------------------
Why custom symbols + footprints?

Without them, each physical part has to be represented by two unrelated
schematic symbols (one Conn_01xN per header row). KiCad has no way to
know the rows belong to the same physical part, so when you "Update PCB
from Schematic", the rows land at arbitrary positions and you have to
manually realign every pull. A unified symbol+footprint pair bakes the
geometry in.

Multi-unit trick: KiCad symbols can have multiple "units" sharing one
footprint. We use a 2-unit symbol per part — unit A is the LV row (or
left header), unit B is the HV row (or right header). On the schematic
each unit can be placed independently for clean wiring, but they
collapse to one footprint with one reference designator on the PCB.
Pin numbers across units are unique (1..6 + 7..12 for the BOB; 1..22 +
23..44 for the ESP32) and map directly to footprint pad numbers.

------------------------------------------------------------------------
KiCad 10 file format notes (the painful bits):

KiCad 10 silently rejects libraries that don't match its expected
verbose s-expression format. The compact format that works inside a
schematic's lib_symbols cache is NOT enough for a standalone library
file. Required fields:

  Symbol library (.kicad_sym):
    - (version 20251024)
    - Each property has (show_name no), (do_not_autoplace no), and
      (effects (font (size N N))) on separate sub-lines
    - Symbol-level (in_pos_files yes), (duplicate_pin_numbers_are_jumpers no)
    - Hidden properties have a top-level (hide yes), not inside effects

  Footprint (.kicad_mod):
    - (version 20260206)
    - Each property multi-line with (at), (layer), (effects)
    - (duplicate_pad_numbers_are_jumpers no) at top level
    - (attr through_hole) for THT footprints
    - fp_line, fp_rect, pad elements all multi-line with stroke/fill

These were reverse-engineered from KiCad 10's stock libraries:
  C:/Program Files/KiCad/10.0/share/kicad/symbols/Connector_Generic.kicad_sym
  C:/Program Files/KiCad/10.0/share/kicad/footprints/Connector_PinHeader_2.54mm.pretty/
"""

import os

PROJECT = "ti99-parts"
SYM_VERSION = "20251024"
FP_VERSION = "20260206"
GENERATOR = "ti99_parts_gen"
GEN_VERSION = "10.0"

# Output directory layout
PCB_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_DIR = os.path.join(PCB_DIR, "lib")
SYM_FILE = os.path.join(LIB_DIR, f"{PROJECT}.kicad_sym")
FP_DIR = os.path.join(LIB_DIR, f"{PROJECT}.pretty")


# ============================================================================
# Symbol library  —  ti99-parts.kicad_sym
# ============================================================================

def sym_property(name, value, x, y, indent, hide=False):
    """One (property ...) block in KiCad 10 verbose format."""
    pad = "\t" * indent
    hide_line = f"{pad}\t(hide yes)\n" if hide else ""
    return (
        f'{pad}(property "{name}" "{value}"\n'
        f"{pad}\t(at {x:.2f} {y:.2f} 0)\n"
        f"{pad}\t(show_name no)\n"
        f"{pad}\t(do_not_autoplace no)\n"
        f"{hide_line}"
        f"{pad}\t(effects\n"
        f"{pad}\t\t(font\n"
        f"{pad}\t\t\t(size 1.27 1.27)\n"
        f"{pad}\t\t)\n"
        f"{pad}\t)\n"
        f"{pad})"
    )


def sym_pin(x, y, angle, name, number, indent, length=3.81):
    """One (pin ...) block in KiCad 10 verbose format."""
    pad = "\t" * indent
    return (
        f"{pad}(pin passive line\n"
        f"{pad}\t(at {x:.2f} {y:.2f} {angle})\n"
        f"{pad}\t(length {length})\n"
        f'{pad}\t(name "{name}"\n'
        f"{pad}\t\t(effects\n"
        f"{pad}\t\t\t(font\n"
        f"{pad}\t\t\t\t(size 1.27 1.27)\n"
        f"{pad}\t\t\t)\n"
        f"{pad}\t\t)\n"
        f"{pad}\t)\n"
        f'{pad}\t(number "{number}"\n'
        f"{pad}\t\t(effects\n"
        f"{pad}\t\t\t(font\n"
        f"{pad}\t\t\t\t(size 1.27 1.27)\n"
        f"{pad}\t\t\t)\n"
        f"{pad}\t\t)\n"
        f"{pad}\t)\n"
        f"{pad})"
    )


def sym_rectangle(x1, y1, x2, y2, indent):
    pad = "\t" * indent
    return (
        f"{pad}(rectangle\n"
        f"{pad}\t(start {x1:.2f} {y1:.2f})\n"
        f"{pad}\t(end {x2:.2f} {y2:.2f})\n"
        f"{pad}\t(stroke\n"
        f"{pad}\t\t(width 0.254)\n"
        f"{pad}\t\t(type default)\n"
        f"{pad}\t)\n"
        f"{pad}\t(fill\n"
        f"{pad}\t\t(type background)\n"
        f"{pad}\t)\n"
        f"{pad})"
    )


def sym_unit(part_base, unit_idx, pins, body_h, indent):
    """One unit subsymbol of a multi-unit symbol."""
    pad = "\t" * indent
    sub = f"{part_base}_{unit_idx}_1"
    rect = sym_rectangle(-1.27, body_h, 1.27, -body_h, indent + 1)
    pin_blocks = "\n".join(
        sym_pin(x, y, a, n, num, indent + 1) for (x, y, a, n, num) in pins
    )
    return (
        f'{pad}(symbol "{sub}"\n'
        f"{rect}\n"
        f"{pin_blocks}\n"
        f"{pad})"
    )


def sym_part(name, ref_letter, value, footprint, datasheet, description,
             units, body_h, lib_prefix=""):
    """One full multi-unit (symbol ...) block.

    units: list of [(x, y, angle, label, pad_num), ...] per unit.
    body_h: rectangle half-height.
    lib_prefix: empty for standalone .kicad_sym, "ti99-parts:" for
                schematic embedded lib_symbols cache.
    """
    indent = 1  # symbols sit one tab inside (kicad_symbol_lib ...)
    pad = "\t" * indent
    base = name
    full_name = f"{lib_prefix}{base}"
    head = body_h + 1.27
    foot = -(body_h + 1.27)

    properties = "\n".join([
        sym_property("Reference", ref_letter, 0, head, indent + 1),
        sym_property("Value", base, 0, foot, indent + 1),
        sym_property("Footprint", f"{PROJECT}:{base}", 0, 0, indent + 1,
                     hide=True),
        sym_property("Datasheet", datasheet, 0, 0, indent + 1, hide=True),
        sym_property("Description", description, 0, 0, indent + 1, hide=True),
    ])

    unit_blocks = "\n".join(
        sym_unit(base, i + 1, pins, body_h, indent + 1)
        for i, pins in enumerate(units)
    )

    return (
        f'{pad}(symbol "{full_name}"\n'
        f"{pad}\t(pin_names\n"
        f"{pad}\t\t(offset 1.016)\n"
        f"{pad}\t)\n"
        f"{pad}\t(exclude_from_sim no)\n"
        f"{pad}\t(in_bom yes)\n"
        f"{pad}\t(on_board yes)\n"
        f"{pad}\t(in_pos_files yes)\n"
        f"{pad}\t(duplicate_pin_numbers_are_jumpers no)\n"
        f"{properties}\n"
        f"{unit_blocks}\n"
        f"{pad}\t(embedded_fonts no)\n"
        f"{pad})"
    )


# ----------------------------------------------------------------------------
# BOB-12009 multi-unit symbol
# ----------------------------------------------------------------------------

def bob_12009_symbol(lib_prefix=""):
    """SparkFun BOB-12009 BSS138 4-channel level shifter.

    Pin numbering follows the corrected silkscreen layout:
      LV row (unit A):  pad 1=LV4, 2=LV3, 3=GND, 4=LV(3V3), 5=LV2, 6=LV1
      HV row (unit B):  pad 7=HV4, 8=HV3, 9=GND, 10=HV(5V), 11=HV2, 12=HV1
    """
    body_h = 7.62
    # 6 pins, 2.54mm pitch — top pin at +6.35, bottom at -6.35
    lv_labels = ["LV4", "LV3", "GND", "LV", "LV2", "LV1"]
    hv_labels = ["HV4", "HV3", "GND", "HV", "HV2", "HV1"]

    lv_pins = []
    for k, label in enumerate(lv_labels, start=1):
        py = 6.35 - (k - 1) * 2.54
        # angle 180: pin extends leftward, so (at 5.08 py 180) puts the
        # connection point on the right side of the body, line going into
        # the body. Connections wire to the right.
        lv_pins.append((5.08, py, 180, label, k))

    hv_pins = []
    for k, label in enumerate(hv_labels, start=1):
        py = 6.35 - (k - 1) * 2.54
        hv_pins.append((5.08, py, 180, label, k + 6))

    return sym_part(
        name="BOB-12009",
        ref_letter="J",
        value="BOB-12009",
        footprint=f"{PROJECT}:BOB-12009",
        datasheet="https://www.sparkfun.com/products/12009",
        description="BSS138 4-channel level shifter, 10mm row spacing",
        units=[lv_pins, hv_pins],
        body_h=body_h,
        lib_prefix=lib_prefix,
    )


# ----------------------------------------------------------------------------
# ESP32-S3-N16R8 multi-unit symbol
# ----------------------------------------------------------------------------

# ESP32-S3-DevKitC-1 reference pinout (Espressif). Hosyond N16R8, LBGE
# Gold Edition, and other clones all follow this layout.
ESP32_LEFT_LABELS = [
    "3V3", "3V3", "EN", "IO4", "IO5", "IO6", "IO7", "IO15",
    "IO16", "IO17", "IO18", "IO8", "IO3", "IO46", "IO9", "IO10",
    "IO11", "IO12", "IO13", "IO14", "5V", "GND",
]
ESP32_RIGHT_LABELS = [
    "GND", "TXD0", "RXD0", "IO1", "IO2", "IO42", "IO41", "IO40",
    "IO39", "IO38", "IO37", "IO36", "IO35", "IO0", "IO45", "IO48",
    "IO47", "IO21", "IO20", "IO19", "GND", "GND",
]


def esp32_symbol(lib_prefix=""):
    """Hosyond ESP32-S3 N16R8 / DevKitC-1 form factor dev board.

    Unit A = left header (pads 1-22).
    Unit B = right header (pads 23-44).
    """
    body_h = 27.94
    n = 22
    top_y = (n - 1) / 2 * 2.54

    def header_pins(labels, base_pad):
        out = []
        for k, label in enumerate(labels, start=1):
            py = top_y - (k - 1) * 2.54
            out.append((5.08, py, 180, label, base_pad + k - 1))
        return out

    return sym_part(
        name="ESP32-S3-N16R8",
        ref_letter="U",
        value="ESP32-S3-N16R8",
        footprint=f"{PROJECT}:ESP32-S3-N16R8",
        datasheet=("https://docs.espressif.com/projects/esp-dev-kits/"
                   "en/latest/esp32s3/esp32-s3-devkitc-1/"),
        description=("ESP32-S3-WROOM-1 dev board, 2x22 0.1in headers, "
                     "25.4mm row spacing"),
        units=[
            header_pins(ESP32_LEFT_LABELS, base_pad=1),
            header_pins(ESP32_RIGHT_LABELS, base_pad=23),
        ],
        body_h=body_h,
        lib_prefix=lib_prefix,
    )


def emit_symbol_lib():
    parts = [bob_12009_symbol(), esp32_symbol()]
    return (
        "(kicad_symbol_lib\n"
        f"\t(version {SYM_VERSION})\n"
        f'\t(generator "{GENERATOR}")\n'
        f'\t(generator_version "{GEN_VERSION}")\n'
        + "\n".join(parts) + "\n"
        ")\n"
    )


# ============================================================================
# Footprints  (KiCad 10 verbose format)
# ============================================================================

def fp_property(name, value, x, y, layer, indent, hide=False, size=1.0,
                thickness=0.15):
    pad = "\t" * indent
    hide_line = f"{pad}\t(hide yes)\n" if hide else ""
    return (
        f'{pad}(property "{name}" "{value}"\n'
        f"{pad}\t(at {x:.3f} {y:.3f} 0)\n"
        f'{pad}\t(layer "{layer}")\n'
        f"{hide_line}"
        f"{pad}\t(effects\n"
        f"{pad}\t\t(font\n"
        f"{pad}\t\t\t(size {size:.2f} {size:.2f})\n"
        f"{pad}\t\t\t(thickness {thickness:.2f})\n"
        f"{pad}\t\t)\n"
        f"{pad}\t)\n"
        f"{pad})"
    )


def fp_pad(num, x, y, drill, pad_dia, indent, shape="circle", pad_dia_y=None):
    """Through-hole pad. Pass pad_dia_y for oval pads (width != height)."""
    pad = "\t" * indent
    sx = pad_dia
    sy = pad_dia if pad_dia_y is None else pad_dia_y
    return (
        f'{pad}(pad "{num}" thru_hole {shape}\n'
        f"{pad}\t(at {x:.3f} {y:.3f})\n"
        f"{pad}\t(size {sx:.2f} {sy:.2f})\n"
        f"{pad}\t(drill {drill:.2f})\n"
        f'{pad}\t(layers "*.Cu" "*.Mask")\n'
        f"{pad}\t(remove_unused_layers no)\n"
        f"{pad})"
    )


def fp_text(content, x, y, layer, indent, size=0.7, thickness=0.12):
    pad = "\t" * indent
    return (
        f'{pad}(fp_text user "{content}"\n'
        f"{pad}\t(at {x:.3f} {y:.3f} 0)\n"
        f'{pad}\t(layer "{layer}")\n'
        f"{pad}\t(effects\n"
        f"{pad}\t\t(font\n"
        f"{pad}\t\t\t(size {size:.2f} {size:.2f})\n"
        f"{pad}\t\t\t(thickness {thickness:.2f})\n"
        f"{pad}\t\t)\n"
        f"{pad}\t)\n"
        f"{pad})"
    )


def fp_line(x1, y1, x2, y2, layer, indent, width=0.12):
    pad = "\t" * indent
    return (
        f"{pad}(fp_line\n"
        f"{pad}\t(start {x1:.3f} {y1:.3f})\n"
        f"{pad}\t(end {x2:.3f} {y2:.3f})\n"
        f"{pad}\t(stroke\n"
        f"{pad}\t\t(width {width:.2f})\n"
        f"{pad}\t\t(type solid)\n"
        f"{pad}\t)\n"
        f'{pad}\t(layer "{layer}")\n'
        f"{pad})"
    )


def fp_rect_box(x1, y1, x2, y2, layer, indent):
    """Rectangle drawn as four fp_lines (silkscreen / courtyard outline)."""
    width = 0.05 if layer == "F.CrtYd" else 0.12
    return "\n".join([
        fp_line(x1, y1, x2, y1, layer, indent, width),
        fp_line(x2, y1, x2, y2, layer, indent, width),
        fp_line(x2, y2, x1, y2, layer, indent, width),
        fp_line(x1, y2, x1, y1, layer, indent, width),
    ])


def fp_header(name, descr, tags):
    """Match stock KiCad 10 footprint header field order:
       version -> generator -> layer -> descr -> tags -> property* -> attr
       -> duplicate_pad_numbers_are_jumpers -> body...
    No (generator_version) for footprints — stock doesn't emit it.
    """
    indent = 1
    pad = "\t" * indent
    ref_prop = fp_property("Reference", "REF**", 0, 0, "F.SilkS", indent)
    val_prop = fp_property("Value", name, 0, 0, "F.Fab", indent)
    return (
        f'(footprint "{name}"\n'
        f"{pad}(version {FP_VERSION})\n"
        f'{pad}(generator "{GENERATOR}")\n'
        f'{pad}(layer "F.Cu")\n'
        f'{pad}(descr "{descr}")\n'
        f'{pad}(tags "{tags}")\n'
        f"{ref_prop}\n"
        f"{val_prop}\n"
        f"{pad}(attr through_hole)\n"
        f"{pad}(duplicate_pad_numbers_are_jumpers no)"
    )


# ----------------------------------------------------------------------------
# BOB-12009 footprint
# ----------------------------------------------------------------------------

def emit_bob_12009_fp():
    """2x6 through-hole pads, 10.00mm row spacing.

    Pin map:  LV row (y=-5.0): 1=LV4 2=LV3 3=GND 4=LV(3V3) 5=LV2 6=LV1
              HV row (y=+5.0): 7=HV4 8=HV3 9=GND 10=HV(5V) 11=HV2 12=HV1
    """
    indent = 1
    pitch = 2.54
    row_y = 5.0  # half of 10mm row spacing
    xs = [(-2.5 + k) * pitch for k in range(6)]

    pads = []
    lv_labels = ["LV4", "LV3", "GND", "LV", "LV2", "LV1"]
    for i, x in enumerate(xs):
        shape = "rect" if i == 0 else "circle"
        pads.append(fp_pad(i + 1, x, -row_y, 1.0, 1.7, indent, shape))
    hv_labels = ["HV4", "HV3", "GND", "HV", "HV2", "HV1"]
    for i, x in enumerate(xs):
        pads.append(fp_pad(i + 7, x, +row_y, 1.0, 1.7, indent))

    silk = []
    for x, lab in zip(xs, lv_labels):
        silk.append(fp_text(lab, x, -row_y - 1.6, "F.SilkS", indent, size=0.7))
    for x, lab in zip(xs, hv_labels):
        silk.append(fp_text(lab, x, +row_y + 1.6, "F.SilkS", indent, size=0.7))

    body_w = 8.5
    body_h = 8.0
    silk.append(fp_rect_box(-body_w, -body_h, body_w, body_h,
                            "F.SilkS", indent))
    silk.append(fp_rect_box(-body_w - 0.5, -body_h - 0.5,
                            body_w + 0.5, body_h + 0.5,
                            "F.CrtYd", indent))

    body = "\n".join(pads + silk)
    return (
        fp_header(
            "BOB-12009",
            "SparkFun BOB-12009 BSS138 4-channel level shifter, "
            "2x6 0.1in pin sockets, 10mm row spacing",
            "level-shifter BSS138 BOB-12009"
        )
        + "\n" + body + "\n\t(embedded_fonts no)\n)\n"
    )


# ----------------------------------------------------------------------------
# ESP32-S3-N16R8 footprint
# ----------------------------------------------------------------------------

def emit_esp32_fp():
    """2x22 through-hole pads, 25.4mm row spacing.

    Pad 1 is at the TOP of the footprint (smallest y in KiCad's
    downward-Y convention) to match the schematic symbol layout where
    pin 1 (3V3) is at the top. Without this, traces from the ESP32 to
    the BOBs end up vertically inverted on the PCB.
    """
    indent = 1
    pitch = 2.54
    row_y = 12.7
    n = 22
    top_y = (n - 1) / 2 * pitch  # half the total pin span

    pads = []
    for i in range(n):
        py = i * pitch - top_y      # i=0 -> -26.67 (top), i=21 -> +26.67 (bottom)
        shape = "rect" if i == 0 else "circle"
        pads.append(fp_pad(i + 1, -row_y, py, 1.0, 1.7, indent, shape))
    for i in range(n):
        py = i * pitch - top_y
        pads.append(fp_pad(i + 23, +row_y, py, 1.0, 1.7, indent))

    silk = []
    for i, lab in enumerate(ESP32_LEFT_LABELS):
        py = i * pitch - top_y
        silk.append(fp_text(lab, -row_y + 3.5, py, "F.SilkS", indent, size=0.6))
    for i, lab in enumerate(ESP32_RIGHT_LABELS):
        py = i * pitch - top_y
        silk.append(fp_text(lab, +row_y - 3.5, py, "F.SilkS", indent, size=0.6))

    body_w = 13.5
    body_h = top_y + 2.5
    silk.append(fp_rect_box(-body_w, -body_h, body_w, body_h,
                            "F.SilkS", indent))

    # USB-C cutout indicator at the +Y edge
    usb_w = 4.5
    silk.append(fp_line(-usb_w, body_h, -usb_w, body_h + 1.5,
                        "F.SilkS", indent))
    silk.append(fp_line(-usb_w, body_h + 1.5, usb_w, body_h + 1.5,
                        "F.SilkS", indent))
    silk.append(fp_line(usb_w, body_h + 1.5, usb_w, body_h,
                        "F.SilkS", indent))
    silk.append(fp_text("USB-C", 0, body_h + 0.8, "F.SilkS", indent, size=0.7))

    silk.append(fp_rect_box(-body_w - 0.5, -body_h - 1.0,
                            body_w + 0.5, body_h + 1.0,
                            "F.CrtYd", indent))

    body = "\n".join(pads + silk)
    return (
        fp_header(
            "ESP32-S3-N16R8",
            "Hosyond ESP32-S3 N16R8 (compatible with ESP32-S3-DevKitC-1 "
            "form factor), 2x22 0.1in headers, 25.4mm row spacing",
            "ESP32-S3 DevKitC-1 Hosyond"
        )
        + "\n" + body + "\n\t(embedded_fonts no)\n)\n"
    )


# ----------------------------------------------------------------------------
# J13 cable header footprint (1.1mm drill for 18-22 AWG hookup wire)
# ----------------------------------------------------------------------------

def emit_j13_cable_header_fp():
    """1x4 cable-solder header, 3.81mm pitch (wider than 0.1in spec).

    No header plugs in here — wires from a pre-built TI PSU cable solder
    directly into the holes — so 0.1in pitch isn't required. Spread to
    3.81mm so the pads keep a healthy gap between them, leaving room for
    a solder fillet and avoiding the overlap the original 2.54mm pitch
    caused.

    Drill 1.1mm / pad 2.2mm round — sized for the 18-22 AWG hookup wire
    used on the TI PSU daisy-chain. (Was 2.0mm/3.0mm for 14 AWG, which
    left the thin power wire floating in the hole with no solid joint.)
    """
    indent = 1
    pitch = 3.81
    n = 4
    xs = [(k - (n - 1) / 2) * pitch for k in range(n)]

    pads = []
    for i, x in enumerate(xs):
        shape = "rect" if i == 0 else "circle"
        pads.append(fp_pad(i + 1, x, 0, drill=1.1, pad_dia=2.2,
                           indent=indent, shape=shape))

    silk = []
    for i, x in enumerate(xs):
        silk.append(fp_text(str(i + 1), x, -2.5, "F.SilkS", indent, size=0.7))
    silk.append(fp_text("-5V",  xs[0], 2.5, "F.SilkS", indent, size=0.6))
    silk.append(fp_text("+12V", xs[1], 2.5, "F.SilkS", indent, size=0.6))
    silk.append(fp_text("GND",  xs[2], 2.5, "F.SilkS", indent, size=0.6))
    silk.append(fp_text("+5V",  xs[3], 2.5, "F.SilkS", indent, size=0.6))

    body_w = abs(xs[0]) + 2.0
    body_h = 3.5
    silk.append(fp_rect_box(-body_w, -body_h, body_w, body_h,
                            "F.SilkS", indent))
    silk.append(fp_rect_box(-body_w - 0.5, -body_h - 0.5,
                            body_w + 0.5, body_h + 0.5,
                            "F.CrtYd", indent))

    body = "\n".join(pads + silk)
    return (
        fp_header(
            "J13-CableHeader-1x04",
            "1x4 solder header, 3.81mm pitch, 1.1mm drill / 2.2mm pad "
            "for 18-22 AWG hookup wire (TI PSU daisy-chain cable)",
            "cable-header thru-hole"
        )
        + "\n" + body + "\n\t(embedded_fonts no)\n)\n"
    )


# ============================================================================
# Main
# ============================================================================

def write(path, content):
    with open(path, "w", newline="\n") as f:
        f.write(content)
    print(f"  wrote {os.path.relpath(path, PCB_DIR)}")


def main():
    os.makedirs(LIB_DIR, exist_ok=True)
    os.makedirs(FP_DIR, exist_ok=True)

    print("Generating ti99-parts library...")
    write(SYM_FILE, emit_symbol_lib())
    write(os.path.join(FP_DIR, "BOB-12009.kicad_mod"), emit_bob_12009_fp())
    write(os.path.join(FP_DIR, "ESP32-S3-N16R8.kicad_mod"), emit_esp32_fp())
    write(
        os.path.join(FP_DIR, "J13-CableHeader-1x04.kicad_mod"),
        emit_j13_cable_header_fp(),
    )

    print()
    print("Done. Open ti99-kb-adapter.kicad_pro and verify:")
    print(f"  - Symbol Editor -> Browse: ti99-parts shows BOB-12009 + ESP32-S3-N16R8")
    print(f"  - Footprint Editor -> Browse: ti99-parts shows the three footprints")


if __name__ == "__main__":
    main()
