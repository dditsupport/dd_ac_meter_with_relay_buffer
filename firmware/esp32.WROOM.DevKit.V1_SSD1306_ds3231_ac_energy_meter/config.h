#pragma once

// ---------- Identity / build ----------
// 2.0.0 is a breaking release: the relay schedule now means AC-ALLOWED open
// hours (energize = cut OUTSIDE the window) — the inverse of 1.x, which
// energized INSIDE the window. It also adds compressor-aware, fail-safe NC
// cutoff (see the Relay output section). The backend uses this version to
// warn about 1.x devices still running the old, inverted convention.
#define FW_VERSION              "2.0.0"

// ---------- Backend ----------
// The ingest endpoint URL is split into two parts:
//   INGEST_HOST_DEFAULT - scheme + host + optional port, e.g. "https://aromen.biz"
//                         Stored in NVS and configurable at runtime via BLE
//                         (Server Config characteristic). This default is only
//                         used if NVS has not been written.
//   INGEST_PATH         - the path component, hardcoded in firmware. The
//                         backend is expected to keep this stable.
// Full URL = NVS host (or INGEST_HOST_DEFAULT) + INGEST_PATH.
//
// To switch backend hostnames at runtime, write {"host":"https://newdomain.com"}
// from the companion app — no reflash needed.
#define INGEST_HOST_DEFAULT     "https://aromen.biz"
#define INGEST_PATH             "/meter/api/ingest.php"
#define DEVICE_TOKEN            "hs2AfGYZqZSFbb_rp-t3zy_I_rXb5TJISpn6Okih4pg"

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
#define DISPLAY_REFRESH_MS       1000     // 1 Hz OLED refresh & PZEM sample
#define WIFI_SCAN_INTERVAL_SEC  120       // 2 minutes between Wi-Fi cycles
#define NTP_SYNC_TIMEOUT_MS     5000
#define NTP_RESYNC_INTERVAL_SEC 3600      // re-hit the NTP server at most every 1 h
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS         10000

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

// ---------- Demo mode ----------
// Set to 1 to bypass the real PZEM and feed the rest of the firmware
// synthetic (but plausible) readings. Lets you bench-test the OLED,
// LittleFS logging, Wi-Fi sync, and BLE characteristics without having
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
#define PIN_PZEM_RX             16        // ESP32 RX2 <- PZEM TX
#define PIN_PZEM_TX             17        // ESP32 TX2 -> PZEM RX
#define PZEM_BAUD               9600

#define PIN_OLED_SCK            23
#define PIN_OLED_MOSI           22
#define PIN_OLED_RST            21
#define PIN_OLED_DC             19
#define PIN_OLED_CS             18        // moved off GPIO 5 (strapping pin)

#define PIN_I2C_SDA             4         // DS3231 SDA
#define PIN_I2C_SCL             15        // DS3231 SCL — GPIO 15 is a strapping pin but idles HIGH (I2C pull-ups), so boot is unaffected
#define I2C_FREQ_HZ             400000    // DS3231 supports up to 400 kHz
#define RTC_WRITEBACK_DRIFT_SEC 2         // skip RTC writeback if NTP within this

// ---------- Status LED ----------
// Wi-Fi activity indicator. GPIO 2 is the on-board LED on most ESP32 dev
// boards (it was freed when OLED_RST moved to GPIO 19). Set ACTIVE_HIGH to 0
// if your board's LED is wired active-low.
#define PIN_STATUS_LED          2
#define LED_ACTIVE_HIGH         1
#define LED_BLINK_SEARCH_MS     150       // toggle period while Wi-Fi disconnected
#define LED_BLINK_TX_MS         60        // toggle period during a data POST
#define LED_TX_PULSE_MS         800       // how long the TX flicker lasts per POST

// ---------- Relay output (off-hours AC cutoff) ----------
// Fail-safe NC wiring: the relay sits DE-ENERGIZED during open hours so the
// AC has power; to CUT the AC we ENERGIZE it (opening the NC contact). So
// "energize" == "cut AC", and a dead controller leaves the AC powered.
// Drive a contactor rated for the compressor's inrush (1.5-2 ton LRA), not a
// bare PCB relay. Active-high drives the coil on; set RELAY_ACTIVE_HIGH 0 for
// inverted-input modules.
//
// The AC-allowed "open hours" schedule + the two knobs below are pushed by the
// server per device (relay_schedule / relay_compressor_watts / relay_grace_min)
// and cached in NVS so cutoff keeps working through a Wi-Fi outage. The values
// here are only defaults until the server pushes real ones.
#define PIN_RELAY               26
#define RELAY_ACTIVE_HIGH       1

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
#define CONN_TASK_STACK         12288
#define SAMPLING_TASK_PRIO      3
#define CONN_TASK_PRIO          2
