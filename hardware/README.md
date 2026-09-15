# Hardware

PCB source for the AC-cutoff relay board.

| File | Format | Size |
|---|---|---|
| `6040_Dipak_Savkare_Relay.dip` | DipTrace PCB layout (`DTBOARD`) | 259 KB |

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
