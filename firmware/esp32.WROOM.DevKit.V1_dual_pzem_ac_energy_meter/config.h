#pragma once

// ---------- Identity / build ----------
// 3.0.0 is the DUAL-PZEM build: one ESP32 reads PZEM_CHANNELS meters and
// reports all of them under a single device_id. It is a breaking release
// because every reading now carries a 1-based `ch`:
//   * log rows are "seq,ch,boot_id,sec,V,I,P,Wh,PF,Hz" (channel is new, and
//     one sampling instant writes one row per channel, sharing `seq`);
//   * the ingest POST tags each reading with "ch" and the device with
//     "channel_count", so the server keys readings on (device_id, seq, ch).
// It carries forward 2.0.0's AC-ALLOWED open-hours relay convention
// (energize = cut OUTSIDE the window) and compressor-aware fail-safe NC cutoff.
#define FW_VERSION              "3.0.0"

// ---------- Backend ----------
// The ingest endpoint URL is split into two parts:
//   INGEST_HOST_DEFAULT - scheme + host + optional port, e.g. "https://ac.aromen.biz"
//                         Stored in NVS and configurable at runtime via BLE
//                         (Server Config characteristic). This default is only
//                         used if NVS has not been written.
//   INGEST_PATH         - the path component, hardcoded in firmware. The
//                         backend is expected to keep this stable.
// Full URL = NVS host (or INGEST_HOST_DEFAULT) + INGEST_PATH.
//
// To switch backend hostnames at runtime, write {"host":"https://newdomain.com"}
// from the companion app — no reflash needed.
#define INGEST_HOST_DEFAULT     "https://ac.aromen.biz"
#define INGEST_PATH             "/api/ingest.php"
#define DEVICE_TOKEN            "token"

// ---------- Wi-Fi (optional bench-test fallback) ----------
// If non-empty, the firmware writes these to NVS at boot whenever the saved
// list is empty. Useful when you don't yet have the companion app running and
// just want to bring the device online for testing. Leave both empty to force
// BLE-only provisioning (the production flow).
#define WIFI_SSID               "TP-Link_Second_Floor"
#define WIFI_PASSWORD           "1234567890"

// ---------- Timing ----------
// LOG_INTERVAL_SEC_DEFAULT is the cadence used when the server has not (yet)
// pushed a different value via the ingest.php response. The runtime value
// lives in NVS and is settable from the server: each POST response may
// include {"log_interval_sec": N}, and the firmware will use N until told
// otherwise.
//
// Deployed default: 300 (5 minutes) — matches the per-device interval set
// from the server. The server can still override it at runtime via the
// ingest.php response ({"log_interval_sec": N}).
// Sanity bounds enforced in storage::set_log_interval_sec(): 60..86400.
#define LOG_INTERVAL_SEC_DEFAULT 300
#define LOG_INTERVAL_SEC_MIN     60
#define LOG_INTERVAL_SEC_MAX     86400
#define SAMPLE_INTERVAL_MS       1000     // 1 Hz PZEM sample cadence
#define WIFI_SCAN_INTERVAL_SEC  120       // 2 minutes between Wi-Fi cycles
#define NTP_SYNC_TIMEOUT_MS     5000
#define NTP_RESYNC_INTERVAL_SEC 3600      // re-hit the NTP server at most every 1 h
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS         10000
// TLS handshake cap (seconds). WiFiClientSecure defaults to 120 s, so on a
// marginal link a stalled handshake blocks the connectivity task far past the
// ~30 s task watchdog, which then aborts and reboots the chip (seen on the
// Super Mini as a recurring `task_wdt: conn` panic). Capping it well under the
// WDT lets a bad handshake fail fast so post_batch() just retries next cycle.
#define TLS_HANDSHAKE_TIMEOUT_S 12

// Wi-Fi TX power, in quarter-dBm units — the ESP32 wifi_power_t enum values are
// exactly dBm*4 (19.5 dBm = 78, 13 dBm = 52, 11 dBm = 44). Kept as a plain
// number (NOT the WIFI_POWER_* enum) so files that don't include WiFi.h (e.g.
// storage.cpp) can use it as the NVS fallback.
//
// The WROOM DevKit is externally regulated and has a proper antenna, so it runs
// at FULL power — no throttling needed. (The Super Mini has to be capped to ~11
// dBm because its weak onboard LDO sags mid-TX-burst.) This is only the
// fallback; the live value lives in NVS and is settable from the app.
#define WIFI_TX_POWER_QDBM      78   // 19.5 dBm — full power

