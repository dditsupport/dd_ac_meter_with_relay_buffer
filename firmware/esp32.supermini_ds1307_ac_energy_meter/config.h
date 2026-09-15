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
// NTP is a convenience here, not the time source of record: the DS1307 holds
// the clock, the Android app sets it over BLE, and the ingest response carries
// server_time as a third fallback. Over 12 h even an uncompensated DS1307 stays
// within a couple of seconds, which is nothing against a 300 s log interval. So
// resync twice a day rather than hourly — that also cuts configTzTime() calls
// from 24 a day to 2, which is worth having while the heap is under suspicion.
#define NTP_RESYNC_INTERVAL_SEC 43200     // 12 h — twice a day, after a success
// Retry sooner than that after a FAILED attempt. The freshness gate above is
// driven by the last SUCCESS, so on its own it let a device that could not
// reach an NTP server retry on every single Wi-Fi cycle. This rate-limits the
// ATTEMPT, so a device that boots without a clock is not stuck waiting half a
// day for its second try either.
#define NTP_RETRY_INTERVAL_SEC  900       // 15 min — after a failed attempt
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS         10000    // response read

// TCP connect and TLS handshake caps for the ingest POST.
//
// WDT BUDGET. health::begin() installs a 30 s task watchdog with
// trigger_panic = true. The connectivity task feeds it immediately before the
// POST, so the POST is the longest single unfed span and its three phases have
// to stay clear of 30 s:
//     TCP_CONNECT_TIMEOUT_MS   5 s
//   + TLS_HANDSHAKE_TIMEOUT_S  8 s
//   + HTTP_TIMEOUT_MS         10 s
//   = 23 s, leaving ~7 s of margin.
// The previous values (connect 10 s via HTTP_TIMEOUT_MS, handshake 12 s, read
// 10 s) summed to 32 s — OVER the watchdog — so a POST that hit all three caps
// panicked the chip instead of failing cleanly. Raising any of these three
// means re-checking that sum against the 30 s in health.cpp.
//
// Both caps are needed: WiFiClientSecure leaves the handshake at 120 s by
// default, and HTTPClient's connect timeout is separate from its response-read
// timeout. A handshake that fails fast just retries next cycle, costing one
// sync interval; one that runs long costs a reboot.
#define TCP_CONNECT_TIMEOUT_MS  5000
#define TLS_HANDSHAKE_TIMEOUT_S 8

// Wi-Fi TX power DEFAULT, in quarter-dBm units — the ESP32 wifi_power_t enum
// values are exactly dBm*4 (13 dBm = 52, 11 dBm = 44, 8.5 dBm = 34). Kept as a
// plain number (NOT the WIFI_POWER_* enum) so files that don't include WiFi.h
// (e.g. storage.cpp) can use it as the NVS fallback.
//
// Why it's capped: the Super Mini's onboard LDO can't sustain the radio's
// ~335 mA peak at full power (19.5 dBm) — the 3V3 rail sags mid-TX-burst,
// corrupts the 802.11 auth frames, and association fails with disconnect
// reason 2 (AUTH_EXPIRE). Even on a stiff external supply, RF coupling from the
// RTC/PZEM wiring near the PCB antenna lowers the usable ceiling; field testing
// settled on 13 dBm as the stable point with peripherals wired. Applied via
// WiFi.setTxPower() after EVERY WiFi.begin(). This is ONLY the fallback — the
// live value lives in NVS and is settable from the app's Configure Wi-Fi screen.
#define WIFI_TX_POWER_QDBM      44   // 11 dBm (= WIFI_POWER_11dBm)

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
// Rows per POST. Deliberately small: each row costs ~100 B of request body and
// ~10 ArduinoJson slots, and ArduinoJson 7 grows the document on the heap in
// 1 KB pools rather than reserving up front. At 100 rows a single POST churned
// ~9 KB of pool plus an ~11 KB body String through the heap every cycle — and
// that was self-worsening: once POSTs began failing the backlog grew until
// every retry hit the 100-row cap, so each attempt demanded MORE contiguous
// memory than the one that had just failed. 25 keeps the peak near 4 KB, at the
// cost of more frequent but individually cheaper POSTs.
#define SYNC_BATCH_SIZE         25        // rows per POST

