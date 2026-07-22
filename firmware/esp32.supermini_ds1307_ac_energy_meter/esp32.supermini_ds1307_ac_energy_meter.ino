// AC Energy Meter — ESP32-C3 firmware (v1.0.0)
//
// Sketch entry point. Sets up the brownout detector, mounts storage, then
// spawns two FreeRTOS tasks: SamplingTask (PZEM) and ConnectivityTask (BLE
// always, Wi-Fi periodic). The ESP32-C3 is single-core, so both run on core 0
// and cooperatively share it via priorities and vTaskDelay. All shared mutable
// state lives in g_state, guarded by g_state_mutex.
//
// See docs/PINOUT.md for wiring and docs/PROVISIONING.md for setup steps.

#include "config.h"
#include "shared_state.h"
#include "identity.h"
#include "time_source.h"
#include "pzem.h"
#include "storage.h"
#include "health.h"
#include "wifi_sync.h"
#include "ble_service.h"
#include "led.h"
#include "relay.h"
#include "rtc.h"

#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_mac.h>
#include "log_serial.h"

// ---- Global shared state ----------------------------------------------------
SharedState g_state;
SemaphoreHandle_t g_state_mutex;

// Set by loop() on a BOOT-button long-press; consumed by sampling_task, which
// owns all PZEM/Modbus access, so the reset never races a concurrent read.
static volatile bool g_energy_reset_req = false;

// ---- Forward declarations ---------------------------------------------------
static void sampling_task(void *);
static void connectivity_task(void *);
static void handle_serial_command(const String &cmd);

extern "C" void health_mark_clean_uptime();