// ---------- BLE / Wi-Fi radio sharing (WROOM: none needed) ----------
// The dual-core WROOM has no BLE/Wi-Fi coexistence problem, so BOTH of the
// single-radio workarounds used on the ESP32-C3 Super Mini are DISABLED here:
//
//   WIFI_PAUSE_BLE_DURING_SYNC 0 — do NOT stop advertising during a sync; BLE
//     stays connectable throughout, so the app can talk to the device at any
//     time (on the C3 this had to be 1 to avoid a coexistence crash).
//   BLE_CONFIG_WINDOW_SEC      0 — no BLE->Wi-Fi handoff; BLE runs for the
//     WHOLE session alongside Wi-Fi instead of only a 2-minute config window
//     followed by ble_service::shutdown() (on the C3 this had to be 120).
//
// Set them non-zero only if a specific board turns out to need throttling.
#define WIFI_PAUSE_BLE_DURING_SYNC 0
#define BLE_CONFIG_WINDOW_SEC      0

// ---------- Heap guard (TLS POST) ----------
// A TLS handshake needs a large contiguous allocation for mbedTLS's buffers. If
// free memory — or the largest free block — has dropped too low, post_batch()
// defers the POST (rows stay buffered, retried next cycle) rather than risking
// an OOM-time hard fault. Both numbers are logged after every POST so they can
// be tuned to what this board actually runs at.
#define WIFI_MIN_FREE_HEAP_BYTES     45000
#define WIFI_MIN_LARGEST_BLOCK_BYTES 40000

// ---------- ROM / panic log visibility ----------
// log_serial::init() can silence ets_printf / ROM putchar output to keep the
// console clean of the Wi-Fi PHY's high-bit garbage. But that same path carries
// the panic reason line ("CORRUPT HEAP: ...", "assert failed ...", "Guru
// Meditation ..."), so silencing it hides *why* a crash happened. 1 = quiet
// production console; set to 0 while diagnosing a crash to see the reason.
#define ROM_LOG_QUIET           1

// Heartbeat: even when /log.csv is empty, force a POST at least this often so
// the server can push log_interval_sec / server_time / future config knobs.
// Also fires once on first Wi-Fi cycle after boot, so a fresh device picks
// up server-side cadence within seconds of getting online.
#define CONFIG_HEARTBEAT_SEC    3600

// ---------- Storage ----------
#define SYNC_BATCH_SIZE         100       // rows per POST
#define MAX_BOOT_HISTORY        32        // circular buffer entries
#define MAX_WIFI_CREDS          1         // only one network at a time
#define SEQ_HWM_STRIDE          10        // NVS write batching for last_seq
#define BUFFER_FREE_MIN_BYTES   (150 * 1024UL)
#define BUFFER_FREE_MIN_PCT     10        // also keep >= 10% free
#define MAX_SCAN_RESULTS        12        // top-N APs returned over BLE

// ---------- Fault thresholds ----------
#define PZEM_FAIL_THRESHOLD     3         // consecutive Modbus fails -> PZEM ERROR
#define SENSOR_LOW_V_THRESHOLD  50.0f     // V < this for SENSOR_FAULT_WINDOW = SENSOR? fault
#define SENSOR_FAULT_WINDOW_SEC 60
// Per-sample PZEM read/reset retries. A single Modbus transaction occasionally
// misses (bus noise, marginal levels/grounding), which flapped the status
// OK<->STALE and made the energy reset need several taps. Retry a couple times
// before declaring failure. The delay must exceed the library's ~200 ms value
// cache so each retry forces a fresh transaction.
#define PZEM_READ_ATTEMPTS      3
#define PZEM_READ_RETRY_MS      250

