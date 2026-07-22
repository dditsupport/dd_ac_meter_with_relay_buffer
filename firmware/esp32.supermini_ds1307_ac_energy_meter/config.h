#pragma once

// ---------- Identity / build ----------
// 2.0.0 is a breaking release: the relay schedule now means AC-ALLOWED open
// hours (energize = cut OUTSIDE the window) — the inverse of 1.x, which
// energized INSIDE the window. It also retargets the hardware to the
// ESP32-C3 Super Mini (DS1307, no OLED). The backend uses this version to
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
// ~30 s task watchdog, which then aborts and reboots the chip (seen as the
// recurring `task_wdt: conn` panic). Capping it well under the WDT lets a bad
// handshake fail fast so post_batch() just retries next cycle instead.
#define TLS_HANDSHAKE_TIMEOUT_S 12

// Wi-Fi TX power cap. The Super Mini's onboard LDO (~250 mA) can't sustain the
// radio's ~335 mA peak at full power (19.5 dBm); the 3V3 rail sags during a TX
// burst and corrupts the 802.11 auth frames, so association fails with
// disconnect reason 2 (AUTH_EXPIRE). Field testing (incl. an external 1 A
// regulator) showed 15 dBm still fails while BLE runs concurrently but 11 dBm
// is stable, so 11 dBm is the default. Applied via WiFi.setTxPower() after
// EVERY WiFi.begin() (the driver resets it on reconnect). Raise it only with a
// stiff 3V3 supply AND if BLE is quieted during sync (see below).
#define WIFI_TX_POWER           WIFI_POWER_11dBm

// Pause BLE advertising for the duration of each Wi-Fi cycle (connect + NTP +
// TLS POST). On the single-core ESP32-C3, BLE advertising sharing the radio
// with a Wi-Fi TLS upload trips a coexistence crash (the panic always faults at
// the same PC). Pausing advertising lets Wi-Fi own the radio for the upload,
// then advertising resumes. It's a no-op while a phone is actively connected
// over BLE (NimBLE has already stopped advertising then), so live config isn't
// disrupted — it only quiets the autonomous case. Set to 0 to keep BLE fully
// concurrent (safe on a stiff supply, or the dual-core WROOM).
#define WIFI_PAUSE_BLE_DURING_SYNC 1

// BLE→Wi-Fi radio handoff. On the single-core ESP32-C3 the Wi-Fi TLS sync
// crashes while the BLE controller is co-active (pausing advertising isn't
// enough — the controller itself must be off). So run BLE ONLY for the first
// BLE_CONFIG_WINDOW_SEC after boot — the provisioning/config window, with Wi-Fi
// held off — then shut BLE down completely and let Wi-Fi own the radio for the
// rest of the run. Reboot the device to get another BLE config window. Set to 0
// to disable the handoff and keep BLE + Wi-Fi concurrent (fine on the dual-core
// WROOM or with a stiff supply).
#define BLE_CONFIG_WINDOW_SEC   120

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
// Per-sample PZEM read retries. A single Modbus transaction to the PZEM-004T
// occasionally misses (radio activity on the single-core C3, marginal 5 V TTL
// levels / grounding, or bus noise), which made the status flap OK<->STALE.
// Retry the read a couple times before declaring the sample failed. The delay
// must exceed the library's ~200 ms value cache so each retry forces a fresh
// transaction rather than returning the same stale (NaN) cached read.
#define PZEM_READ_ATTEMPTS      3
#define PZEM_READ_RETRY_MS      250

// ---------- Demo mode ----------
// Set to 1 to bypass the real PZEM and feed the rest of the firmware
// synthetic (but plausible) readings. Lets you bench-test the LittleFS
// logging, Wi-Fi sync, and BLE characteristics without having the PZEM
// physically wired. Leave at 0 for production / real measurements.
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

// ---------- Pin map (ESP32-C3 Super Mini) ----------
// UART1 to the PZEM: RX/TX use the header's labeled RX/TX pins (UART0's
// default GPIOs), which are free because the console runs over native USB.
#define PIN_PZEM_RX             20        // GPIO20 (labeled RX) <- PZEM TX
#define PIN_PZEM_TX             21        // GPIO21 (labeled TX) -> PZEM RX
#define PZEM_BAUD               9600

