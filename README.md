# AC Energy Meter

Single-phase AC energy metering for any 230 V circuit or load. The device
measures voltage, current, real power, energy, power factor, and frequency
with a PZEM-004T v3.0, buffers 15-minute readings to internal flash, and
mirrors them to a MilesWeb-hosted PHP/MySQL backend via two store-and-forward
paths:

- **Wi-Fi** — when a known hotspot is in range, the ESP32 connects, NTP-syncs,
  POSTs buffered rows, and clears them on server ACK.
- **BLE** — a companion Android app pulls buffered rows over GATT and
  forwards them to the same MilesWeb endpoint over the phone's cellular
  data.

The ESP32 is the source of truth. Cloud and app are catch-up mirrors.

## Relay: compressor-aware AC cutoff

Each meter drives a relay (default GPIO 10) that acts as an **off-hours AC
cutoff** — a safety net for when a store manager forgets to switch the AC off
after closing. Wiring is **fail-safe NC**: the relay sits **de-energized while
the AC is allowed (open hours)** so the AC has power, and it **energizes to cut
the AC** during off hours. If the controller loses power, the relay
de-energizes and the AC keeps working. Drive a **contactor rated for the
compressor's inrush** (units are 1.5–2 ton), not a bare PCB relay — see
[`docs/PINOUT.md`](docs/PINOUT.md).

An admin sets, per device, from the backend (Admin → Devices → **Relay**):

- **AC-allowed open hours** — weekly windows (e.g. 09:00→02:00, or 11:00→23:00);
  the relay cuts power *outside* them.
- **Compressor watt threshold** — below it the compressor is considered off.
- **Grace minutes** — how long to wait for the compressor to cycle off before
  cutting.

The firmware caches all three in NVS and enforces cutoff from its local clock
even during a Wi-Fi outage. Crucially it **never opens the relay while the
compressor is drawing load**: at closing it waits for the compressor to cycle
off (wattage drops below the threshold) and then cuts, hard-cutting only at the
grace deadline if it never idles. If the AC is already idle at closing it never
cuts. No open-hours schedule cached = relay stays de-energized (AC on), the
fail-safe for an unconfigured device.

The firmware reports its relay state (energized/cut + mode) on every
`ingest.php` POST, so the admin table shows a **live indicator** per meter, and
exposes the fuller state (state-machine phase + latest wattage) over the BLE
relay characteristic. The logged wattage rows are what you tune the
threshold/grace against after observing real data.

The **Android app** can also take the relay over BLE from the device detail
screen: **On** (energize = cut) / **Off** (de-energize = AC on) force it
regardless of schedule, and **Auto** returns it to cutoff control. The override
lives in RAM, so a power-cycle returns the meter to its server config.

## Repository layout

```
esp32.supermini_ds1307_ac_energy_meter/        ESP32-C3 Super Mini firmware (DS1307, no OLED)
esp32.WROOM.DevKit.V1_SSD1306_ds3231_ac_energy_meter/  ESP32 WROOM DevKit V1 firmware (DS3231 + SSD1306 OLED)
backend/                                       MilesWeb PHP + MySQL (planned, not yet built)
android/                                       Companion app (planned, not yet built)
docs/                                          Wiring, provisioning, future hardware notes
tools/                                          Bench-test helpers (fake_ingest.py)
```

Both firmware folders are self-contained Arduino sketches for the same
meter; pick the one that matches your hardware. The `.supermini` build
targets the ESP32-C3 Super Mini with a DS1307 RTC and no display; the
`.WROOM.DevKit.V1` build targets the ESP32 WROOM DevKit V1 with a DS3231
RTC and an SSD1306 OLED. They are kept separate, not merged.

## How fast does data reach the cloud?

The firmware aims for **seconds, not minutes**, when Wi-Fi is reachable.
Once a 15-minute row is appended to flash, the sampling task immediately
asks the connectivity task to run a Wi-Fi cycle; the next 1-second tick
picks it up, connects, NTP-syncs, POSTs, and (on ACK) truncates the row
from flash. The 2-minute periodic scan still runs as a fallback for when
the AP is out of range.