// ---------- Demo mode ----------
// Set to 1 to bypass the real PZEM and feed the rest of the firmware
// synthetic (but plausible) readings. Lets you bench-test the LittleFS
// logging, Wi-Fi sync, and BLE characteristics without having
// the PZEM physically wired. Leave at 0 for production / real measurements.
#define PZEM_DEMO_MODE          0

// ---------- Boot-loop guard ----------
#define BOOTLOOP_WINDOW_SEC     60
#define BOOTLOOP_THRESHOLD      5         // boots inside the window -> BLE-only mode

// "Stuck" watchdogs. Independent of the task WDT (which catches frozen
// tasks within 30 s) — these catch the subtler failure modes where every
// task keeps running but the radio is silently dead.
//   STUCK_WIFI: time since last successful ingest POST. Only trips after
//               at least one successful POST has ever happened — so a
//               brand-new device with no Wi-Fi credentials won't reboot.
//   STUCK_BLE:  time since BLE was last 'alive' (advertising or connected).
//               Trips only if NimBLE wedged so badly that advertising stops.
#define STUCK_WIFI_REBOOT_SEC   21600     // 6 h
#define STUCK_BLE_REBOOT_SEC    43200     // 12 h

// ---------- Pin map (ESP32 DevKit V1) ----------
// PIN RULE: use only the pins broken out on the LEFT and RIGHT headers. Never
// use GPIO 6..11 — those are the bottom-row pins wired to the module's internal
// SPI flash (CLK/CMD/SD0..SD3). Touching them bricks flash access and the chip
// won't boot. On the 38-pin DevKit they are physically present on the bottom
// row; treat that whole row as reserved.
//
// Every pin below is on a side header and outside 6..11. Strapping pins in use
// are noted individually (GPIO 0, 2, 15) — each is safe in its wiring here.
// ---- PZEM channels (dual-meter build) ----------------------------------------
// Two PZEM-004T v3.0 meters on ONE ESP32, each on its own hardware UART. The
// WROOM has three UARTs: UART0 is the USB console, leaving UART2 for channel 1
// and UART1 for channel 2.
//
// Why separate UARTs rather than one shared bus: the PZEM-004T v3.0 **TTL**
// variant drives its TX line permanently (it is not a tri-stated RS485
// multi-drop transceiver), so hanging two of them off one UART makes their TX
// lines fight and corrupts every reply. One UART per meter avoids that entirely
// and needs no Modbus re-addressing — both meters can keep the factory address.
//
// If you later use the **RS485** variant on a proper multi-drop bus, you can put
// both channels on the same UART/pins and give them distinct Modbus addresses
// via PZEM*_ADDR below; the driver already keys each channel on its address.
#define PZEM_CHANNELS           2

#define PIN_PZEM1_RX            16        // ESP32 RX2 <- PZEM-1 TX
#define PIN_PZEM1_TX            17        // ESP32 TX2 -> PZEM-1 RX
#define PZEM1_ADDR              0xF8      // PZEM factory default address

// Channel 2 on UART1. GPIO 32/33 are plain side-header pins: not strapping
// pins, not the GPIO 6..11 SPI-flash block, and both input+output capable.
#define PIN_PZEM2_RX            32        // ESP32 RX1 <- PZEM-2 TX
#define PIN_PZEM2_TX            33        // ESP32 TX1 -> PZEM-2 RX
#define PZEM2_ADDR              0xF8      // same default addr is fine on its own UART

#define PZEM_BAUD               9600


#define PIN_I2C_SDA             4         // DS1307 SDA
#define PIN_I2C_SCL             15        // DS1307 SCL — GPIO 15 is a strapping pin but idles HIGH (I2C pull-ups), so boot is unaffected
// The DS1307 is a STANDARD-MODE part: 100 kHz max. (The DS3231 this build used
// to carry tolerated 400 kHz; running the DS1307 that fast is out of spec.)
#define I2C_FREQ_HZ             100000    // DS1307 is a standard-mode (100 kHz) part
#define RTC_WRITEBACK_DRIFT_SEC 2         // skip RTC writeback if NTP within this

