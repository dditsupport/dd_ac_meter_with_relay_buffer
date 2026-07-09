#include "wifi_sync.h"
#include "config.h"
#include "shared_state.h"
#include "storage.h"
#include "identity.h"
#include "time_source.h"
#include "rtc.h"
#include "led.h"
#include "relay.h"
#include "ble_service.h"   // pause/resume advertising during the Wi-Fi connect

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "log_serial.h"

// TODO: HMAC payload signing as a future hardening step. For v1, the only auth
// is the X-Device-Token header. Cert pinning is also TODO; we use setInsecure()
// to accept any server cert because the device speaks to a single endpoint
// configured at flash time and the token is the bearer credential.
//
// To enable cert pinning later: replace setInsecure() with setCACert(rootCA)
// and bundle the root cert as a PROGMEM blob in config.h.

namespace wifi_sync {

static volatile bool s_radio_busy = false;
static volatile bool s_immediate_sync_pending = false;
static volatile bool s_scan_pending = false;
static String s_scan_results_json = "[]";
static volatile uint32_t s_scan_version = 0;
// Time of last fully-successful POST round-trip. 0 means "never this boot",
// which forces a heartbeat POST on the first cycle so a fresh device picks
// up server-pushed config (log_interval_sec, server_time) without waiting
// for its first 15-min log row.
static uint64_t s_last_successful_post_us = 0;

// Last STA disconnect reason code, captured from the Wi-Fi event so the connect
// failure log can say *why* it failed. Common codes: 15 =
// 4WAY_HANDSHAKE_TIMEOUT (wrong password), 2 = AUTH_EXPIRE, 201 = NO_AP_FOUND,
// 205 = CONNECTION_FAIL. 0 = none captured yet.
static volatile uint8_t s_last_disc_reason = 0;

static void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    s_last_disc_reason = info.wifi_sta_disconnected.reason;
  }
}

static void set_wifi_status(WifiStatus st) {
  if (state_lock()) {
    g_state.wifi_status = st;
    state_unlock();
  }
}

