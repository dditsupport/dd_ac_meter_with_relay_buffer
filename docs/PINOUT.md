# Pinout — ESP32-C3 Super Mini

Orientation: USB-C connector at the **top**, module facing you. The board
breaks out two rows of pins; the silkscreen labels each GPIO by number.

## Confirmed hardware

| Property | Value |
|---|---|
| Board | ESP32-C3 Super Mini |
| Chip | ESP32-C3 (32-bit RISC-V, single-core) |
| CPU clock | up to 160 MHz |
| Wireless | Wi-Fi 802.11 b/g/n + Bluetooth LE |
| USB | on-board **native USB** (USB Serial/JTAG) — programming, power, console |
| Flash | 4 MB |
| Logic level | 3.3 V (5 V in via USB / `5V` pin) |

There is **no USB-UART bridge chip** on this board — the USB port is the
ESP32-C3's own USB Serial/JTAG peripheral (internal GPIO18/19, not on the
header). The serial console therefore runs over **USB-CDC**, which must be
enabled in the Arduino board settings (see `docs/PROVISIONING.md`).

The MAC address (and therefore the firmware-derived `device_id` and BLE
advertising name `Meter-XXXXXX`) is per-board. Read it from the serial
boot output over USB: the firmware prints `Device ID: meter-xxxxxx` right
after storage init.

## Header reference

```
        Left row                         Right row
        ────────                         ─────────
        5V                               GPIO5   (A5 / MISO)
        GND                              GPIO6   (MOSI)
        3V3                              GPIO7   (SS)
        GPIO4  (A4 / SCK)                GPIO8   (SDA, on-board LED, strap)
        GPIO3  (A3)                      GPIO9   (SCL, BOOT button, strap)
        GPIO2  (A2, strap)               GPIO10
        GPIO1  (A1)                      GPIO20  (RX / U0RXD)
        GPIO0  (A0)                      GPIO21  (TX / U0TXD)
```

## Definitive pin map

| Peripheral pin | ESP32-C3 GPIO | Silkscreen | Notes |
|---|---|---|---|
| **PZEM-004T v3.0** | | | UART1 |
| TX (→ C3 RX) | GPIO20 | **20 / RX** | |
| RX (← C3 TX) | GPIO21 | **21 / TX** | |
| 5V | 5V rail | **5V** | |
| GND | GND | **G** | |
| **DS1307 RTC (I²C)** | | | 100 kHz |
| VCC | 5V rail | **5V** | DS1307 is a 4.5–5.5 V part — see caveat below |
| GND | GND | **G** | |
| SDA | GPIO6 | **6** | |
| SCL | GPIO5 | **5** | |
| SQW | — | — | leave disconnected |
| **Status LED (external)** | | | |
| Anode (→ resistor) | GPIO7 | **7** | active-high; ~330 Ω to LED, LED to GND |
| **CR2032 coin-cell sense** | | | ADC1, no divider |
| Cell + (→ sense) | GPIO3 | **3** | ADC1_CH3; CR2032 stays ≤3.1 V so wire straight, no divider |
| **AC-cutoff contactor** | | | |
| IN (control) | GPIO10 | **10** | energize = cut AC (see below) |
| VCC | 5V rail | **5V** | |
| GND | GND | **G** | |

The console (USB) needs no header pins — it rides the native USB port, which
is why the labeled `RX`/`TX` pins (GPIO20/21, the chip's UART0) are free for
the PZEM here.

### Relay / AC-cutoff wiring

- The relay is an **off-hours AC cutoff**. Wire the AC feed through the
  contactor's **NC (normally-closed)** contacts so that **de-energized = AC
  powered**. GPIO10 energizes the coil only to *cut* the AC during off hours; a
  dead controller leaves the AC running (fail-safe).
- Use a **contactor rated for the compressor's locked-rotor inrush** (1.5–2 ton
  AC), not a small PCB relay, and drive its coil via the module's isolated
  input.
- The PZEM must be powered **independently of the cut circuit** (its measured
  branch reads ~0 after a cut — the firmware latches through that; it must not
  lose its own supply).

## Power & ground

- The board is powered from the `5V` pin (or the USB-C port). An external
  5 V supply — e.g. an HLK-PM01 mains module — feeds `5V`; the on-board
  regulator provides 3V3.
