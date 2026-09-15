# Hardware

PCB design files for the AC energy meter's relay stage.

| File | What it is |
|---|---|
| `6040_Dipak_Savkare_Relay_MOSFET.dip` | DipTrace **PCB layout** — relay + MOSFET driver board |

## Opening these

`.dip` is a DipTrace PCB layout (binary, `DTBOARD` magic). Open it with
DipTrace — https://diptrace.com. The free edition is pin/signal limited, so a
board that exceeds those limits needs a licensed copy to open and edit.

A matching schematic would be a `.dch` file; none is committed yet.

## Working with them in git

These are binary, and `.gitattributes` marks them so. Git will not diff them
and will not attempt a line merge — a merged layout file would be corrupt, not
merely wrong.

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