// ---- Setup ------------------------------------------------------------------
void setup() {
  // The brownout detector is enabled by default in ESP-IDF 5.x at the chip
  // default threshold (~2.4 V). Override via menuconfig / sdkconfig if needed.

  Serial.begin(115200);
  log_serial::init();
  delay(50);
  LOG_PRINTLN();
  LOG_PRINTLN("=== AC Energy Meter boot ===");

  g_state_mutex = xSemaphoreCreateMutex();
  if (!g_state_mutex) {
    LOG_PRINTLN("[fatal] state mutex alloc failed");
    while (true) delay(1000);
  }

  time_source::begin();
  health::begin();
  if (!storage::begin()) {
    LOG_PRINTLN("[fatal] storage init failed; check flash");
    while (true) delay(1000);
  }
  pzem::begin();

  // Probe the PZEM once at boot and report its status on the console (mirrors
  // the RTC line), so a miswired / unpowered / wrong-baud meter is obvious at
  // startup. Retry a few times since the first Modbus reads right after the
  // UART comes up can miss.
  {
    PzemSample psample{};
    bool pok = false;
    for (int i = 0; i < 5 && !pok; ++i) {
      pok = pzem::read(psample);
      if (!pok) delay(200);
    }
    if (pok) {
      LOG_PRINTF("[pzem] OK: V=%.1f I=%.3f P=%.1f Wh=%.0f PF=%.2f Hz=%.1f\n",
                    psample.voltage, psample.current, psample.power,
                    psample.energy_wh, psample.pf, psample.frequency);
    } else {
      LOG_PRINTLN("[pzem] ERROR: no response — check wiring (RX=GPIO20, TX=GPIO21), "
                  "5 V power, and 9600 baud");
    }
  }

  // BOOT button for the fresh-install energy reset (long-press in loop()).
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  // Seed wall clock from the DS1307 if it's healthy. This lets "Today:" energy
  // totals track from boot instead of only counting the current session until
  // NTP or BLE sets the clock.
  if (rtc::begin()) {
    time_t epoch = rtc::read_epoch();
    if (epoch > 0 && time_source::set_wall_clock(epoch)) {
      struct tm lt;
      localtime_r(&epoch, &lt);
      char tbuf[40];
      strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S %Z", &lt);
      LOG_PRINTF("[boot] RTC time: %s  (epoch=%ld)\n", tbuf, (long)epoch);
    } else {
      LOG_PRINTLN("[boot] RTC present but returned no time");
    }
  } else {
    LOG_PRINTLN("[boot] RTC unavailable (lost power or not wired)");
  }

  // Seed shared state.
  if (state_lock(pdMS_TO_TICKS(1000))) {
    g_state.boot_id = storage::boot_id();
    g_state.last_seq = storage::last_seq();
    g_state.unsynced_count = storage::current_unsynced_count();
    g_state.buffer_full = storage::is_buffer_full();
    g_state.wifi_status = WIFI_IDLE;
    g_state.ble_status = BLE_OFF;
    g_state.wall_clock_known = time_source::wall_clock_known();
    state_unlock();
  }

  LOG_PRINTF("Device ID: %s  fw=%s  boot=%u  unsynced=%u\n",
                identity::device_id().c_str(), identity::fw_version(),
                storage::boot_id(), storage::current_unsynced_count());

  // Diagnostic: print both the factory base MAC (what esptool shows) and the
  // effective Wi-Fi STA MAC (what WiFi.macAddress() returns). They differ if
  // a Custom MAC has been burned into eFuse, which is why two boards can
  // print different BLE names than their printed chip MACs would suggest.
  {
    uint8_t base[6] = {0};
    uint8_t sta[6]  = {0};
    esp_efuse_mac_get_default(base);
    esp_read_mac(sta, ESP_MAC_WIFI_STA);
    LOG_PRINTF("[boot] eFuse base MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  base[0], base[1], base[2], base[3], base[4], base[5]);
    LOG_PRINTF("[boot] Wi-Fi STA MAC : %02X:%02X:%02X:%02X:%02X:%02X (used for device_id)\n",
                  sta[0],  sta[1],  sta[2],  sta[3],  sta[4],  sta[5]);
  }

  // Hardcoded Wi-Fi fallback for bench testing. If WIFI_SSID is non-empty
  // and the NVS slot has no credentials yet, copy them in. Once provisioned
  // via BLE the saved value wins and this block becomes a no-op.
  if (sizeof(WIFI_SSID) > 1) {
    storage::WifiCred existing[MAX_WIFI_CREDS];
    if (storage::get_wifi_creds(existing, MAX_WIFI_CREDS) == 0) {
      if (storage::add_wifi_cred(WIFI_SSID, WIFI_PASSWORD)) {
        LOG_PRINTF("[boot] seeded Wi-Fi from config.h: %s\n", WIFI_SSID);
      }
    }
  }

  ble_service::begin();
  wifi_sync::begin();
  led::begin();
  relay::begin();

  // ESP32-C3 has a single core, so both tasks are pinned to core 0. The
  // higher-priority SamplingTask preempts ConnectivityTask; both yield via
  // vTaskDelay so neither starves the other.
  xTaskCreatePinnedToCore(sampling_task, "sampling",
                          SAMPLING_TASK_STACK, nullptr, SAMPLING_TASK_PRIO,
                          nullptr, 0);
  xTaskCreatePinnedToCore(connectivity_task, "conn",
                          CONN_TASK_STACK, nullptr, CONN_TASK_PRIO,
                          nullptr, 0);
}

// ---- Main loop (idle / serial console / WDT feeder) ------------------------
void loop() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        handle_serial_command(line);
        line = "";
      }
    } else if (line.length() < 64) {
      line += c;
    }
  }

  // BOOT button (GPIO 9) long-press = zero the PZEM energy register (fresh
  // install). We only raise a request here; sampling_task performs the reset so
  // all PZEM access stays on one task. Fires once per hold.
  static uint32_t boot_press_start_ms = 0;
  static bool     boot_reset_fired    = false;
  if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
    if (boot_press_start_ms == 0) boot_press_start_ms = millis();
    if (!boot_reset_fired && (millis() - boot_press_start_ms) >= FACTORY_RESET_HOLD_MS) {
      boot_reset_fired   = true;
      g_energy_reset_req = true;
      LOG_PRINTLN("[reset] BOOT held — energy reset requested");
    }
  } else {
    boot_press_start_ms = 0;
    boot_reset_fired    = false;
  }

  led::tick();
  relay::tick();
  delay(50);
}