static bool try_connect_known() {
  // Already connected from a previous cycle? Reuse the link — re-scanning
  // and calling WiFi.begin() again every 2 min would otherwise force a
  // disconnect/reconnect and spam the log with the IDF's own
  // early-log noise (the bursts of high-bit bytes that locked_vprintf
  // can't catch because they're written via ets_printf).
  if (WiFi.status() == WL_CONNECTED) {
    set_wifi_status(WIFI_CONNECTED);
    return true;
  }

  storage::WifiCred creds[MAX_WIFI_CREDS];
  size_t n = storage::get_wifi_creds(creds, MAX_WIFI_CREDS);
  if (n == 0) {
    LOG_PRINTLN("[wifi] no saved network — nothing to connect to");
    return false;
  }

  // Log the saved network(s) so the serial console shows exactly which SSID the
  // device is about to try (and how many are stored).
  LOG_PRINTF("[wifi] %u saved network(s):\n", (unsigned)n);
  for (size_t i = 0; i < n; ++i) {
    LOG_PRINTF("[wifi]   [%u] \"%s\"\n", (unsigned)i, creds[i].ssid.c_str());
  }

  // Not connected: clear any lingering "connecting" state before the
  // WiFi.begin() below, then give the IDF a moment to actually leave the
  // connecting state. On the single-core ESP32-C3 (Wi-Fi sharing the core with
  // BLE), a prior failed attempt can leave the STA stuck mid-connect; the IDF
  // then rejects the next set_config with "sta is connecting, cannot set
  // config", so the fresh credentials never apply and it never associates.
  // (Background auto-reconnect — the usual source of that stuck state — is
  // disabled in begin(), so we drive every connect from a clean, idle STA.)
  WiFi.disconnect(false, false);
  delay(200);

  // Pause BLE advertising for the whole connect window. On the single-core
  // ESP32-C3 the radio is time-shared with BLE in software, and NimBLE
  // advertising starves the 802.11 auth exchange so it expires (disconnect
  // reason 2 = AUTH_EXPIRE) even at a strong signal — which is exactly why the
  // stand-alone Wi-Fi scanner (no BLE) sees the AP fine but this firmware could
  // not associate. With advertising stopped, Wi-Fi owns the radio for auth /
  // assoc / the WPA handshake. Advertising is resumed before we return.
  ble_service::pause_advertising();
  delay(100);

  // Report the target AP's signal level as this device sees it. Now that BLE is
  // paused the scan is reliable (unlike a scan run alongside BLE). This is
  // informational only — we always attempt the connect below, so a hidden SSID
  // or a scan miss never blocks it.
  int found = WiFi.scanNetworks(false, true, false, 300);
  for (size_t i = 0; i < n; ++i) {
    bool seen = false;
    for (int j = 0; j < found; ++j) {
      if (WiFi.SSID(j) == creds[i].ssid) {
        LOG_PRINTF("[wifi] \"%s\" seen at %d dBm (ch %d)\n",
                      creds[i].ssid.c_str(), (int)WiFi.RSSI(j), WiFi.channel(j));
        seen = true;
        break;
      }
    }
    if (!seen) {
      LOG_PRINTF("[wifi] \"%s\" not seen in scan (hidden or out of range)\n",
                    creds[i].ssid.c_str());
    }
  }
  if (found > 0) WiFi.scanDelete();

  // Connect by calling WiFi.begin() directly for each stored network — a SINGLE
  // attempt per cycle. Do NOT retry with a second WiFi.begin() here: on the
  // single-core C3, tearing Wi-Fi down (WiFi.disconnect(true,true)) and
  // re-begin()'ing it while the BLE controller is up deterministically crashes
  // the coexistence stack (RISC-V panic on the 2nd begin). The natural
  // WIFI_SCAN_INTERVAL_SEC cycle provides the retry cadence instead. We also do
  // NOT gate on the scan above: the IDF connection manager finds the AP's
  // channel itself, which also covers hidden SSIDs.
  bool connected = false;
  for (size_t i = 0; i < n && !connected; ++i) {
    set_wifi_status(WIFI_CONNECTING);
    // Print the exact SSID and password being used so the credential in NVS can
    // be verified against the router. (Diagnostic: the password is echoed in
    // clear text on the serial console.)
    LOG_PRINTF("[wifi] connecting to \"%s\" with password \"%s\" ...\n",
                  creds[i].ssid.c_str(), creds[i].password.c_str());
    s_last_disc_reason = 0;
    WiFi.begin(creds[i].ssid.c_str(), creds[i].password.c_str());
    // Cap TX power immediately after begin(). The ESP32-C3 Super Mini's onboard
    // LDO is only ~250 mA, but the radio peaks at ~335 mA in a 20 dBm TX burst;
    // at full power the rail sags mid-burst, the PA distorts the 802.11 auth
    // frames, the AP never decodes them, and the supplicant times out with
    // reason 2 (AUTH_EXPIRE) — before the WPA password is ever tested. 15 dBm
    // drops the peak to ~240 mA, under the LDO limit, and association completes.
    // This MUST be reapplied after every begin(): the driver resets TX power on
    // mode changes / reconnect, so a one-time call in setup() silently drifts
    // back to max on the first reconnect.
    WiFi.setTxPower(WIFI_TX_POWER);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      set_wifi_status(WIFI_CONNECTED);
      LOG_PRINTF("[wifi] connected to %s, ip=%s, rssi=%d dBm\n",
                    creds[i].ssid.c_str(),
                    WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
      connected = true;
      break;
    }
    // Report the terminal status AND the STA disconnect reason so the console
    // distinguishes a wrong password (reason 15 = 4WAY_HANDSHAKE_TIMEOUT) from
    // AP-not-found (reason 201 = NO_AP_FOUND) from an auth expiry (reason 2,
    // typically radio-coexistence starvation). status is the Arduino
    // wl_status_t (6 = WL_DISCONNECTED).
    LOG_PRINTF("[wifi] \"%s\" did not connect (status=%d, reason=%d)\n",
                  creds[i].ssid.c_str(), (int)WiFi.status(),
                  (int)s_last_disc_reason);
    WiFi.disconnect(true, true);
  }

  // Restore advertising whether or not we connected, so the app can still reach
  // the device over BLE between Wi-Fi cycles.
  ble_service::resume_advertising();
  return connected;
}

// Last NTP sync (uint64 monotonic-us) and last good epoch, for rate-limiting.
static uint64_t s_last_ntp_us = 0;
static bool     s_ntp_ever_ok = false;

