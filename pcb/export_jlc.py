#!/usr/bin/env python3
"""Export JLCPCB assembly files for the ti99-kb-adapter board.

Produces (in pcb/gerbers/):
  ti99-kb-adapter_BOM.csv  -- assembly BOM: every schematic part that
                              carries an "LCSC" property (V7: Q1-Q16
                              BSS138 = C78284, R1-R32 10k = C17414).
                              Parts without an LCSC field (connectors,
                              ESP32 socket) are hand-soldered and
                              excluded on purpose.
  ti99-kb-adapter_CPL.csv  -- placement file, converted from
                              `kicad-cli pcb export pos` to JLC's
                              column names, filtered to BOM refs.

Usage:  python export_jlc.py           (both; CPL skipped if kicad-cli
                                        or the laid-out board is missing)
        python export_jlc.py bom
        python export_jlc.py cpl

NOTE: JLC's part rotation convention sometimes differs from KiCad's
(SOT-23 commonly needs +/-180). ALWAYS eyeball the part-placement
preview during JLC order review before paying.
"""

import csv
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "ti99-kb-adapter.kicad_sch")
PCB = os.path.join(HERE, "ti99-kb-adapter.kicad_pcb")
OUT = os.path.join(HERE, "gerbers")


def sch_parts():
    """(ref, value, footprint, lcsc) for every symbol instance with an
    LCSC property, parsed from the generated schematic."""
    text = open(SCH, encoding="utf-8").read()
    parts = []
    # Instance blocks are two-space-indented "(symbol" ... ")" at the
    # same level (generator output is stable).
    for block in re.findall(r"\n  \(symbol\n[\s\S]*?\n  \)", text):
        def prop(name):
            m = re.search(r'\(property "%s" "([^"]*)"' % name, block)
            return m.group(1) if m else ""
        ref, lcsc = prop("Reference"), prop("LCSC")
        if lcsc and ref:
            parts.append((ref, prop("Value"), prop("Footprint"), lcsc))
    return parts


def export_bom():
    parts = sch_parts()
    groups = {}
    for ref, val, fp, lcsc in parts:
        fp_short = fp.split(":")[-1]
        groups.setdefault((val, fp_short, lcsc), []).append(ref)
    path = os.path.join(OUT, "ti99-kb-adapter_BOM.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
        for (val, fp, lcsc), refs in sorted(groups.items()):
            refs.sort(key=lambda r: (r.rstrip("0123456789"),
                                     int(re.search(r"\d+$", r).group())))
            w.writerow([val, ",".join(refs), fp, lcsc])
    print(f"wrote {path}  ({len(parts)} parts, {len(groups)} line items)")
    return {ref for ref, *_ in parts}


def export_cpl(bom_refs):
    cli = shutil.which("kicad-cli")
    if not cli:
        hits = [os.path.join(r, "kicad-cli.exe")
                for r in [r"C:\Program Files\KiCad"]
                for r, _, files in os.walk(r) if "kicad-cli.exe" in files]
        cli = hits[-1] if hits else None
    if not cli:
        print("CPL skipped: kicad-cli not found")
        return
    raw = os.path.join(OUT, "_pos_raw.csv")
    r = subprocess.run([cli, "pcb", "export", "pos", "--format", "csv",
                        "--units", "mm", "--side", "both", "-o", raw, PCB],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"CPL skipped: kicad-cli pos failed: {r.stderr.strip()}")
        return
    path = os.path.join(OUT, "ti99-kb-adapter_CPL.csv")
    kept = 0
    with open(raw, newline="", encoding="utf-8") as fin, \
         open(path, "w", newline="", encoding="utf-8") as fout:
        rd = csv.DictReader(fin)
        w = csv.writer(fout)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in rd:
            ref = row.get("Ref") or row.get("Designator") or ""
            if ref not in bom_refs:
                continue
            side = (row.get("Side") or "top").strip().lower()
            w.writerow([ref, row.get("PosX"), row.get("PosY"),
                        "Top" if side in ("top", "front") else "Bottom",
                        row.get("Rot")])
            kept += 1
    os.remove(raw)
    print(f"wrote {path}  ({kept} placements)")
    if kept < len(bom_refs):
        print(f"WARNING: {len(bom_refs) - kept} BOM refs missing from the "
              f"board -- run F8 + placement in KiCad first.")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    refs = export_bom() if what in ("all", "bom") else {
        ref for ref, *_ in sch_parts()}
    if what in ("all", "cpl"):
        export_cpl(refs)