static void handle_serial_command(const String &cmd) {
  String c = cmd;
  c.trim();
  c.toUpperCase();
  if (c == "DUMP") {
    storage::dump_log_to_serial();
  } else if (c == "BOOTS") {
    storage::dump_boots_to_serial();
  } else if (c == "CLEAR") {
    storage::clear_log();
  } else if (c == "WIFI") {
    storage::WifiCred creds[MAX_WIFI_CREDS];
    size_t n = storage::get_wifi_creds(creds, MAX_WIFI_CREDS);
    LOG_PRINTF("%u saved networks\n", (unsigned)n);
    for (size_t i = 0; i < n; ++i) {
      LOG_PRINTF("  %u: %s\n", (unsigned)i, creds[i].ssid.c_str());
    }
  } else if (c == "INFO") {
    LOG_PRINTF("id=%s fw=%s boot=%u last_seq=%llu unsynced=%u free=%u\n",
                  identity::device_id().c_str(), identity::fw_version(),
                  storage::boot_id(), (unsigned long long)storage::last_seq(),
                  storage::current_unsynced_count(),
                  (unsigned)storage::free_bytes());
  } else if (c == "SYNC") {
    LOG_PRINTLN("[cmd] requesting immediate Wi-Fi sync");
    wifi_sync::request_immediate_sync();
  } else if (c == "CLEARBOOTS") {
    storage::clear_boot_history();
    LOG_PRINTLN("[cmd] boot_history cleared from NVS");
  } else if (c == "LOG") {
    // Append a synthetic row using the latest sample so the next sync
    // has something to send. Useful for end-to-end testing without
    // waiting LOG_INTERVAL_SEC.
    SharedState snap;
    if (state_snapshot(snap)) {
      uint64_t seq = storage::last_seq() + 1;
      storage::RowFields rf;
      rf.seq = seq;
      rf.boot_id = storage::boot_id();
      rf.sec_since_boot = (uint32_t)(time_source::monotonic_us() / 1000000ULL);
      rf.V = snap.latest.voltage;
      rf.I = snap.latest.current;
      rf.P = snap.latest.power;
      rf.Wh = snap.latest.energy_wh;
      rf.PF = snap.latest.pf;
      if (storage::append_row(rf)) {
        storage::set_last_seq(seq);
        wifi_sync::request_immediate_sync();
        LOG_PRINTF("[cmd] synthetic row seq=%llu logged, sync requested\n",
                      (unsigned long long)seq);
      } else {
        LOG_PRINTLN("[cmd] append_row failed (buffer full?)");
      }
    }
  } else {
    LOG_PRINTF("unknown command: %s (try DUMP, BOOTS, CLEAR, CLEARBOOTS, WIFI, INFO, SYNC, LOG)\n",
                  c.c_str());
  }
}