- The PZEM logic side runs from the same 5 V rail (PZEM does NOT accept 3.3 V).
- The DS1307 runs from **5 V** — it is a 4.5–5.5 V part. Most DS1307 "tiny
  RTC" breakout boards expect 5 V and carry their SDA/SCL pull-ups to that
  rail. The ESP32-C3 GPIOs are **not** 5 V tolerant, so if your module pulls
  the I²C lines up to 5 V, add a level shifter (or move the pull-ups to 3V3)
  before wiring SDA/SCL to GPIO5/6.
- **Common ground is mandatory**: PZEM 5 V GND, DS1307 GND, relay GND, and
  the C3 GND must share a single rail. Without it, UART and I²C traffic is
  unreliable.

## UART / I²C assignments

- `Serial` (USB-CDC over native USB) → console / logs @ 115200.
- `Serial1` (UART1, GPIO20 RX / GPIO21 TX) → PZEM.
- `Wire` (GPIO6 SDA, GPIO5 SCL) → DS1307 at 100 kHz. The DS1307 is a
  standard-mode part and does not support 400 kHz. Add external pull-ups
  (4.7 kΩ) if your module doesn't include them — but see the 5 V caveat
  under **Power & ground**.

## Strapping-pin safety

The ESP32-C3's strapping pins are **GPIO2, GPIO8, GPIO9**. This project keeps
all of them free of signal wiring:

- **GPIO9** is the BOOT button (pulled up; hold low at reset to enter
  download mode). Left unused.
- **GPIO8** carries the board's on-board blue LED and is a strapping pin
  (must be high at boot). Left unused — the status LED is an external LED on
  GPIO7 instead.
- **GPIO2** is a strapping pin. Left unused.

## Free pins for future expansion

Unused and available (all non-strapping unless noted):

- **GPIO0, GPIO1, GPIO4** (also usable as ADC A0/A1/A4)
- **GPIO3** — used for the CR2032 coin-cell voltage sense (ADC1_CH3)
- **GPIO2, GPIO8** — usable but strapping pins; wire with care
- **GPIO9** — BOOT button

---

# Pinout — ESP32 WROOM DevKit V1 (single meter)

Applies to `firmware/esp32.WROOM.DevKit.V1_ds1307_ac_energy_meter/`,
**FW 2.2.0 and later**, on the grouped-module PCB
(`hardware/esp32_module_grouped*.dip`). Never use GPIO 6..11 (SPI flash).

> **Silkscreen error on this PCB:** the labels **32** and **35** are swapped.
> The pad printed **"35"** (pad 13) is really **GPIO32**, and the pad printed
> **"32"** (pad 12) is really **GPIO35**, which is input-only. Never route an
> output to the pad printed "32". The firmware always uses the real GPIO.

| Peripheral pin | Real GPIO | Pad | Silkscreen | Notes |
|---|---|---|---|---|
| **PZEM-004T v3.0** | | | | UART2 (`Serial2`) |
| TX (→ ESP32 RX2) | GPIO16 | 20 | 16 | |
| RX (← ESP32 TX2) | GPIO17 | 22 | 17 | |
| **DS1307 RTC (I²C)** | | | | 100 kHz; pull-ups to 3.3 V |
| SDA | GPIO13 | 18 | 13 | |
| SCL | GPIO32 | 13 | **35** (mislabelled) | |
| **CR2032 coin-cell sense** | GPIO34 | 11 | 34 | input-only ADC1_CH6; via a ½ divider (~1.5 V at the pin), `COINCELL_DIVIDER_RATIO 2.0` |
| **Status LED (external)** | GPIO33 | 14 | 33 | GPIO33 → resistor (~330 Ω) → LED → GND, active-high |
| **AC-cutoff relay** (PC817 input) | GPIO25 | 15 | 25 | active-low: LOW = energize = cut AC; no pull or pulse at boot |
| BOOT button | GPIO0 | — | on-board | strapping pin, read only after boot |

Older boards use different pins and are not compatible with FW 2.2.0 — keep
them on their own firmware or rewire them:

| FW | SDA | SCL | LED | Relay | Coin-cell |
|---|---|---|---|---|---|
| ≤ 2.0.0 | 4 | 15 | 2 | 26 | 35 (no divider) |
| 2.1.0 | 32 | 33 | 26 | 13 | 39 |

If the reported `coincell_mv` is off by a fixed factor, set
`COINCELL_DIVIDER_RATIO` in `config.h` to (R_top + R_bottom) / R_bottom for the
board's actual resistors.
