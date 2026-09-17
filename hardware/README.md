# Hardware

PCB source for the AC-cutoff relay board.

| File | Format | Size |
|---|---|---|
| `6040_Dipak_Savkare_Relay.dip` | DipTrace PCB layout (`DTBOARD`) | 259 KB |
| `6040_Dipak_Savkare_Relay_MOSFET.dip` | DipTrace PCB layout (`DTBOARD`) | 264 KB |
| `pcb_drawing_dimensioned.pdf` | Dimensioned mechanical outline | 95 KB |

> **Two `.dip` files, and it is not recorded which supersedes which.** Both
> carry job `6040`. `…_Relay.dip` landed in 3f419da; `…_Relay_MOSFET.dip` was
> supplied later and is larger, so it is *probably* the same board after a
> MOSFET stage was added — but nothing here establishes that, and DipTrace is
> the only thing that can. Confirm before fabricating from either, and delete
> the dead one once you know.

## The dimensioned drawing

`pcb_drawing_dimensioned.pdf` is **CCTV-8CH ENCLOSURE PCB OUTLINE — REV D**: one
A4 vector sheet, viewed from the enclosure front (component side), **mm**, origin
at the **board top-left**. GitHub previews PDFs in the browser, so unlike the
`.dip` files this is readable by anyone — which is the reason to keep it here.

| | |
|---|---|
| Board | 100 × 120 × 1.0 mm, no cutouts |
| Mounting | 7× Ø5.2 NPTH (H1–H7), keep-out **Ø11 both sides**; board sits on all 7 cover bosses |
| Clamping | H1/H3/H6/H7 plate nib + screw clamp · H2/H4/H5 screw + washer, no nib |
| Hole grid | x = 6, 50, 94 · y = 6, 53, 106 |
| Height budget | component side **≤25.5** · back side **≤2.8** (to plate) |
| Terminal block | notch opening 82 × 14 along the bottom edge, x 9–91 |
| LED window | block 10.00 × 30.55 at REF A (17, 38.73); window 5.66 × 13.66 centred, centre (22, 54); block top 23.3 below the PCB face |
| Placement | board → enclosure offset X+3, Y+3 · enclosure inside coords for the window X 76–86, Y 41.73–72.28 |

The sheet carries one unresolved item, bold on the drawing itself:

> **VERIFY: boss top → plate ring gap ≈1.1**

That is a fit check against a physical enclosure, not something a layout
settles. It is open as committed.

Note the drawing is a **100 × 120** outline. If either `.dip` is a different
size, the drawing does not describe it — see the caveat above the file table.

## Opening it

`.dip` is **DipTrace**'s binary PCB-layout format — open it with DipTrace
(Windows; runs under Wine). It is not KiCad, Eagle or Altium, and no free
viewer reads it, so there is no way to inspect this file from a text diff.

It is a **board** file. If a matching schematic (`.dch`) exists it is not in
this repo; ask the board house for it alongside any Gerbers.

`.gitattributes` marks `*.dip` / `*.dch` as binary so git never attempts a
line diff, a merge, or an end-of-line conversion on them — any of which would
silently corrupt the file. Treat a PCB revision as a whole-file replacement.

## What is on the board

The notes below were **extracted from the binary** by pulling its UTF-16
strings, because nothing here can open DipTrace. Treat them as a lead, not a
bill of materials: a DipTrace board embeds the footprint libraries it was drawn
from, so string extraction **cannot tell a placed component from an unused
library entry**. Open the file in DipTrace for anything authoritative.

Signals named in the file: `AC_L`, `AC_N`, `AC1`, `AC2`, `+VO`, `-VO`, `DC+`,
`DC-`, `IN+`, `IN-`, `OUT+`, `OUT-`, `VCC`, `GND`, `3.3`, `ADC`, `RST`,
`CH_PD`, `TX/1`, `RX/3`.

Parts named in the file:

- **HLK-5M12** — Hi-Link 5 W AC-DC module, 85–264 VAC in, 12 V / 450 mA out,
  3 kV isolation, 38 × 23 mm DIP. `HLK-PM01` footprints also appear.
- **Varistor** (`EPCOS_CD_1003`) — mains-side surge clamp.
- **Relay** footprints, and a `TL1105L` tactile switch.
- **Kingbright LED(BI)-3R** bi-colour 3 mm LED, plus other LED footprints.
- **Molex 39531000 / 395310003** 5.08 mm shrouded terminal blocks, and
  Sullins 2.54 mm headers (`PPTC031LFBN-RC`, `PPTC041LFBN-RC`).
- 1206 chip resistors/diodes, SOT-23 / SOT-223, radial and film capacitors,
  axial resistors.

Board/job identifiers in the file: `BE6040`, `JT-PCB_T` — `6040` matches the
filename, so it is most likely the fabricator's job number.

### One thing worth checking

The file also references **`ESP12` / `ESP12E` / `ESP 12-Q`** footprints and a
`SOCKET-28-3`. ESP-12 is an **ESP8266** module, whereas this project's firmware
targets the ESP32-C3 Super Mini and the ESP32 WROOM DevKit V1 (see
`docs/PINOUT.md`). That is either a leftover library entry or a sign this
layout predates the current MCU choice — worth confirming in DipTrace before
anyone fabricates from it.

## Provenance

Supplied as-is by the board designer. The file embeds absolute library paths
from the designer's machine (`C:\Users\...`, `D:\Bhatt Google Drive\...`) and
depends on a custom `Foot Print.lib` that is **not** included, so DipTrace may
report missing libraries on open. Nothing in it is a credential.