// Last measured DS1307 drift for the hourly drift log: signed seconds where
// + = RTC ahead of true time (running fast), and the NTP epoch at which it was
// measured (0 = none yet). Sent in the POST; the server logs each distinct
// measurement so drift can be tracked over time.
static long   s_rtc_drift_sec   = 0;
static time_t s_rtc_drift_epoch = 0;

// Last successful ingest POST timestamp (monotonic-us) for the stuck-Wi-Fi
// watchdog. Sentinel 0 means "never since boot".
static uint64_t s_last_post_us = 0;

static bool ntp_sync_if_due() {
  // Skip NTP if the wall clock is already known AND the last sync was less
  // than NTP_RESYNC_INTERVAL_SEC ago. Saves ~4–8 seconds of busy-wait per
  // Wi-Fi cycle when the device cycles every 2 minutes but only needs a
  // fresh time reference once an hour.
  bool wc_known = false;
  if (state_lock()) { wc_known = g_state.wall_clock_known; state_unlock(); }
  uint64_t now_us  = time_source::monotonic_us();
  uint64_t since_s = (now_us - s_last_ntp_us) / 1000000ULL;
  if (s_ntp_ever_ok && wc_known && since_s < (uint64_t)NTP_RESYNC_INTERVAL_SEC) {
    return true;  // recent enough — skip the network round-trip
  }

  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
  uint32_t start = millis();
  while (millis() - start < NTP_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      time_source::set_wall_clock(now);
      if (state_lock()) {
        g_state.wall_clock_known = true;
        state_unlock();
      }
      // Mirror NTP-corrected time into the DS1307 so it stays accurate
      // across power loss. Skip the write if the RTC is already within
      // the small drift threshold to limit flash/I2C traffic.
      time_t rtc_now = rtc::read_epoch();
      if (rtc_now > 0) {
        // Capture the signed drift BEFORE any writeback so the hourly log
        // records how far the RTC had wandered. + = RTC ahead (fast).
        s_rtc_drift_sec   = (long)rtc_now - (long)now;
        s_rtc_drift_epoch = now;
      }
      long drift = (long)now - (long)rtc_now;
      if (drift < 0) drift = -drift;
      if (rtc_now == 0 || drift > RTC_WRITEBACK_DRIFT_SEC) {
        if (rtc::write_epoch(now)) {
          LOG_PRINTF("[wifi] RTC writeback ok (drift=%ld sec)\n", drift);
        }
      }
      LOG_PRINTF("[wifi] NTP sync ok, epoch=%ld\n", (long)now);
      s_last_ntp_us = now_us;
      s_ntp_ever_ok = true;
      return true;
    }
    delay(100);
  }
  LOG_PRINTLN("[wifi] NTP sync timed out");
  return false;
}

// Averaged CR2032 coin-cell (RTC backup) voltage in millivolts, read from
// PIN_COINCELL_ADC. analogReadMilliVolts() applies the chip's eFuse ADC
// calibration, so no manual esp_adc_cal work is needed. COINCELL_DIVIDER_RATIO
// scales the pin voltage back to the cell node (1.0 on this build — no
// divider). Reported on each POST.
static uint32_t read_coincell_mv() {
  static bool s_adc_ready = false;
  if (!s_adc_ready) {
    analogSetPinAttenuation(PIN_COINCELL_ADC, ADC_11db);  // ~0–3.1 V full scale
    s_adc_ready = true;
  }
  uint32_t acc = 0;
  for (int i = 0; i < COINCELL_ADC_SAMPLES; ++i) {
    acc += analogReadMilliVolts(PIN_COINCELL_ADC);
  }
  float mv = (float)acc / COINCELL_ADC_SAMPLES * COINCELL_DIVIDER_RATIO;
  return (uint32_t)(mv + 0.5f);
}