#define PIN_I2C_SDA             6         // DS1307 SDA (GPIO6, non-strapping)
#define PIN_I2C_SCL             5         // DS1307 SCL (GPIO5, non-strapping)
#define I2C_FREQ_HZ             100000    // DS1307 is a standard-mode (100 kHz) part
#define RTC_WRITEBACK_DRIFT_SEC 2         // skip RTC writeback if NTP within this

// ---------- Status LED ----------
// Wi-Fi activity indicator. External LED (+ series resistor to GND) on GPIO7,
// a plain non-strapping pin. Set ACTIVE_HIGH to 0 if your LED is wired
// active-low. (The Super Mini's on-board GPIO8 LED is left free — GPIO8 is a
// strapping pin.)
#define PIN_STATUS_LED          7
#define LED_ACTIVE_HIGH         1
#define LED_BLINK_SEARCH_MS     150       // toggle period while Wi-Fi disconnected
#define LED_BLINK_TX_MS         60        // toggle period during a data POST
#define LED_TX_PULSE_MS         800       // how long the TX flicker lasts per POST

// ---------- Coin-cell (RTC backup) voltage sense ----------
// Averaged ADC read of the CR2032 coin cell that backs up the DS1307 RTC,
// reported on each ingest POST as `coincell_mv` (millivolts) alongside
// wifi_rssi and rtc_drift so a dying cell can be spotted before the clock is
// lost. GPIO3 = ADC1_CH3 on the ESP32-C3 (non-strapping, free); being on ADC1
// means Wi-Fi activity doesn't disturb the reading. A fresh CR2032 is ~3.0 V
// and its end-of-life is ~2.0 V, so it stays within the ~0–3.1 V (ADC_11db)
// full-scale range and needs NO divider (ratio 1.0). If you ever sense a
// higher node through a divider, set COINCELL_DIVIDER_RATIO to
// (R_top + R_bottom) / R_bottom so the reported millivolts stay cell-referred.
#define PIN_COINCELL_ADC        3
#define COINCELL_DIVIDER_RATIO  1.0f
#define COINCELL_ADC_SAMPLES    16

// ---------- Factory reset button ----------
// Long-press the on-board BOOT button (GPIO 9) to zero the PZEM energy register
// — intended for a fresh install only. GPIO 9 is the C3's download-mode
// strapping pin, so it must be HIGH at power-on (it is, via the board pull-up);
// we only read it after boot. Held LOW (pressed) for this long triggers the
// reset.
#define PIN_BOOT_BUTTON         9
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
// The AC-allowed "open hours" schedule + the two knobs below are pushed by the
// server per device (relay_schedule / relay_compressor_watts / relay_grace_min)
// and cached in NVS so cutoff keeps working through a Wi-Fi outage. The values
// here are only defaults until the server pushes real ones.
#define PIN_RELAY               10        // GPIO10, non-strapping
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

// ---------- Heap guard (TLS POST) ----------
// A TLS handshake needs a large contiguous allocation for mbedTLS's buffers. On
// a marginal link the handshake retries and churns the heap; if free memory (or
// the largest free block) has dropped too low, opening the connection can fail
// hard or corrupt the heap. So post_batch() checks these thresholds first and
// defers the POST (rows stay buffered, retried next cycle) when memory is tight,
// degrading gracefully instead of panicking. Tune from the logged values.
#define WIFI_MIN_FREE_HEAP_BYTES    45000
#define WIFI_MIN_LARGEST_BLOCK_BYTES 40000

// ---------- ROM / panic log visibility ----------
// log_serial::init() can silence ets_printf / ROM putchar output to keep the
// console clean of the Wi-Fi PHY's high-bit garbage. But that same path carries
// the panic reason line ("CORRUPT HEAP: ...", "assert failed ...", "Guru
// Meditation ..."), so silencing it hides *why* a crash happened. Set to 0 while
// diagnosing crashes to see the reason; set to 1 for a quiet production console.
#define ROM_LOG_QUIET           0