// Size of the static request-body buffer in wifi_sync.cpp. Must exceed the
// largest body SYNC_BATCH_SIZE can produce (~5 KB worst case at 25 rows with a
// full MAX_BOOT_HISTORY). post_batch() measures the document first and refuses
// to POST rather than truncate into invalid JSON, so raising SYNC_BATCH_SIZE
// means raising this too. Lives in .bss: costs a fixed 8 KB of DRAM and removes
// a per-cycle heap allocation that was built through hundreds of reallocations
// — the single largest source of fragmentation in the connectivity task.
#define POST_BODY_BUF_BYTES     8192
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
//
// ---------- Stuck-Wi-Fi escalation ----------
// FIELD DATA (meter-53dcbc, 23 days): the old rule — "no successful POST for
// 6 h => the radio is wedged, reboot" — was the ONLY thing recovering this
// device, and it took six hours to do it. Thirteen of fourteen reboots in that
// window were preceded by a POST blackout of exactly 6.04 h, each ending with a
// single catch-up POST of 67-72 rows. 14.7% of that device's rows arrived late,
// mean 2.75 h.
//
// The counter now advances ONLY while WiFi.isConnected(), and resets whenever
// the link drops or a POST lands. That matters in both directions: on a device
// synced from a phone hotspot, ordinary offline time used to trip it and reboot
// a perfectly healthy unit; and because it can no longer be tripped by being
// offline, the thresholds are safe to make aggressive.
//
// Escalation, cheapest first:
//   STUCK_WIFI_REASSOC_SEC — force a radio rest, i.e. a full reassociation with
//     a fresh DHCP lease and DNS servers. A stale association that survives
//     WL_CONNECTED is the usual cause, and this costs one sync interval instead
//     of a reboot: uptime, boot_id and the buffered log all survive. 0 disables.
//   STUCK_WIFI_REBOOT_SEC  — if reassociating did not help either, reboot.
// A boot that has never posted at all is exempt from both, so a fresh or
// unprovisioned device never reboots itself.
#define STUCK_WIFI_REASSOC_SEC  600       // 10 min associated, nothing posted
#define STUCK_WIFI_REBOOT_SEC   1800      // 30 min associated, nothing posted
#define STUCK_BLE_REBOOT_SEC    43200     // 12 h

// ---------- Nightly scheduled reboot ----------
// Reboot once a day in the small hours for a clean slate: fresh heap — the one
// resource that never heals on its own, since nothing compacts a C heap — plus
// fresh Wi-Fi and BLE stacks and every counter and timer reset.
//
// Nothing is lost. Buffered rows live in LittleFS and ship on the next sync;
// boot_id, seq and the log cursor are all in NVS. The cost is a few seconds of
// downtime and the first log row of the new boot arriving one log interval late.
//
// The exact minute inside the window is derived from the device MAC, so a fleet
// spreads itself across the window instead of every unit rebooting on the same
// second and stampeding the ingest endpoint when they all come back.
//
// Fires at most once per local calendar day. The day it last fired is recorded
// in NVS, NOT in RAM — otherwise the reboot it causes would clear the flag and
// it would fire again a second later, in a loop. It also requires a trusted wall
// clock (no clock, no schedule), skips a device that booted less than
// NIGHTLY_REBOOT_MIN_UPTIME_SEC ago, and defers while a phone is connected over
// BLE or a sync is in flight, retrying each second until the window closes.
//
// DEFAULTS ONLY. The live values live in NVS and are pushed by the server on any
// ingest response (nightly_reboot_enable / nightly_reboot_start_hour /
// nightly_reboot_end_hour), so the window can be moved across a fleet without
// reflashing. These apply only until the server first says otherwise.
#define NIGHTLY_REBOOT_ENABLE_DEFAULT       1
#define NIGHTLY_REBOOT_START_HOUR_DEFAULT   4     // local time, inclusive
#define NIGHTLY_REBOOT_END_HOUR_DEFAULT     5     // local time, exclusive
// Compile-time only: an internal safety rail, not an ops knob.
#define NIGHTLY_REBOOT_MIN_UPTIME_SEC 600   // don't reboot a device that just booted