static bool post_batch(uint64_t snapshot_seq, uint64_t &out_acked_seq) {
  // Collect up to SYNC_BATCH_SIZE rows with seq <= snapshot_seq.
  StaticJsonDocument<16384> doc;
  doc["device_id"] = identity::device_id();
  doc["fw_version"] = identity::fw_version();
  doc["sync_wall_time"] = time_source::iso8601_now();
  doc["current_boot_id"] = storage::boot_id();
  doc["current_boot_uptime_sec"] = (uint32_t)(time_source::monotonic_us() / 1000000ULL);

  // Report current relay state so the server can show a live indicator.
  doc["relay_on"]      = relay::is_on();
  doc["relay_mode"]    = relay::mode_str();   // "auto" | "on" | "off"
  doc["relay_version"] = relay::version();

  // Hourly DS1307 drift (signed seconds, + = RTC ahead of true time), measured
  // at the last NTP sync. rtc_drift_epoch is the NTP epoch of that measurement;
  // the server dedups on (device_id, epoch) so each hourly sample is logged
  // once even though it rides every 2-minute POST until the next sync.
  if (s_rtc_drift_epoch > 0) {
    doc["rtc_drift_sec"]   = s_rtc_drift_sec;
    doc["rtc_drift_epoch"] = (uint32_t)s_rtc_drift_epoch;
  }

  // Connected-AP Wi-Fi signal strength (dBm, negative). This POST always runs
  // over Wi-Fi, so RSSI is valid; the server logs it alongside the RTC drift
  // sample and caches the latest for the admin list.
  doc["wifi_rssi"] = (int)WiFi.RSSI();

  // CR2032 coin-cell (RTC backup) voltage (millivolts). Logged next to the RTC
  // drift sample and cached as the latest for the admin list.
  doc["coincell_mv"] = read_coincell_mv();

  JsonArray hist = doc.createNestedArray("boot_history");
  storage::BootRecord recs[MAX_BOOT_HISTORY];
  size_t hn = storage::get_boot_history(recs, MAX_BOOT_HISTORY);
  for (size_t i = 0; i < hn; ++i) {
    JsonObject o = hist.createNestedObject();
    o["boot_id"] = recs[i].boot_id;
    o["duration_sec"] = recs[i].duration_sec;
  }

  JsonArray readings = doc.createNestedArray("readings");
  uint64_t max_in_batch = 0;
  uint32_t included = 0;
  storage::stream_rows_up_to(snapshot_seq, [&](const storage::RowFields &r) -> bool {
    if (included >= SYNC_BATCH_SIZE) return false;
    JsonObject o = readings.createNestedObject();
    o["seq"] = r.seq;
    o["boot_id"] = r.boot_id;
    o["sec"] = r.sec_since_boot;
    o["V"] = r.V;
    o["I"] = r.I;
    o["P"] = r.P;
    o["Wh"] = r.Wh;
    o["PF"] = r.PF;
    o["Hz"] = r.Hz;
    if (r.seq > max_in_batch) max_in_batch = r.seq;
    included++;
    return true;
  });

  if (included == 0) {
    // Empty buffer. Decide whether to send a config-fetch heartbeat anyway.
    uint64_t now_us = time_source::monotonic_us();
    bool heartbeat_due =
        (s_last_successful_post_us == 0) ||
        ((now_us - s_last_successful_post_us) >=
         (uint64_t)CONFIG_HEARTBEAT_SEC * 1000000ULL);
    if (!heartbeat_due) {
      out_acked_seq = snapshot_seq;
      return true;  // recently POSTed, truly nothing to send
    }
    // Fall through and POST with an empty readings array. Server can use
    // this opportunity to push log_interval_sec / server_time / etc.
    LOG_PRINTLN("[wifi] heartbeat POST (empty readings) to refresh config");
  }

  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();  // TODO: cert pinning
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  // Compose URL: NVS-configured host (BLE-settable) or the compiled default,
  // then the hardcoded path. Strip any trailing slash from the host so we
  // don't double up.
  String host = storage::ingest_host();
  if (host.isEmpty()) host = INGEST_HOST_DEFAULT;
  while (host.endsWith("/")) host.remove(host.length() - 1);
  String url = host + INGEST_PATH;

  bool ok;
  if (url.startsWith("https://")) {
    ok = http.begin(client, url);
  } else {
    ok = http.begin(url);  // plain HTTP for bench stub
  }
  if (!ok) {
    LOG_PRINTLN("[wifi] http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  s_radio_busy = true;
  set_wifi_status(WIFI_SYNCING);
  led::signal_tx();  // flash the status LED to show data going out
  int code = http.POST((uint8_t *)body.c_str(), body.length());
  String resp = http.getString();
  http.end();
  s_radio_busy = false;

  if (code != 200) {
    LOG_PRINTF("[wifi] POST failed: code=%d body=%s\n", code, resp.c_str());
    return false;
  }
  StaticJsonDocument<256> rdoc;
  if (deserializeJson(rdoc, resp)) {
    LOG_PRINTF("[wifi] bad response JSON: %s\n", resp.c_str());
    return false;
  }
  if (!(rdoc["ok"] | false)) {
    LOG_PRINTF("[wifi] server rejected: %s\n", resp.c_str());
    return false;
  }
  uint64_t acked = rdoc["acked_up_to_seq"] | 0;
  if (acked == 0) acked = max_in_batch;
  out_acked_seq = acked;
  s_last_successful_post_us = time_source::monotonic_us();

  // Optional: server-pushed logging cadence. Lets ops change the 15-min
  // default to anything between 60 s and 86400 s without reflashing. Values
  // outside that range are silently rejected by storage::set_log_interval_sec().
  uint32_t srv_log_int = rdoc["log_interval_sec"] | 0;
  if (srv_log_int > 0) {
    if (storage::set_log_interval_sec(srv_log_int)) {
      LOG_PRINTF("[wifi] log_interval_sec from server: %u\n", srv_log_int);
    } else {
      LOG_PRINTF("[wifi] log_interval_sec %u out of range, ignored\n", srv_log_int);
    }
  }

  // Optional: server-pushed relay config. The server attaches relay_version
  // (uint), relay_schedule (AC-allowed open hours array), and the two cutoff
  // knobs relay_compressor_watts / relay_grace_min to every ingest response.
  // relay::apply() is a no-op when nothing changed; 0 leaves a knob unchanged.
  if (rdoc.containsKey("relay_version")) {
    uint32_t rv = rdoc["relay_version"] | 0;
    String sched_json;
    if (rdoc.containsKey("relay_schedule")) {
      serializeJson(rdoc["relay_schedule"], sched_json);
    } else {
      sched_json = "[]";
    }
    uint32_t cw = rdoc["relay_compressor_watts"] | 0;   // 0 = leave unchanged
    uint32_t gm = rdoc["relay_grace_min"]        | 0;   // 0 = leave unchanged
    relay::apply(rv, sched_json, cw, gm);
  }

  // Server-time fallback: if neither the DS1307 nor NTP gave us a wall
  // clock, seed time_source from the server's response. The server
  // returns server_time as an ISO 8601 string (MilesWeb is in UTC by
  // default; APP_TIMEZONE in secrets.php can change that).
  const char *srv_time = rdoc["server_time"] | (const char *)nullptr;
  if (srv_time && !time_source::wall_clock_known()) {
    time_t srv_epoch = time_source::parse_iso8601(srv_time);
    if (srv_epoch > 0 && time_source::set_wall_clock(srv_epoch)) {
      if (state_lock()) {
        g_state.wall_clock_known = true;
        state_unlock();
      }
      // Persist into the RTC if it's present (even if it had been marked
      // unavailable due to lost-power; this restarts the oscillator).
      rtc::write_epoch(srv_epoch);
      LOG_PRINTF("[wifi] wall clock seeded from server_time: %s\n", srv_time);
    }
  }

  LOG_PRINTF("[wifi] POST ok, %u rows, acked_up_to=%llu\n",
                included, (unsigned long long)acked);
  s_last_post_us = time_source::monotonic_us();
  return true;
}

uint32_t seconds_since_last_successful_post() {
  if (s_last_post_us == 0) return UINT32_MAX;
  return (uint32_t)((time_source::monotonic_us() - s_last_post_us) / 1000000ULL);
}

void begin() {
  WiFi.mode(WIFI_STA);
  // Drive every (re)connect ourselves from run_cycle()/try_connect_known()
  // instead of letting the IDF auto-reconnect in the background. On the
  // single-core C3 the auto-reconnect handler fires a fresh esp_wifi_connect()
  // the instant an attempt fails and races our own WiFi.disconnect()/
  // WiFi.begin(), which makes esp_wifi_set_config() reject the new credentials
  // with "sta is connecting, cannot set config" (seen in the serial log) — so
  // the SSID/password never apply and the device never associates. With
  // auto-reconnect off, each connect starts from a clean, idle STA. We already
  // reconnect on every 2-minute cycle (and immediately on a sync request), and
  // try_connect_known() early-returns while the link is up, so continuous
  // reachability is preserved without the racing background reconnector.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  // Capture STA disconnect reason codes for the connect-failure diagnostics.
  WiFi.onEvent(on_wifi_event);
}

bool is_radio_busy() { return s_radio_busy; }

void request_immediate_sync() { s_immediate_sync_pending = true; }
bool consume_immediate_sync_request() {
  if (!s_immediate_sync_pending) return false;
  s_immediate_sync_pending = false;
  return true;
}

void request_scan() { s_scan_pending = true; }
bool consume_scan_request() {
  if (!s_scan_pending) return false;
  s_scan_pending = false;
  return true;
}

String get_scan_results_json() { return s_scan_results_json; }
uint32_t scan_results_version() { return s_scan_version; }

void run_scan() {
  s_radio_busy = true;
  set_wifi_status(WIFI_SCANNING);
  // scanNetworks(async=false, show_hidden=false, passive=false, max_ms_per_chan=300)
  int n = WiFi.scanNetworks(false, false, false, 300);
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.to<JsonArray>();
  // Sort indices by RSSI (descending) and take top MAX_SCAN_RESULTS.
  int idx[64];
  int total = n;
  if (total < 0) total = 0;
  if (total > 64) total = 64;
  for (int i = 0; i < total; ++i) idx[i] = i;
  for (int i = 1; i < total; ++i) {
    int key = idx[i];
    int j = i - 1;
    while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(key)) {
      idx[j + 1] = idx[j];
      --j;
    }
    idx[j + 1] = key;
  }
  int emit = total < MAX_SCAN_RESULTS ? total : MAX_SCAN_RESULTS;
  for (int i = 0; i < emit; ++i) {
    JsonObject o = arr.createNestedObject();
    o["s"] = WiFi.SSID(idx[i]);
    o["r"] = WiFi.RSSI(idx[i]);
    o["e"] = (WiFi.encryptionType(idx[i]) != WIFI_AUTH_OPEN) ? 1 : 0;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  s_scan_results_json = out;
  s_scan_version++;
  s_radio_busy = false;
  set_wifi_status(WIFI_IDLE);
  LOG_PRINTF("[wifi] scan complete: %d AP(s), emitted %d\n", n, emit);
}

bool run_cycle() {
  if (!try_connect_known()) {
    WiFi.disconnect(true, true);
    set_wifi_status(WIFI_IDLE);
    return false;
  }

  ntp_sync_if_due();  // hourly resync; OK to proceed even if it fails

  uint64_t snapshot = storage::snapshot_max_seq();
  // Loop until all rows up to snapshot have been acked or a POST fails.
  while (true) {
    uint64_t acked = 0;
    if (!post_batch(snapshot, acked)) break;
    if (acked > 0) {
      storage::truncate_up_to(acked);
      // Prune boot_history: any entry older than the oldest remaining row's
      // boot_id is no longer needed (server has it, device won't re-send).
      // If /log.csv is now empty, prune everything older than the current
      // boot so boot_history collapses to just {current_boot}.
      uint32_t min_keep = storage::boot_id();
      storage::stream_rows_up_to(UINT64_MAX, [&](const storage::RowFields &r) -> bool {
        if (r.boot_id < min_keep) min_keep = r.boot_id;
        return true;
      });
      storage::prune_boot_history_below(min_keep);

      if (state_lock()) {
        g_state.unsynced_count = storage::current_unsynced_count();
        state_unlock();
      }
    }
    // If nothing left to send for this snapshot, exit.
    if (storage::current_unsynced_count() == 0) break;
    // If we still have rows with seq <= snapshot (multi-batch case), continue.
    bool more = false;
    storage::stream_rows_up_to(snapshot, [&](const storage::RowFields &) {
      more = true;
      return false;
    });
    if (!more) break;
  }

  storage::set_last_sync_at((uint32_t)time(nullptr));
  // Stay connected between cycles — do NOT disconnect here. The next cycle's
  // try_connect_known() early-returns on WL_CONNECTED, so we skip the
  // reconnect (no log spam, no IDF re-association noise) and the device
  // stays reachable / shows "Connected" in the app.
  set_wifi_status(WIFI_CONNECTED);
  return true;
}

}  // namespace wifi_sync