// ---- SamplingTask -----------------------------------------------------------
static void sampling_task(void *) {
  esp_task_wdt_add(nullptr);

  uint64_t last_us = time_source::monotonic_us();
  uint64_t last_log_us = last_us;
  uint32_t prev_day_observed = 0;   // last local day this boot has seen
  bool clean_uptime_marked = false;
  uint32_t boot_start_us_sec = (uint32_t)(last_us / 1000000ULL);

  // Session anchor = first valid PZEM cumulative reading after this boot.
  // RAM-only; resets on every reboot, which is exactly what "session" means.
  float session_anchor_wh = -1.0f;

  for (;;) {
    esp_task_wdt_reset();

    // Fresh-install energy reset — from the BOOT long-press (loop()) or the
    // BLE "reset energy" command (Android app). Done here, on the PZEM-owning
    // task, so the resetEnergy() Modbus write never races pzem::read(). Read
    // both sources unconditionally so a request on either isn't left pending.
    bool reset_from_button = g_energy_reset_req;
    g_energy_reset_req = false;
    bool reset_from_ble = pzem::consume_reset_request();
    if (reset_from_button || reset_from_ble) {
      if (pzem::reset_energy()) {
        session_anchor_wh = -1.0f;   // re-anchor session; today re-anchors on the drop
        LOG_PRINTF("[reset] PZEM energy register zeroed (%s)\n",
                      reset_from_ble ? "BLE app" : "BOOT button");
      } else {
        LOG_PRINTLN("[reset] pzem::reset_energy() failed");
      }
    }

    PzemSample sample{};
    bool ok = pzem::read(sample);
    PzemStatus st = pzem::classify(ok, sample);

    // Log PZEM status transitions so a fault that develops (or clears) after
    // boot shows up on the console, not just the one-shot probe in setup().
    static PzemStatus last_logged_st = (PzemStatus)0xFF;  // force first log
    if (st != last_logged_st) {
      const char *sname = st == PZEM_OK ? "OK"
                        : st == PZEM_STALE ? "STALE (no Modbus response)"
                        : st == PZEM_SENSOR_FAULT ? "SENSOR_FAULT (V ~0 while on)"
                        : "UNKNOWN";
      LOG_PRINTF("[pzem] status: %s\n", sname);
      last_logged_st = st;
    }

    uint64_t now_us = time_source::monotonic_us();
    last_us = now_us;

    // ----- kWh display values, all derived from PZEM (no ESP32 integration) -----
    float total_kwh = 0.0f;
    float session_kwh = 0.0f;
    float today_kwh = 0.0f;
    bool today_partial = true;

    if (ok) {
      // Total: PZEM's lifetime cumulative reading, straight conversion.
      total_kwh = sample.energy_wh / 1000.0f;

      // Session: anchored at first PZEM read of this boot. Re-anchor if the
      // PZEM rolled back (someone called resetEnergy(), or hardware reset).
      if (session_anchor_wh < 0 || sample.energy_wh < session_anchor_wh) {
        session_anchor_wh = sample.energy_wh;
      }
      session_kwh = (sample.energy_wh - session_anchor_wh) / 1000.0f;

      // Today: anchored in NVS at the start of the day. Re-anchor whenever
      // the day changes, when no anchor exists yet, or when the PZEM rolled
      // back below the stored anchor.
      if (time_source::wall_clock_known()) {
        uint32_t today = time_source::local_day_number();
        float anchor_wh = storage::today_anchor_wh();
        uint32_t anchor_day = storage::today_anchor_day();
        bool anchor_clean = storage::today_anchor_clean();

        bool observed_rollover =
            (prev_day_observed != 0) && (prev_day_observed != today);
        bool need_reanchor = (anchor_wh < 0) ||
                             (anchor_day != today) ||
                             (sample.energy_wh < anchor_wh);

        if (need_reanchor) {
          // Clean iff we watched the day boundary tick over during this boot
          // (so we caught the full day's energy). Otherwise it's a mid-day
          // boot, a wall-clock-just-arrived case, or a multi-day gap.
          bool clean = observed_rollover;
          storage::set_today_anchor(sample.energy_wh, today, clean);
          anchor_wh = sample.energy_wh;
          anchor_clean = clean;
        }
        today_kwh = (sample.energy_wh - anchor_wh) / 1000.0f;
        if (today_kwh < 0) today_kwh = 0.0f;
        today_partial = !anchor_clean;
        prev_day_observed = today;
      }
    }

    if (state_lock()) {
      if (ok) {
        g_state.latest = sample;
        g_state.total_kwh = total_kwh;
        g_state.session_kwh = session_kwh;
        g_state.today_kwh = today_kwh;
        g_state.today_is_partial = today_partial;
        if (sample.power > g_state.peak_power_w) g_state.peak_power_w = sample.power;
      }
      g_state.pzem_status = st;
      g_state.wall_clock_known = time_source::wall_clock_known();
      g_state.uptime_sec = (uint32_t)(now_us / 1000000ULL);
      g_state.unsynced_count = storage::current_unsynced_count();
      g_state.buffer_full = storage::is_buffer_full();
      state_unlock();
    }

    // Feed the relay's compressor-aware cutoff with the latest wattage.
    relay::update_power(sample.power, ok);

    // Periodic log row. Cadence is server-configurable (storage::log_interval_sec)
    // and falls back to LOG_INTERVAL_SEC_DEFAULT (config.h) on a fresh device.
    uint32_t log_period_sec = storage::log_interval_sec();
    if ((uint64_t)(now_us - last_log_us) >= (uint64_t)log_period_sec * 1000000ULL) {
      last_log_us = now_us;
      if (ok || st == PZEM_OK) {
        uint64_t seq = storage::last_seq() + 1;
        storage::RowFields rf;
        rf.seq = seq;
        rf.boot_id = storage::boot_id();
        rf.sec_since_boot = (uint32_t)(now_us / 1000000ULL);
        rf.V = sample.voltage;
        rf.I = sample.current;
        rf.P = sample.power;
        rf.Wh = sample.energy_wh;
        rf.PF = sample.pf;
        rf.Hz = sample.frequency;
        if (storage::append_row(rf)) {
          storage::set_last_seq(seq);
          if (state_lock()) {
            g_state.last_seq = seq;
            g_state.unsynced_count = storage::current_unsynced_count();
            state_unlock();
          }
          // Push to MilesWeb as soon as the next ConnectivityTask tick runs.
          // If Wi-Fi is reachable, the row ships within seconds; if not,
          // it stays in /log.csv and the periodic 2-min cycle retries.
          wifi_sync::request_immediate_sync();
        }
      }
    }

    // Mark clean uptime after passing the boot-loop window.
    if (!clean_uptime_marked &&
        (uint32_t)(now_us / 1000000ULL) - boot_start_us_sec >= BOOTLOOP_WINDOW_SEC) {
      health_mark_clean_uptime();
      clean_uptime_marked = true;
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
  }
}

// ---- ConnectivityTask ------------------------------------------------------
static void connectivity_task(void *) {
  esp_task_wdt_add(nullptr);
  uint64_t last_wifi_us      = time_source::monotonic_us();
  uint64_t last_ble_alive_us = time_source::monotonic_us();
  bool first_cycle = true;
  // On the single-core C3, Wi-Fi (esp. the TLS sync) crashes while the BLE
  // controller is co-active. So run BLE ONLY for the first BLE_CONFIG_WINDOW_SEC
  // — the provisioning window, Wi-Fi held off — then shut BLE down and give
  // Wi-Fi the radio. `ble_off` latches true once that handoff happens. With
  // BLE_CONFIG_WINDOW_SEC == 0 the handoff is disabled and both run concurrently.
  bool ble_off = false;

  for (;;) {
    esp_task_wdt_reset();
    uint64_t now_us = time_source::monotonic_us();
    uint64_t uptime_sec = now_us / 1000000ULL;

    if (!ble_off) {
      ble_service::tick();
      if (ble_service::is_alive()) last_ble_alive_us = now_us;
    }

    if (!health::boot_loop_tripped()) {
      bool wifi_allowed;
#if BLE_CONFIG_WINDOW_SEC > 0
      if (!ble_off && uptime_sec >= (uint64_t)BLE_CONFIG_WINDOW_SEC) {
        // Config window is over, but if the app is still connected, don't cut it
        // off mid-session — hold BLE (and keep Wi-Fi off the radio) until the app
        // disconnects, then hand the radio to Wi-Fi. Log the deferral once.
        if (ble_service::is_connected()) {
          static bool logged_defer = false;
          if (!logged_defer) {
            LOG_PRINTLN("[conn] BLE config window over but app is connected — deferring handoff until it disconnects");
            logged_defer = true;
          }
        } else {
          LOG_PRINTLN("[conn] BLE config window over — shutting BLE down, Wi-Fi takes the radio");
          ble_service::shutdown();
          ble_off = true;
          first_cycle = true;   // connect immediately now that the radio is free
        }
      }
      wifi_allowed = ble_off;   // Wi-Fi only after BLE has handed off the radio
#else
      wifi_allowed = true;      // handoff disabled: BLE + Wi-Fi run concurrently
#endif

      // On-demand Wi-Fi scan requested over BLE (provisioning), only while BLE
      // is still up during the config window.
      if (!ble_off && wifi_sync::consume_scan_request()) {
        wifi_sync::run_scan();
        ble_service::tick();  // push the fresh results immediately
      }

      if (wifi_allowed) {
        uint64_t since_us = now_us - last_wifi_us;
        uint64_t interval_us = (uint64_t)WIFI_SCAN_INTERVAL_SEC * 1000000ULL;
        bool periodic_due = first_cycle ? true : (since_us >= interval_us);
        bool triggered = wifi_sync::consume_immediate_sync_request();
        if (periodic_due || triggered) {
          first_cycle = false;
          last_wifi_us = now_us;
          wifi_sync::run_cycle();
        }
      }
    }

    // ---- Stuck-watchdog soft reboots ---------------------------------------
    // Independent of the 30 s task WDT — these catch the subtler case where
    // every task is alive but the radio side is silently dead. Guarded by
    // uptime so we never reboot in the first STUCK_*_REBOOT_SEC after boot.
    if (uptime_sec > STUCK_WIFI_REBOOT_SEC) {
      uint32_t since_post = wifi_sync::seconds_since_last_successful_post();
      // UINT32_MAX = never posted -> don't reboot a brand-new / unprovisioned
      // device. Only reboot if we *had* been syncing and now can't.
      if (since_post != UINT32_MAX && since_post > STUCK_WIFI_REBOOT_SEC) {
        LOG_PRINTF("[health] stuck-wifi watchdog: %u s since last POST, restarting\n",
                   since_post);
        delay(100);
        esp_restart();
      }
    }

    // Stuck-BLE watchdog only applies while BLE is meant to be alive. After the
    // intentional handoff shutdown, BLE is off by design — don't reboot on it.
    if (!ble_off) {
      uint64_t since_ble_us = time_source::monotonic_us() - last_ble_alive_us;
      if (since_ble_us / 1000000ULL > STUCK_BLE_REBOOT_SEC) {
        LOG_PRINTF("[health] stuck-ble watchdog: %llu s since BLE was alive, restarting\n",
                   (unsigned long long)(since_ble_us / 1000000ULL));
        delay(100);
        esp_restart();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