// ---------- Periodic radio rest ----------
// Every storage::radio_rest_interval_sec() the connectivity task takes the radio
// off-air for radio_rest_duration_sec(): the STA is disassociated and the Wi-Fi
// PHY powered down, and BLE advertising stopped. Both then come back and a sync
// runs immediately on the fresh link.
//
// Why: try_connect_known() reuses an existing association whenever
// WiFi.status() reports WL_CONNECTED, and run_cycle() only tears the link down
// when that function fails. So if the STA is left holding a STALE association
// after the AP restarts — routine with a phone hotspot: screen off, band
// switch, DHCP renewal, a carrier blip — every POST fails at DNS/TCP while the
// driver still reports "connected", and nothing forces a reassociation.
//
// No data is lost across a rest: the sampling task keeps writing rows to
// LittleFS throughout and they ship on the cycle that follows. The rest is
// deferred while a phone is connected over BLE, so a provisioning session is
// never cut off mid-way.
//
// An interval of 0 (the default) retires the PERIODIC TIMER ONLY; the rest
// MECHANISM stays compiled and the stuck-Wi-Fi escalation above still forces one
// on demand, which is the path that earns its keep — it reassociates only when
// the device is demonstrably associated-but-not-posting, instead of going
// off-air on a schedule on the chance something is wrong. Give this a non-zero
// interval only if field logs show stale associations the on-demand path misses.
// Both are server-pushable (radio_rest_interval_sec / radio_rest_duration_sec)
// and cached in NVS; these defaults apply only until the server first speaks.
#define RADIO_REST_INTERVAL_SEC_DEFAULT 0    // periodic rest off; on-demand still active
#define RADIO_REST_DURATION_SEC_DEFAULT 45   // seconds fully off-air (<= 60)

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
// Wi-Fi activity indicator. External LED on GPIO7, a plain non-strapping pin,
// wired ACTIVE-LOW: the anode is tied to Vcc and the cathode returns through a
// series resistor to GPIO7, so the LED lights when the pin is driven LOW. Hence
// LED_ACTIVE_HIGH is 0. Set it to 1 for the other wiring (GPIO7 -> resistor ->
// LED -> GND). (The Super Mini's on-board GPIO8 LED is left free — GPIO8 is a
// strapping pin.)
#define PIN_STATUS_LED          7
#define LED_ACTIVE_HIGH         0
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
// bare PCB relay. The relay is driven through a PC817 optocoupler wired
// NON-INVERTING / ACTIVE-HIGH: a HIGH on the pin turns the opto LED on and
// ENERGIZES the coil (cutting AC); the pin idles LOW, leaving the coil
// DE-ENERGIZED (AC on) — the fail-safe state, which also holds while the pin is
// undriven at reset. Hence RELAY_ACTIVE_HIGH is 1. Set it to 0 for a board
// whose coil energizes on a LOW input.
//
// The AC-allowed "open hours" schedule + the two knobs below are pushed by the
// server per device (relay_schedule / relay_compressor_watts / relay_grace_min)
// and cached in NVS so cutoff keeps working through a Wi-Fi outage. The values
// here are only defaults until the server pushes real ones.
#define PIN_RELAY               10        // GPIO10, non-strapping
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
// xTaskCreate takes this stack FROM THE HEAP, so every byte here is a byte the
// mbedTLS handshake cannot have — and the handshake's transient peak is 45-48 KB
// (see HEAP_MIN_FREE_BYTES). 32768 was chosen back when post_batch()'s 16 KB
// StaticJsonDocument lived on this stack; it moved to .bss long ago and the
// figure was never revisited, leaving ~20 KB committed to a task that does not
// use it.
//
// That mattered once POST_BODY_BUF_BYTES put another 8 KB of .bss beyond the
// heap's reach: at-rest free fell to ~49 KB against a 47.8 KB peak draw, the
// handshake ran the heap down to 296 bytes, and every POST failed with code=-1.
// 16384 returns 16 KB and restores ~18 KB of margin — still a third more than
// the 12288 the reference build runs the same handshake on.
//
// Raising this again means re-checking it against that peak draw, because the
// two come out of the same pool.
#define CONN_TASK_STACK         16384
#define SAMPLING_TASK_PRIO      3
#define CONN_TASK_PRIO          2