If neither the DS1307 nor NTP gave the device a wall clock, the firmware
still POSTs (`sync_wall_time` is empty); MilesWeb is expected to return a
`server_time` ISO 8601 string in the response. The firmware uses that to
seed its clock and the RTC, so subsequent rows carry real timestamps.

## Status

This branch delivers **firmware Stages 1–7** per the project spec:

| Stage | Scope | Status |
|---|---|---|
| 1 | Board bring-up | ✅ |
| 2 | PZEM bring-up | ✅ |
| 3 | PZEM integration | ✅ |
| 4 | LittleFS + NVS + boot history | ✅ |
| 5 | Backend (MilesWeb) | ⏳ next session |
| 6 | Wi-Fi sync end-to-end | ✅ (testable against `tools/fake_ingest.py`) |
| 7 | BLE GATT service | ✅ (testable with nRF Connect) |
| 8 | Android companion app | ⏳ next session |
| 9 | Backend dashboard | ⏳ next session |
| 10 | AC install | hardware, out of code scope |

## Building the firmware

See [`docs/PROVISIONING.md`](docs/PROVISIONING.md) for the full step-by-step.
Quick path:

1. Arduino IDE 2.x with libraries: PZEM-004T-v30 (mandulaj),
   ArduinoJson, NimBLE-Arduino, RTClib.
2. Board: **ESP32C3 Dev Module**, **USB CDC On Boot: Enabled**, partition
   scheme: **No OTA (2MB APP/2MB SPIFFS)** (see `docs/PROVISIONING.md`).
3. Open `esp32.supermini_ds1307_ac_energy_meter/ac_energy_meter.ino` and Upload.

## Bench-testing without a backend

Run the stub:

```bash
python3 tools/fake_ingest.py --port 8080
```

Point `INGEST_URL` in `esp32.supermini_ds1307_ac_energy_meter/config.h` at
`http://<laptop-ip>:8080/ingest`, reflash, and the device will exercise its
full sync path against the stub.

## Architecture quick reference

- **Two FreeRTOS tasks** sharing the ESP32-C3's single core (both pinned to
  core 0; the higher-priority sampler preempts the connectivity task):
  - sampling: 1 Hz PZEM read. Every 15 min appends one row to `/log.csv`
    and advances `last_seq`.
  - connectivity: BLE GATT always advertising. Every 2 min attempts the
    Wi-Fi cycle (scan → connect → NTP → POST → ACK truncate).
- **Single mutex** protects the small shared-state POD; readers snapshot
  a value copy and release before doing any I/O.
- **Monotonic-µs clock** for energy integration (`esp_timer_get_time()`);
  wall-clock is only used for log timestamps and midnight rollover.
- **DS1307 RTC** on I²C (GPIO 5/6, 100 kHz) seeds wall-clock at boot so
  "Today: X kWh" totals track from the first second. NTP corrections are
  written back to the chip so it stays accurate across power loss. If the
  chip is absent or reports lost-power, NTP and BLE Set-Wall-Time still work
  as before.
- **NVS** uses two namespaces (`cfg` for Wi-Fi credentials, `state` for
  boot id / seq HWM / boot history) and a high-water-mark scheme that
  persists every 10 seqs to limit flash wear at the cost of small
  monotonic gaps across crashes.
- **`/log.csv`** is append-only with crash recovery on boot (deletes any
  leftover `/log.tmp`, validates and trims the last line). Sync uses
  snapshot-and-rewrite: capture max seq from RAM, send rows ≤ that, on
  ACK rewrite the file keeping only rows > that.
- **Buffer full**: when LittleFS free space drops below max(150 KB, 10 %),
  the firmware stops logging and flags the condition in shared state
  (`buffer_full`), surfaced over BLE Device Info.

See [`docs/PROVISIONING.md`](docs/PROVISIONING.md) for security TODOs
(no BLE bonding, no cert pinning, no HMAC) deferred to future hardening.
