#include "storage.h"
#include "config.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs_flash.h>
#include "log_serial.h"

namespace storage {

static Preferences s_cfg;
static Preferences s_state;
static SemaphoreHandle_t s_log_mutex = nullptr;
// Held across "read last_seq -> append the row(s) -> set_last_seq" by the
// writers, and by rebase_seq(), so a renumbering can never land in between
// and leave a row stamped with a pre-rebase seq.
static SemaphoreHandle_t s_seq_mutex = nullptr;

static uint32_t s_boot_id = 0;
static uint64_t s_last_seq = 0;
static uint64_t s_seq_hwm = 0;
static uint32_t s_unsynced_count = 0;
static bool s_buffer_full = false;
static uint32_t s_partition_total = 0;
static volatile bool s_factory_reset_req = false;

// ---- Helpers ----------------------------------------------------------------

static bool lock_log(TickType_t ticks = pdMS_TO_TICKS(2000)) {
  return xSemaphoreTake(s_log_mutex, ticks) == pdTRUE;
}
static void unlock_log() { xSemaphoreGive(s_log_mutex); }

static bool parse_row(const String &line, RowFields &out) {
  // v3 expected: "seq,boot_id,sec,V,I,P,Wh,PF,Hz,epoch"
  // v2 (legacy): "seq,boot_id,sec,V,I,P,Wh,PF,Hz"  <- epoch defaults to 0
  // v1 (legacy): "seq,boot_id,sec,V,I,P,Wh,PF"     <- Hz defaults to 0
  int parts = 0;
  const char *s = line.c_str();
  char *end;
  uint64_t v_u64;
  uint32_t v_u32;
  float v_f;
  out.Hz = 0.0f;  // default for legacy rows
  out.epoch = 0;   // default for pre-v3 rows (clock time not recorded)

  v_u64 = strtoull(s, &end, 10);
  if (end == s || *end != ',') return false;
  out.seq = v_u64;
  s = end + 1; parts++;

  v_u32 = strtoul(s, &end, 10);
  if (end == s || *end != ',') return false;
  out.boot_id = v_u32;
  s = end + 1; parts++;

  v_u32 = strtoul(s, &end, 10);
  if (end == s || *end != ',') return false;
  out.sec_since_boot = v_u32;
  s = end + 1; parts++;

  float *fields[] = {&out.V, &out.I, &out.P, &out.Wh, &out.PF};
  for (int i = 0; i < 5; ++i) {
    v_f = strtof(s, &end);
    if (end == s) return false;
    *fields[i] = v_f;
    if (i < 4) {
      if (*end != ',') return false;
      s = end + 1;
    }
    parts++;
  }
  // Optional Hz field (v2). If present, *end == ','; otherwise it's '\n', '\r' or '\0'.
  if (*end == ',') {
    s = end + 1;
    v_f = strtof(s, &end);
    if (end != s) {
      out.Hz = v_f;
      parts++;
      // Optional wall-clock epoch (v3), only ever written after Hz.
      if (*end == ',') {
        s = end + 1;
        uint32_t ep = strtoul(s, &end, 10);
        if (end == s) return false;
        out.epoch = ep;
        parts++;
      }
    }
  }
  return parts == 8 || parts == 9 || parts == 10;
}

// Strip a trailing partial line from /log.csv if it lacks newline or fails to parse.
static void repair_tail() {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  size_t size = f.size();
  if (size == 0) { f.close(); return; }

  // Read up to last 256 bytes.
  size_t scan = size > 256 ? 256 : size;
  f.seek(size - scan, SeekSet);
  String tail;
  while (f.available()) tail += (char)f.read();
  f.close();

  // Find last full line.
  int last_nl = tail.lastIndexOf('\n');
  if (last_nl < 0) {
    // No newline at all in tail; if file <= 256B, the whole thing is garbage.
    if (size <= 256) {
      LittleFS.remove(LOG_PATH);
    } else {
      // Otherwise, leave it; tail was inside a long line. Defensive: truncate to size - scan.
      // (Should not happen in practice — rows are ~50 bytes.)
    }
    return;
  }
  // Validate the last complete line.
  String last_line = tail.substring(tail.lastIndexOf('\n', last_nl - 1) + 1, last_nl);
  RowFields rf;
  if (!parse_row(last_line, rf)) {
    // Truncate to before that bad line.
    size_t cut_at = size - (tail.length() - tail.lastIndexOf('\n', last_nl - 1) - 1);
    File w = LittleFS.open(LOG_PATH, "r+");
    if (w) {
      // ESP32 LittleFS lacks fs::truncate; rewrite without the bad line.
      w.close();
      File r = LittleFS.open(LOG_PATH, "r");
      File t = LittleFS.open(LOG_TMP_PATH, "w");
      if (r && t) {
        size_t copied = 0;
        while (r.available() && copied < cut_at) {
          int b = r.read();
          if (b < 0) break;
          t.write((uint8_t)b);
          copied++;
        }
        t.close();
        r.close();
        LittleFS.remove(LOG_PATH);
        LittleFS.rename(LOG_TMP_PATH, LOG_PATH);
      }
    }
  }
  // Also: ensure file ends with newline. If after repair last char isn't '\n', append one.
  File chk = LittleFS.open(LOG_PATH, "r");
  if (chk) {
    size_t sz = chk.size();
    if (sz > 0) {
      chk.seek(sz - 1, SeekSet);
      int last = chk.read();
      chk.close();
      if (last != '\n') {
        File ap = LittleFS.open(LOG_PATH, "a");
        if (ap) { ap.write((uint8_t)'\n'); ap.close(); }
      }
    } else chk.close();
  }
}

static uint32_t count_rows() {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  uint32_t n = 0;
  while (f.available()) {
    int b = f.read();
    if (b == '\n') n++;
  }
  f.close();
  return n;
}

static void scan_prev_boot_duration(uint32_t prev_boot_id, uint32_t &max_sec) {
  max_sec = 0;
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  String line;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n') {
      RowFields rf;
      if (parse_row(line, rf) && rf.boot_id == prev_boot_id) {
        if (rf.sec_since_boot > max_sec) max_sec = rf.sec_since_boot;
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
  f.close();
}

// ---- Public API -------------------------------------------------------------

bool begin() {
  s_log_mutex = xSemaphoreCreateMutex();
  s_seq_mutex = xSemaphoreCreateMutex();
  if (!s_log_mutex || !s_seq_mutex) return false;

  if (!LittleFS.begin(true)) {
    // begin(true) is meant to format-on-fail, but that path can itself return
    // false on a partition left inconsistent by a reset caught mid-write (it
    // sometimes formats yet fails to remount in the same call). Force an
    // explicit reformat + remount rather than bricking the device on a fatal —
    // the buffered rows are expendable, a dead unit isn't.
    LOG_PRINTLN("[storage] LittleFS mount failed — forcing reformat");
    if (!LittleFS.format() || !LittleFS.begin(false)) {
      LOG_PRINTLN("[storage] LittleFS reformat failed; check flash");
      return false;
    }
    LOG_PRINTLN("[storage] LittleFS reformatted OK (buffered rows lost)");
  }
  s_partition_total = LittleFS.totalBytes();

  // Recovery step 1: leftover /log.tmp. truncate_up_to() writes and closes the
  // complete tmp file BEFORE removing /log.csv, so a tmp with no /log.csv
  // beside it means power was lost between that remove and the rename — the
  // tmp holds every unsynced row and must be finished, not deleted. A tmp next
  // to an intact /log.csv is a copy that never completed; drop it.
  if (LittleFS.exists(LOG_TMP_PATH)) {
    if (!LittleFS.exists(LOG_PATH)) {
      LOG_PRINTLN("[storage] finishing interrupted truncate: /log.tmp -> /log.csv");
      LittleFS.rename(LOG_TMP_PATH, LOG_PATH);
    } else {
      LOG_PRINTLN("[storage] cleanup leftover /log.tmp");
      LittleFS.remove(LOG_TMP_PATH);
    }
  }

  // Recovery step 2: repair tail of /log.csv.
  if (LittleFS.exists(LOG_PATH)) {
    repair_tail();
  }

  // Open NVS namespaces.
  s_cfg.begin("cfg", false);
  s_state.begin("state", false);

  // Pull previous boot identity & seq HWM.
  uint32_t prev_boot_id = s_state.getUInt("boot_id", 0);
  s_seq_hwm = s_state.getULong64("seq_hwm", 0);

  // Restore last_seq from HWM (never reuse seqs).
  s_last_seq = s_seq_hwm;

  // If previous boot exists AND it ran long enough to log at least one row,
  // append a boot record. Zero-duration boots (dev reflashes, brief power
  // glitches) carry no readings and would just clutter boot_history with
  // entries the server doesn't need.
  if (prev_boot_id > 0) {
    uint32_t max_sec = 0;
    scan_prev_boot_duration(prev_boot_id, max_sec);
    if (max_sec > 0) {
      BootRecord rec = {prev_boot_id, max_sec};
      push_boot_record(rec);
    }
  }

  // Bump and persist new boot_id.
  s_boot_id = prev_boot_id + 1;
  s_state.putUInt("boot_id", s_boot_id);

  s_unsynced_count = count_rows();
  s_buffer_full = (LittleFS.totalBytes() - LittleFS.usedBytes()) <
                  max((uint32_t)BUFFER_FREE_MIN_BYTES,
                      (uint32_t)(s_partition_total * BUFFER_FREE_MIN_PCT / 100));

  LOG_PRINTF("[storage] boot_id=%u last_seq=%llu unsynced=%u free=%u\n",
                s_boot_id, (unsigned long long)s_last_seq, s_unsynced_count,
                (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  return true;
}

uint32_t boot_id() { return s_boot_id; }
uint64_t last_seq() { return s_last_seq; }
uint64_t seq_hwm() { return s_seq_hwm; }

bool seq_lock(TickType_t ticks) { return xSemaphoreTake(s_seq_mutex, ticks) == pdTRUE; }
void seq_unlock() { xSemaphoreGive(s_seq_mutex); }

void set_last_seq(uint64_t seq) {
  // Only ever forward: a seq is never reissued, and after rebase_seq() moves
  // the counter up nothing may drag it back down.
  if (seq <= s_last_seq) return;
  s_last_seq = seq;
  // Advance HWM only when we cross it.
  if (seq >= s_seq_hwm) {
    s_seq_hwm = seq + SEQ_HWM_STRIDE;
    s_state.putULong64("seq_hwm", s_seq_hwm);
  }
}

// ---- Boot history (circular buffer in NVS) ----------------------------------

void push_boot_record(const BootRecord &rec) {
  // Stored as a JSON array string for simplicity (max 32 entries x ~30 bytes ~ <1 KB).
  String json = s_state.getString("boots", "[]");
  StaticJsonDocument<1500> doc;
  if (deserializeJson(doc, json)) {
    doc.clear();
    doc.to<JsonArray>();
  }
  JsonArray arr = doc.as<JsonArray>();
  JsonObject obj = arr.createNestedObject();
  obj["b"] = rec.boot_id;
  obj["d"] = rec.duration_sec;
  while (arr.size() > MAX_BOOT_HISTORY) arr.remove(0);
  String out;
  serializeJson(doc, out);
  s_state.putString("boots", out);
}

void prune_boot_history_below(uint32_t min_keep_boot_id) {
  String json = s_state.getString("boots", "[]");
  StaticJsonDocument<1500> doc;
  if (deserializeJson(doc, json)) return;
  JsonArray arr = doc.as<JsonArray>();
  bool changed = false;
  for (int i = (int)arr.size() - 1; i >= 0; --i) {
    uint32_t bid = arr[i]["b"] | 0;
    if (bid < min_keep_boot_id) {
      arr.remove(i);
      changed = true;
    }
  }
  if (changed) {
    String out;
    serializeJson(doc, out);
    s_state.putString("boots", out);
  }
}

void clear_boot_history() {
  s_state.putString("boots", "[]");
}

size_t get_boot_history(BootRecord *out, size_t max_out) {
  String json = s_state.getString("boots", "[]");
  StaticJsonDocument<1500> doc;
  if (deserializeJson(doc, json)) return 0;
  JsonArray arr = doc.as<JsonArray>();
  size_t n = 0;
  for (JsonObject o : arr) {
    if (n >= max_out) break;
    out[n].boot_id = o["b"] | 0;
    out[n].duration_sec = o["d"] | 0;
    n++;
  }
  return n;
}

// ---- Wi-Fi creds ------------------------------------------------------------

size_t get_wifi_creds(WifiCred *out, size_t max_out) {
  String json = s_cfg.getString("wifi", "[]");
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, json)) return 0;
  JsonArray arr = doc.as<JsonArray>();
  size_t n = 0;
  for (JsonObject o : arr) {
    if (n >= max_out) break;
    out[n].ssid = (const char *)(o["s"] | "");
    out[n].password = (const char *)(o["p"] | "");
    if (out[n].ssid.length()) n++;
  }
  return n;
}

bool add_wifi_cred(const String &ssid, const String &password) {
  if (ssid.isEmpty()) return false;
  // Single-credential model: replace any prior entry with this one.
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.to<JsonArray>();
  JsonObject o = arr.createNestedObject();
  o["s"] = ssid;
  o["p"] = password;
  String out;
  serializeJson(doc, out);
  s_cfg.putString("wifi", out);
  return true;
}

void clear_wifi_creds() {
  s_cfg.putString("wifi", "[]");
}

void set_last_sync_at(uint32_t epoch) {
  s_state.putUInt("sync_at", epoch);
}
uint32_t last_sync_at() {
  return s_state.getUInt("sync_at", 0);
}

uint32_t last_nightly_reboot_day() {
  return s_state.getUInt("nrb_day", 0);
}
void set_last_nightly_reboot_day(uint32_t day) {
  s_state.putUInt("nrb_day", day);
}

// ---- Server-pushed maintenance config --------------------------------------

bool nightly_reboot_enabled() {
  return s_cfg.getBool("nrb_en", NIGHTLY_REBOOT_ENABLE_DEFAULT != 0);
}
uint8_t nightly_reboot_start_hour() {
  return s_cfg.getUChar("nrb_sh", NIGHTLY_REBOOT_START_HOUR_DEFAULT);
}
uint8_t nightly_reboot_end_hour() {
  return s_cfg.getUChar("nrb_eh", NIGHTLY_REBOOT_END_HOUR_DEFAULT);
}
bool set_nightly_reboot(bool enabled, uint8_t start_hour, uint8_t end_hour) {
  // The window must be a non-empty span inside one local day. Rejecting rather
  // than clamping means a malformed push leaves the previous schedule intact.
  if (start_hour > 23 || end_hour > 24 || end_hour <= start_hour) return false;
  if (s_cfg.getBool ("nrb_en", NIGHTLY_REBOOT_ENABLE_DEFAULT != 0) != enabled)
    s_cfg.putBool ("nrb_en", enabled);
  if (s_cfg.getUChar("nrb_sh", NIGHTLY_REBOOT_START_HOUR_DEFAULT) != start_hour)
    s_cfg.putUChar("nrb_sh", start_hour);
  if (s_cfg.getUChar("nrb_eh", NIGHTLY_REBOOT_END_HOUR_DEFAULT) != end_hour)
    s_cfg.putUChar("nrb_eh", end_hour);
  return true;
}

uint32_t radio_rest_interval_sec() {
  return s_cfg.getUInt("rr_int", RADIO_REST_INTERVAL_SEC_DEFAULT);
}
uint32_t radio_rest_duration_sec() {
  return s_cfg.getUInt("rr_dur", RADIO_REST_DURATION_SEC_DEFAULT);
}
bool set_radio_rest(uint32_t interval_sec, uint32_t duration_sec) {
  // 0 = periodic rest disabled; otherwise at least 10 min, so a mistyped value
  // cannot put the radio off-air on a loop. The 60 s duration cap keeps a rest
  // well inside STUCK_WIFI_REASSOC_SEC (600 s), so a rest can never be mistaken
  // for a wedged radio, and matches the bound the admin UI enforces.
  if (interval_sec != 0 && (interval_sec < 600 || interval_sec > 86400)) return false;
  if (duration_sec < 5 || duration_sec > 60) return false;
  if (s_cfg.getUInt("rr_int", RADIO_REST_INTERVAL_SEC_DEFAULT) != interval_sec)
    s_cfg.putUInt("rr_int", interval_sec);
  if (s_cfg.getUInt("rr_dur", RADIO_REST_DURATION_SEC_DEFAULT) != duration_sec)
    s_cfg.putUInt("rr_dur", duration_sec);
  return true;
}

String ingest_host() {
  return s_cfg.getString("host", "");
}
bool set_ingest_host(const String &host) {
  if (host.length() > 128) return false;  // sanity cap
  s_cfg.putString("host", host);
  return true;
}

uint32_t log_interval_sec() {
  return s_cfg.getUInt("log_int", LOG_INTERVAL_SEC_DEFAULT);
}
bool set_log_interval_sec(uint32_t sec) {
  if (sec < LOG_INTERVAL_SEC_MIN || sec > LOG_INTERVAL_SEC_MAX) return false;
  // Avoid an NVS write if the value didn't change — limits flash wear on
  // chatty servers that include the field in every response.
  if (s_cfg.getUInt("log_int", 0) == sec) return true;
  s_cfg.putUInt("log_int", sec);
  return true;
}

int wifi_tx_power_qdbm() {
  return s_cfg.getInt("txpwr", WIFI_TX_POWER_QDBM);
}
bool set_wifi_tx_power_qdbm(int qdbm) {
  // esp_wifi accepts 8..84 quarter-dBm (2..21 dBm); reject anything else.
  if (qdbm < 8 || qdbm > 84) return false;
  if (s_cfg.getInt("txpwr", -1) == qdbm) return true;  // no-op write guard
  s_cfg.putInt("txpwr", qdbm);
  return true;
}

float today_anchor_wh() {
  return s_state.getFloat("tdy_wh", -1.0f);
}
uint32_t today_anchor_day() {
  return s_state.getUInt("tdy_day", 0);
}
bool today_anchor_clean() {
  return s_state.getBool("tdy_cln", false);
}
void set_today_anchor(float wh, uint32_t day, bool clean) {
  s_state.putFloat("tdy_wh", wh);
  s_state.putUInt("tdy_day", day);
  s_state.putBool("tdy_cln", clean);
}

// ---- Log file ---------------------------------------------------------------

uint32_t free_bytes() {
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool is_buffer_full() {
  uint32_t free_b = free_bytes();
  uint32_t threshold = max((uint32_t)BUFFER_FREE_MIN_BYTES,
                           (uint32_t)(s_partition_total * BUFFER_FREE_MIN_PCT / 100));
  s_buffer_full = (free_b < threshold);
  return s_buffer_full;
}

// Render one row in the on-disk CSV format. Returns its length, or 0 if it
// did not fit (never a truncated line).
static int format_row(const RowFields &row, char *line, size_t cap) {
  int n = snprintf(line, cap,
                   "%llu,%u,%u,%.2f,%.3f,%.2f,%.2f,%.3f,%.2f,%u\n",
                   (unsigned long long)row.seq, row.boot_id, row.sec_since_boot,
                   row.V, row.I, row.P, row.Wh, row.PF, row.Hz,
                   (unsigned)row.epoch);
  return (n > 0 && n < (int)cap) ? n : 0;
}

bool append_row(const RowFields &row) {
  if (is_buffer_full()) return false;
  if (!lock_log()) return false;
  bool ok = false;
  File f = LittleFS.open(LOG_PATH, "a");
  if (f) {
    char line[128];
    int n = format_row(row, line, sizeof(line));
    if (n > 0) {
      size_t w = f.write((const uint8_t *)line, n);
      f.flush();
      f.close();
      if ((int)w == n) {
        s_unsynced_count++;
        ok = true;
      }
    } else {
      f.close();
    }
  }
  unlock_log();
  return ok;
}

uint32_t row_count() {
  return s_unsynced_count;
}

uint32_t current_unsynced_count() {
  return s_unsynced_count;
}

uint64_t snapshot_max_seq() {
  return s_last_seq;
}

uint32_t stream_rows_up_to(uint64_t max_seq, std::function<bool(const RowFields &)> cb) {
  uint32_t emitted = 0;
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  String line;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n') {
      RowFields rf;
      if (parse_row(line, rf) && rf.seq <= max_seq) {
        if (!cb(rf)) {
          f.close();
          return emitted;
        }
        emitted++;
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
  f.close();
  return emitted;
}

bool truncate_up_to(uint64_t acked_seq) {
  if (!lock_log()) return false;
  bool ok = false;
  File r = LittleFS.open(LOG_PATH, "r");
  File w = LittleFS.open(LOG_TMP_PATH, "w");
  if (r && w) {
    String line;
    uint32_t kept = 0;
    bool write_ok = true;
    while (r.available() && write_ok) {
      char c = (char)r.read();
      if (c == '\n') {
        RowFields rf;
        if (parse_row(line, rf) && rf.seq > acked_seq) {
          line += '\n';
          write_ok = w.write((const uint8_t *)line.c_str(), line.length()) == line.length();
          kept++;
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    w.flush();
    w.close();
    r.close();
    if (write_ok) {
      LittleFS.remove(LOG_PATH);
      LittleFS.rename(LOG_TMP_PATH, LOG_PATH);
      s_unsynced_count = kept;
      ok = true;
    } else {
      // Short write (flash full mid-copy): the tmp is missing rows, so keep the
      // original log intact and retry on the next ack.
      LittleFS.remove(LOG_TMP_PATH);
      LOG_PRINTLN("[storage] truncate aborted: short write to /log.tmp");
    }
  } else {
    if (r) r.close();
    if (w) w.close();
  }
  unlock_log();
  return ok;
}

bool rebase_seq(uint64_t seq_floor) {
  if (!seq_lock(pdMS_TO_TICKS(5000))) return false;
  if (!lock_log()) { seq_unlock(); return false; }

  // One uniform shift for the whole buffer, chosen so the lowest buffered seq
  // lands just above the server's highest. Uniform keeps the rows of one
  // sampling instant (one per channel) on a shared seq and keeps file order
  // ascending, which acking and truncation rely on.
  uint64_t min_seq = UINT64_MAX;
  stream_rows_up_to(UINT64_MAX, [&](const RowFields &r) -> bool {
    if (r.seq < min_seq) min_seq = r.seq;
    return true;
  });
  uint64_t offset = 0;
  bool ok = true;
  if (min_seq != UINT64_MAX && min_seq <= seq_floor) {
    offset = seq_floor + 1 - min_seq;
    File r = LittleFS.open(LOG_PATH, "r");
    File w = LittleFS.open(LOG_TMP_PATH, "w");
    if (r && w) {
      String line;
      bool write_ok = true;
      while (r.available() && write_ok) {
        char c = (char)r.read();
        if (c == '\n') {
          RowFields rf;
          if (parse_row(line, rf)) {
            rf.seq += offset;
            char buf[128];
            int n = format_row(rf, buf, sizeof(buf));
            if (n > 0) write_ok = w.write((const uint8_t *)buf, n) == (size_t)n;
          }
          line = "";
        } else if (c != '\r') {
          line += c;
        }
      }
      w.flush();
      w.close();
      r.close();
      if (write_ok) {
        // Same remove-then-rename as truncate_up_to(); begin() finishes it if
        // power is lost in between.
        LittleFS.remove(LOG_PATH);
        LittleFS.rename(LOG_TMP_PATH, LOG_PATH);
      } else {
        LittleFS.remove(LOG_TMP_PATH);
        ok = false;
      }
    } else {
      if (r) r.close();
      if (w) w.close();
      ok = false;
    }
  }
  if (ok) {
    // New rows continue above both the renumbered buffer and the server's
    // floor, and the NVS high-water mark follows so a reboot cannot fall back.
    uint64_t next_last = s_last_seq + offset;
    if (next_last < seq_floor) next_last = seq_floor;
    s_last_seq = next_last;
    if (s_last_seq >= s_seq_hwm) {
      s_seq_hwm = s_last_seq + SEQ_HWM_STRIDE;
      s_state.putULong64("seq_hwm", s_seq_hwm);
    }
  }
  unlock_log();
  seq_unlock();
  LOG_PRINTF("[storage] seq rebase above %llu: shift +%llu, last_seq=%llu (%s)\n",
             (unsigned long long)seq_floor, (unsigned long long)offset,
             (unsigned long long)s_last_seq, ok ? "ok" : "FAILED");
  return ok;
}

// ---- Serial helpers ---------------------------------------------------------

void dump_log_to_serial() {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) {
    LOG_PRINTLN("[storage] no log file");
    return;
  }
  LOG_PRINTLN("---BEGIN LOG---");
  while (f.available()) LOG_WRITE(f.read());
  LOG_PRINTLN("---END LOG---");
  f.close();
}

void dump_boots_to_serial() {
  BootRecord recs[MAX_BOOT_HISTORY];
  size_t n = get_boot_history(recs, MAX_BOOT_HISTORY);
  LOG_PRINTF("[storage] current boot_id=%u, history has %u records\n",
                s_boot_id, (unsigned)n);
  for (size_t i = 0; i < n; ++i) {
    LOG_PRINTF("  boot %u: %u sec\n", recs[i].boot_id, recs[i].duration_sec);
  }
}

void clear_log() {
  if (!lock_log()) return;
  LittleFS.remove(LOG_PATH);
  s_unsynced_count = 0;
  unlock_log();
  LOG_PRINTLN("[storage] log cleared");
}

// ---- Factory reset ----------------------------------------------------------

void request_factory_reset() { s_factory_reset_req = true; }

bool consume_factory_reset_request() {
  if (!s_factory_reset_req) return false;
  s_factory_reset_req = false;
  return true;
}

void factory_reset() {
  // Take the log lock and intentionally never release it: the device reboots
  // right after, and holding it guarantees no task appends to LittleFS while we
  // format — a write caught mid-flight by the reboot is exactly what can leave
  // the filesystem unmountable on the next boot.
  lock_log();

  // Close our own NVS handles so the partition can be deinitialized, then erase
  // the WHOLE default NVS partition — every namespace (cfg, state, relay,
  // health) at once, which also future-proofs this against namespaces added
  // later. After erase the device boots as if freshly flashed — no Wi-Fi
  // creds, default host + log interval, no relay schedule, cleared boot-loop
  // history — with ONE exception: boot_id and the seq high-water mark survive.
  // The server keys readings on (device_id, seq, channel) and silently ignores
  // a duplicate, so restarting seq at 1 would make it drop every new row until
  // seq climbed past the pre-reset maximum (and ack them, so the device would
  // delete them too). Carrying boot_id forward keeps the boot chain monotonic.
  uint32_t keep_boot_id = s_boot_id;
  uint64_t keep_seq_hwm = s_seq_hwm > s_last_seq ? s_seq_hwm : s_last_seq + 1;
  s_cfg.end();
  s_state.end();
  esp_err_t derr = nvs_flash_deinit();
  esp_err_t eerr = nvs_flash_erase();
  esp_err_t ierr = nvs_flash_init();
  bool kept = false;
  if (ierr == ESP_OK && s_state.begin("state", false)) {
    kept = s_state.putUInt("boot_id", keep_boot_id) > 0 &&
           s_state.putULong64("seq_hwm", keep_seq_hwm) > 0;
    s_state.end();
  }
  LOG_PRINTF("[storage] NVS wipe: deinit=%d erase=%d init=%d, kept boot_id=%u seq_hwm=%llu: %s\n",
             (int)derr, (int)eerr, (int)ierr, keep_boot_id,
             (unsigned long long)keep_seq_hwm, kept ? "ok" : "FAILED");

  // Reformat LittleFS for a clean, consistent buffer — more robust than removing
  // individual files, which could leave a partially written file across reboot.
  bool fmt = LittleFS.format();
  s_unsynced_count = 0;
  LOG_PRINTF("[storage] LittleFS format: %d\n", (int)fmt);
  LOG_PRINTLN("[storage] FACTORY RESET complete — rebooting as a fresh device");
}

}  // namespace storage