// ---------- Heap guard + heap watchdog (TLS POST) ----------
// A TLS handshake needs one large CONTIGUOUS allocation for mbedTLS's record
// buffers, which makes it the first thing here to fail as the heap fragments —
// long before TOTAL free memory looks alarming. post_batch() therefore checks
// BOTH numbers before opening the connection.
//
// Deferring RECOVERS NOTHING on its own: a C heap never compacts, so once under
// the line the device would defer every POST forever while still showing
// "Wi-Fi connected". That is why each deferral is counted and the heap watchdog
// in the connectivity task reboots at HEAP_LOW_REBOOT_CYCLES.
//
// MEASURED ON THIS BUILD (meter-2e5694, WROOM DevKit V1, 2026-09-15), which is
// the only measurement that counts. NOTE these were taken with the OLD
// CONN_TASK_STACK 32768; that is now 16384, so at-rest free should be ~16 KB
// higher (~65 KB). The largest-block and peak-draw figures are unaffected.
//     [wifi] low heap — deferring POST (free=49212 largest=47092)
//   free at rest    48716 - 49324 B   (with the 32 KB stack)
//   largest block   34804 - 47092 B
//
// WHAT A HANDSHAKE ACTUALLY COSTS, from the "min=" field (which reports
// esp_get_minimum_free_heap_size(), the all-time low-water mark) on four real
// POSTs on meter-2e5694:
//     pre=48048 ... min=296     -> transient peak draw 47752 B
//     pre=46944 ... min=408     ->                     46536 B
//     pre=46200 ... min=408     ->                     45792 B
//     pre=46876 ... min=1504    ->                     45372 B
// So a TLS handshake transiently claims 45-48 KB, an order of magnitude more
// than the ~3 KB the session holds once established (the "tls=" field). That
// transient peak, not the steady cost, is what this floor has to clear.
//
// 50000 sits ~2.2 KB above the worst observed peak. Below it the handshake is
// going to fail anyway, so deferring is the correct and cheaper answer; above
// it, at ~65 KB at rest (see CONN_TASK_STACK), there is ~15 KB of warning.
//
// Two earlier revisions got this wrong in instructive ways. 62000 came from the
// solar_monitor_ds1307 reference build (~72.1 KB at rest) — a build with
// CONN_TASK_STACK 12288 and no relay code — and sat ABOVE where this board
// idles, so the guard tripped on the first POST of every boot and the watchdog
// turned that into a ~2 minute reboot loop. 45000 then sat BELOW the handshake's
// peak draw, which is just as useless in the other direction: the guard passes
// and the handshake fails regardless. A floor only means something when it is
// above the peak draw AND below at-rest free.
//
// THE SUPER MINI IS AN ESP32-C3 and every number above came off a WROOM: single
// core, ~400 KB SRAM against the WROOM's ~520 KB. Its at-rest free heap will be
// lower, so 50000 is PROVISIONAL here until a C3 prints its own [wifi] heap
// line. If it turns out to idle below ~55 KB, cut CONN_TASK_STACK further
// before lowering this floor — the floor cannot go under the handshake's peak
// draw and still mean anything.
//
// Getting it wrong is no longer fatal either way: a boot that has never posted
// is exempt from both the deferral and the reboot (see post_batch), so a floor
// that is still too high shows up as a logged warning and a POST that happens
// anyway, not as a reboot loop.
#define HEAP_MIN_FREE_BYTES          50000
// mbedTLS needs a ~16 KB record buffer as its single largest block; 20000 is
// ~20% above that and far below the 42996 B worst case observed here, so it
// refuses a genuinely starved heap without tripping on normal fragmentation.
#define HEAP_MIN_LARGEST_BLOCK_BYTES 20000

// Consecutive heap-guard deferrals before the connectivity task reboots.
// 0 = measure and report only: post_batch() then logs the low-heap line and
// POSTS ANYWAY rather than deferring into a state nothing can recover from.
//
// Enabled on evidence: the same 618 samples showed a steady 35.2 B lost per
// POST with heap_largest pinned at 47092 throughout — a LEAK, not fragmentation
// — and one boot finishing 2136 B above the floor. The nightly reboot beat
// exhaustion by about two hours, which is luck rather than margin.
//
// Why 3: a deferral leaves the rows buffered, so the next cycle still has work
// and re-tests the guard every WIFI_SCAN_INTERVAL_SEC. Three in a row is ~6
// minutes — fast enough to act, long enough that one odd reading cannot trigger
// it. The measured decline is smooth and monotonic, so there are no transient
// dips to ride out. At a 900 s log interval the nightly reboot always gets
// there first and this never fires; at shorter bench intervals it is what stops
// the device wedging before 04:00.
#define HEAP_LOW_REBOOT_CYCLES       3

// ---------- Heap leak tracing ----------
// Breaks a Wi-Fi cycle into its phases and reports the heap delta of each
// (idle / connect / ntp / post / net), so whichever column trends negative over
// many cycles owns the leak. Single-cycle values are noisy — the sign of the
// AVERAGE is the signal. Costs three heap reads per cycle; keep it on until the
// leak above is attributed.
#define HEAP_TRACE_CYCLE        1

// Stage-by-stage heap accounting INSIDE the POST: ctor, begin, post, read, end,
// dtor, six deltas that sum to dpost. 0 — the bisection is done. It found that
// http.end() returns exactly 0 bytes; the whole TLS allocation stays held until
// the destructors run, which is why the client is explicitly scoped in
// post_batch(). Set back to 1 only if the POST needs re-investigating.
#define HEAP_TRACE_POST         0
