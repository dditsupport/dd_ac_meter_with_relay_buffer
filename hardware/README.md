# Hardware

PCB design files for the AC energy meter's relay stage.

| File | What it is |
|---|---|
| `6040_Dipak_Savkare_Relay_MOSFET.dip` | DipTrace **PCB layout** — relay + MOSFET driver board |
| `pcb_drawing_dimensioned.pdf` | Dimensioned **mechanical outline** — board profile, mounting holes, keep-outs, height budget |

## The mechanical drawing

`pcb_drawing_dimensioned.pdf` is **CCTV-8CH ENCLOSURE PCB OUTLINE — REV D**: a
single A4 sheet, vector, viewed from the enclosure front (component side).
Everything is in **mm**, origin at the **board top-left**. GitHub previews it in
the browser, so it is readable without any EDA tool.

What it constrains:

| | |
|---|---|
| Board | 100 × 120 × 1.0 mm, no cutouts |
| Mounting | 7× Ø5.2 NPTH (H1–H7), keep-out **Ø11 both sides**; the board sits on all 7 cover bosses |
| Clamping | H1/H3/H6/H7 plate nib + screw clamp · H2/H4/H5 screw + washer, no nib |
| Hole grid | x = 6, 50, 94 · y = 6, 53, 106 |
| Height budget | component side **≤25.5** · back side **≤2.8** (to plate) |
| Terminal block | notch opening 82 × 14 along the bottom edge, x 9–91 |
| LED window | block 10.00 × 30.55 at REF A (17, 38.73); window 5.66 × 13.66 centred, centre (22, 54); block top 23.3 below the PCB face |
| Placement | board → enclosure offset X+3, Y+3 · enclosure inside coords for the window X 76–86, Y 41.73–72.28 |

The sheet carries one open item, in bold on the drawing itself:

> **VERIFY: boss top → plate ring gap ≈1.1**

That is a fit check against the real enclosure, not something the layout can
settle. It is unresolved as committed.

**Note the two files describe different things.** The drawing is a 100 × 120
enclosure outline; the DipTrace file is named `6040_…`. Whether that is a board
size, a job number, or a different board entirely is not recorded anywhere in
the repository — worth pinning down before anyone builds to the pair.

## Opening these

`.dip` is a DipTrace PCB layout (binary, `DTBOARD` magic). Open it with
DipTrace — https://diptrace.com. The free edition is pin/signal limited, so a
board that exceeds those limits needs a licensed copy to open and edit.

A matching schematic would be a `.dch` file; none is committed yet.

The `.pdf` needs nothing but a viewer, which is the point of keeping it here
next to the layout: the mechanical constraints stay readable to anyone, whether
or not they can open the DipTrace source.

## Working with them in git

These are binary, and `.gitattributes` marks them so. Git will not diff them
and will not attempt a line merge — a merged layout file would be corrupt, not
merely wrong. (The PDF is a rendered output, so it is only ever replaced
wholesale; the caveat below is really about the editable sources.)

So they cannot be merged: if two people edit the same board in parallel, one
version is lost. Treat a board file as checked out by one person at a time, and
say so in the commit when you change one, since the diff cannot show it.

## What the board drives

Context for the relay stage is in the firmware and docs, not here:

- `docs/PINOUT.md` — wiring, and why the relay drives a **contactor** rather
  than switching the compressor directly. Units are 1.5–2 ton and a bare PCB
  relay will not survive the inrush.
- `firmware/*/relay.cpp` — the compressor-aware, fail-safe cutoff logic and the
  schedule convention (the schedule is AC-**allowed** open hours; firmware
  older than `2.0.0` inverts this and cuts the AC during open hours).