// ---------- Status LED ----------
// Wi-Fi activity indicator. GPIO 2 is the on-board LED on most ESP32 dev
// boards. Set ACTIVE_HIGH to 0
// if your board's LED is wired active-low.
#define PIN_STATUS_LED          2
#define LED_ACTIVE_HIGH         1
#define LED_BLINK_SEARCH_MS     150       // toggle period while Wi-Fi disconnected
#define LED_BLINK_TX_MS         60        // toggle period during a data POST
#define LED_TX_PULSE_MS         800       // how long the TX flicker lasts per POST

// ---------- Coin-cell (RTC backup) voltage sense ----------
// Averaged ADC read of the CR2032 coin cell that backs up the DS1307 RTC,
// reported on each ingest POST as `coincell_mv` (millivolts) alongside
// wifi_rssi and rtc_drift so a dying cell can be spotted before the clock is
// lost. GPIO35 is an input-only ADC1 channel (ADC1_CH7) on the WROOM DevKit V1
// — it can't drive anything, so it's an ideal pure sense line, and being on
// ADC1 means Wi-Fi doesn't disturb the reading. A fresh CR2032 is ~3.0 V and
// its end-of-life is ~2.0 V, so it stays within the ~0–3.1 V (ADC_11db)
// full-scale range and needs NO divider (ratio 1.0). If you ever sense a
// higher node through a divider, set COINCELL_DIVIDER_RATIO to
// (R_top + R_bottom) / R_bottom so the reported millivolts stay cell-referred.
#define PIN_COINCELL_ADC        35
#define COINCELL_DIVIDER_RATIO  1.0f
#define COINCELL_ADC_SAMPLES    16

// ---------- Factory reset button ----------
// Long-press the on-board BOOT button (GPIO 0) to zero the PZEM energy register
// — intended for a fresh install only. GPIO 0 is a strapping pin, so it must be
// HIGH at power-on (it is, via the board pull-up); we only read it after boot.
// Held LOW (pressed) for this long triggers the reset.
#define PIN_BOOT_BUTTON         0
#define FACTORY_RESET_HOLD_MS   5000

// ---------- Relay output (off-hours AC cutoff) ----------
// Fail-safe NC wiring: the relay sits DE-ENERGIZED during open hours so the
// AC has power; to CUT the AC we ENERGIZE it (opening the NC contact). So
// "energize" == "cut AC", and a dead controller leaves the AC powered.
// Drive a contactor rated for the compressor's inrush (1.5-2 ton LRA), not a
// bare PCB relay. The relay is driven through a PC817 optocoupler (inverted
// input / active-low): the pin idles HIGH to keep the relay DE-ENERGIZED
// (AC on) and is driven LOW to ENERGIZE the coil (cut AC), so RELAY_ACTIVE_HIGH
// is 0. Set it to 1 for a board whose coil drives on a HIGH input.
//
// ONE RELAY PER PZEM. Relay N cuts the AC that PZEM N measures, and runs its
// own independent schedule, cutoff state machine and manual override — so the
// compressor-idle check for relay N looks only at channel N's wattage. That
// pairing is the whole point: cutting a circuit based on another meter's load
// would either hard-cut a running compressor or never cut at all.
//
// GPIO 26/27 are plain side-header pins — not strapping pins, and clear of the
// GPIO 6..11 SPI-flash block.
//
// The AC-allowed "open hours" schedule + the two knobs below are pushed by the
// server PER CHANNEL (relay_channels[] in the ingest response) and cached in
// NVS so cutoff keeps working through a Wi-Fi outage. The values here are only
// defaults until the server pushes real ones.
#define RELAY_COUNT             PZEM_CHANNELS   // one relay per meter
#define PIN_RELAY1              26              // cuts the AC measured by PZEM 1
#define PIN_RELAY2              27              // cuts the AC measured by PZEM 2
#define RELAY_ACTIVE_HIGH       0

// Compressor "is-running" watt threshold: below it the compressor is off, so
// cutting is safe. While wattage stays at/above it the cut is deferred (waiting
// for the compressor to cycle off) until the grace deadline.
#define RELAY_COMPRESSOR_WATTS_DEFAULT  800     // ~1.5-2 ton compressor floor
#define RELAY_COMPRESSOR_WATTS_MIN      100
#define RELAY_COMPRESSOR_WATTS_MAX      10000
// Grace window after off-hours begin: wait up to this long for the compressor
// to cycle off before cutting; if it never idles, cut anyway at the deadline.
#define RELAY_GRACE_MIN_DEFAULT         60      // minutes
#define RELAY_GRACE_MIN_MIN             1
#define RELAY_GRACE_MIN_MAX             240

// ---------- Time ----------
#define TZ_INFO                 "IST-5:30"   // POSIX TZ, used by setenv()
#define NTP_SERVER_1            "time.google.com"
#define NTP_SERVER_2            "time.cloudflare.com"

// ---------- BLE UUIDs (generated once, do not change) ----------
#define BLE_SERVICE_UUID        "5f12b3bc-8ef3-4b48-a971-f70a38f519ec"
#define BLE_UUID_DEVICE_INFO    "56c4fe7d-1c7d-4042-9547-6170ec5c243c"
#define BLE_UUID_SET_WALL_TIME  "b90e068f-8856-4cba-a043-841081fbd1a1"
#define BLE_UUID_BOOT_HISTORY   "d155756b-566e-4aa3-9fe5-c898f78fda8b"
#define BLE_UUID_DATA_STREAM    "1199716e-692b-4d47-bd00-72792988364d"
#define BLE_UUID_SYNC_ACK       "a4b32253-c2e3-42e8-93c3-a008325540b6"
#define BLE_UUID_WIFI_CONFIG    "41310027-c18e-4452-a50e-861e77cf2743"
#define BLE_UUID_WIFI_STATUS    "28c3fa43-a1b5-4e0e-a51c-a1e979609d28"
#define BLE_UUID_WIFI_SCAN      "d4346c1c-6e36-4a0f-a164-84cd396a4697"
#define BLE_UUID_SERVER_CONFIG  "9478f8ff-cb2f-4447-8a2f-49791de6bc09"
#define BLE_UUID_RELAY          "8c5a2e91-6f3d-4b27-9a1c-0e7d3f8b6a52"
// Write-only command to zero the PZEM cumulative energy register (fresh
// install). Requires BLE auth and an explicit confirmation payload.
#define BLE_UUID_PZEM_RESET     "b7e6a1d4-3c2f-4e88-9a5b-6d0f21c8e743"

// Write-only command to factory-reset the device: wipe the entire NVS partition
// (Wi-Fi creds, backend host, log interval, boot/seq counters, relay schedule,
// boot-loop history) and clear the buffered readings, then reboot so it comes up
// as a fresh, unprovisioned device. Requires BLE auth and a confirmation payload.
#define BLE_UUID_FACTORY_RESET  "c1a9f2e5-4b6d-4c8a-9e21-7f3b0d5a8c64"

// ---------- BLE access auth (HMAC-SHA256 challenge/response) ----------
// The app must prove it knows BLE_PSK before any other characteristic is
// usable, which keeps generic BLE tools (e.g. nRF Connect) out. The key is
// NEVER sent over BLE: the device issues a per-connection random nonce on the
// Challenge characteristic, the app writes HMAC_SHA256(BLE_PSK, nonce) to the
// Response characteristic, and the firmware verifies it. BLE_PSK is a static
// compile-time secret (like DEVICE_TOKEN) so auth works even with no internet.
// The Android app's BuildConfig.BLE_PSK MUST match this string exactly.
#define BLE_UUID_AUTH_CHALLENGE "4eadfb98-7a40-4aa1-b65c-92c461d02527"
#define BLE_UUID_AUTH_RESPONSE  "0ce9edf3-d11d-4e84-9f55-fe5e943594d9"
#define BLE_PSK                 "49705412b4105495af9b3d25974605ebde7bd3fe0525d510f15c15ea75baef3c"

// ---------- Files ----------
#define LOG_PATH                "/log.csv"
#define LOG_TMP_PATH            "/log.tmp"

// ---------- Task config ----------
#define SAMPLING_TASK_STACK     6144
// post_batch()'s 16 KB StaticJsonDocument now lives in .bss (file-scope static),
// not on this stack, so the task only has to hold the mbedTLS handshake + call
// frames. 32 KB stays comfortably above that with margin.
#define CONN_TASK_STACK         32768
#define SAMPLING_TASK_PRIO      3
#define CONN_TASK_PRIO          2
