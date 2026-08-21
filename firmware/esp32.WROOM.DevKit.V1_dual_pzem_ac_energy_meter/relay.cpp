#include "relay.h"
#include "config.h"
#include "time_source.h"
#include "log_serial.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

namespace relay {

// GPIO per relay. Index == PZEM channel index (0 == "relay 1" / "PZEM 1").
static const int s_pin[RELAY_COUNT] = {
  PIN_RELAY1,
#if RELAY_COUNT > 1
  PIN_RELAY2,
#endif
};

// Cutoff state machine (per off-hours period, per relay).
//   ALLOWED     - inside open hours: de-energized (AC on).
//   MONITORING  - off hours: de-energized while the compressor is still running
//                 (wattage >= threshold) so we never open under load. Cuts the
//                 moment wattage drops below the threshold, or at the grace
//                 deadline at the latest (hard-cut a still-running AC).
//   CUT_LATCHED - relay energized (AC cut) and latched for the rest of the
//                 off-hours period (PZEM now reads ~0, must not re-open).
enum class SmState : uint8_t { ALLOWED, MONITORING, CUT_LATCHED };

// Everything that was a file static in the single-relay build is now per-relay,
// so the two relays cannot influence each other's decisions.
struct RelayState {
  String   schedule_json    = "[]";
  uint32_t version          = 0;
  uint32_t compressor_watts = RELAY_COMPRESSOR_WATTS_DEFAULT;
  uint32_t grace_min        = RELAY_GRACE_MIN_DEFAULT;

  bool     energized = false;          // true = energized (AC cut)
  Mode     mode      = Mode::AUTO;     // RAM-only manual override

  float    latest_power_w = 0.0f;      // from THIS relay's PZEM channel
  bool     power_valid    = false;

  SmState  sm          = SmState::ALLOWED;
  uint64_t offhours_us  = 0;           // monotonic-us when off hours began
};

static RelayState s_r[RELAY_COUNT];
static bool s_initialised = false;

static inline bool valid_r(uint8_t r) { return r < RELAY_COUNT; }

static inline void write_pin(uint8_t r, bool on) {
#if RELAY_ACTIVE_HIGH
  digitalWrite(s_pin[r], on ? HIGH : LOW);
#else
  digitalWrite(s_pin[r], on ? LOW : HIGH);
#endif
  s_r[r].energized = on;
}

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static const char *sm_str(SmState s) {
  switch (s) {
    case SmState::MONITORING:  return "monitoring";
    case SmState::CUT_LATCHED: return "cut";
    default:                   return "allowed";
  }
}

// NVS keys are suffixed with the relay index ("sched0"/"sched1", ...) so each
// relay keeps its own cached config. NVS keys are capped at 15 chars.
static void rkey(char *out, size_t n, const char *base, uint8_t r) {
  snprintf(out, n, "%s%u", base, (unsigned)r);
}

uint8_t count() { return RELAY_COUNT; }

void begin() {
  Preferences p;
  p.begin("relay", true);  // read-only first
  for (uint8_t r = 0; r < RELAY_COUNT; ++r) {
    pinMode(s_pin[r], OUTPUT);
    write_pin(r, false);   // fail-safe: de-energized = AC on at boot

    char k[16];
    rkey(k, sizeof(k), "sched", r); s_r[r].schedule_json = p.getString(k, "[]");
    rkey(k, sizeof(k), "ver",   r); s_r[r].version       = p.getUInt(k, 0);
    rkey(k, sizeof(k), "cw",    r);
    s_r[r].compressor_watts = clamp_u32(p.getUInt(k, RELAY_COMPRESSOR_WATTS_DEFAULT),
                                        RELAY_COMPRESSOR_WATTS_MIN,
                                        RELAY_COMPRESSOR_WATTS_MAX);
    rkey(k, sizeof(k), "gm",    r);
    s_r[r].grace_min = clamp_u32(p.getUInt(k, RELAY_GRACE_MIN_DEFAULT),
                                 RELAY_GRACE_MIN_MIN, RELAY_GRACE_MIN_MAX);
    s_r[r].sm = SmState::ALLOWED;
  }
  p.end();
  s_initialised = true;

  for (uint8_t r = 0; r < RELAY_COUNT; ++r) {
    LOG_PRINTF("[relay%u] boot pin=%d v=%u cw=%uW grace=%umin sched=%s\n",
               (unsigned)(r + 1), s_pin[r], (unsigned)s_r[r].version,
               (unsigned)s_r[r].compressor_watts, (unsigned)s_r[r].grace_min,
               s_r[r].schedule_json.c_str());
  }
}

void update_power(uint8_t r, float watts, bool valid) {
  if (!valid_r(r)) return;
  s_r[r].latest_power_w = watts;
  s_r[r].power_valid    = valid;
}

void apply(uint8_t r, uint32_t version, const String &schedule_json_array,
           uint32_t compressor_watts, uint32_t grace_min) {
  if (!valid_r(r)) return;
  RelayState &st = s_r[r];

  String   new_sched = schedule_json_array.length() ? schedule_json_array : "[]";
  uint32_t new_cw    = compressor_watts
                         ? clamp_u32(compressor_watts, RELAY_COMPRESSOR_WATTS_MIN,
                                     RELAY_COMPRESSOR_WATTS_MAX)
                         : st.compressor_watts;   // 0 = leave unchanged
  uint32_t new_gm    = grace_min
                         ? clamp_u32(grace_min, RELAY_GRACE_MIN_MIN, RELAY_GRACE_MIN_MAX)
                         : st.grace_min;

  // Skip if nothing changed.
  if (version == st.version && new_sched == st.schedule_json &&
      new_cw == st.compressor_watts && new_gm == st.grace_min) return;

  st.schedule_json    = new_sched;
  st.version          = version;
  st.compressor_watts = new_cw;
  st.grace_min        = new_gm;

  Preferences p;
  p.begin("relay", false);
  char k[16];
  rkey(k, sizeof(k), "sched", r); p.putString(k, st.schedule_json);
  rkey(k, sizeof(k), "ver",   r); p.putUInt(k, st.version);
  rkey(k, sizeof(k), "cw",    r); p.putUInt(k, st.compressor_watts);
  rkey(k, sizeof(k), "gm",    r); p.putUInt(k, st.grace_min);
  p.end();
  LOG_PRINTF("[relay%u] config updated v=%u cw=%uW grace=%umin: %s\n",
             (unsigned)(r + 1), (unsigned)st.version,
             (unsigned)st.compressor_watts, (unsigned)st.grace_min,
             st.schedule_json.c_str());
  // The next loop() tick (<=50 ms) re-evaluates and drives the GPIO. We do NOT
  // call tick() here: apply() runs in the connectivity task, and tick() (the
  // stateful cutoff machine) is driven solely by loop() so its state is never
  // mutated from two tasks at once. We also deliberately do not reset the
  // machine, so a latched CUT stays cut rather than briefly re-powering the AC
  // when a config is pushed mid-night.
}

uint32_t version(uint8_t r) { return valid_r(r) ? s_r[r].version   : 0; }
bool     is_on(uint8_t r)   { return valid_r(r) ? s_r[r].energized : false; }
Mode     mode(uint8_t r)    { return valid_r(r) ? s_r[r].mode      : Mode::AUTO; }

const char *mode_str(uint8_t r) {
  if (!valid_r(r)) return "auto";
  switch (s_r[r].mode) {
    case Mode::FORCE_ON:  return "on";
    case Mode::FORCE_OFF: return "off";
    default:              return "auto";
  }
}

void set_mode(uint8_t r, Mode m) {
  if (!valid_r(r) || m == s_r[r].mode) return;
  s_r[r].mode = m;
  LOG_PRINTF("[relay%u] mode -> %s\n", (unsigned)(r + 1), mode_str(r));
  // The next loop() tick (<=50 ms) applies it; tick() is driven only by loop()
  // so the cutoff state machine is never mutated from the BLE-callback task.
}

String status_json() {
  // Kept deliberately SMALL. This value is pushed over a BLE NOTIFY, which is
  // capped at MTU-3 (~244 bytes) and TRUNCATES rather than fragmenting — so a
  // fat payload would silently corrupt the JSON as soon as there is more than
  // one relay. Only live state is sent; the config echoes (state-machine name,
  // compressor threshold, grace minutes, schedule version) are omitted because
  // the app never renders them and they are already in the ingest POST.
  StaticJsonDocument<128 + RELAY_COUNT * 96> doc;
  JsonArray arr = doc.createNestedArray("relays");
  for (uint8_t r = 0; r < RELAY_COUNT; ++r) {
    JsonObject o = arr.createNestedObject();
    o["ch"]        = r + 1;              // 1-based on the wire
    o["mode"]      = mode_str(r);
    o["energized"] = s_r[r].energized;   // true = AC cut
    o["latest_w"]  = s_r[r].power_valid ? (int)(s_r[r].latest_power_w + 0.5f) : -1;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Parse "HH:MM" into minutes-of-day (0..1439). Returns -1 on malformed.
static int parse_hm(const char *s) {
  if (!s || strlen(s) != 5 || s[2] != ':') return -1;
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

// True if `dow` (0..6) is listed in the window's days array.
static bool day_in(JsonArray days, int dow) {
  for (JsonVariant d : days) {
    if (d.as<int>() == dow) return true;
  }
  return false;
}

// True if relay `r` has at least one usable window. An empty schedule means
// "unconfigured" -> AC always allowed (fail-safe, never cut).
static bool schedule_configured(uint8_t r) {
  return s_initialised && s_r[r].schedule_json.length() > 2;
}

// Returns whether AC is ALLOWED (open hours) for relay `r` at (dow, minute)
// given its cached schedule. dow: 0..6 (Sun..Sat). minute: 0..1439.
//
// Window semantics: allowed at `on`, ends at `off`, on the selected weekdays.
//   - Same-day window (on < off): allowed [on, off) on each selected day.
//   - Overnight window (off < on): runs past midnight into the NEXT day (e.g.
//     open 09:00 close 02:00 with Mon selected => allowed Mon 09:00 through
//     Tue 02:00). The selected days are the START days.
// Multiple windows OR together.
static bool ac_allowed(uint8_t r, int dow, int minute) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, s_r[r].schedule_json)) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;

  int prev = (dow + 6) % 7;  // yesterday, for overnight tails
  for (JsonObject w : arr) {
    JsonArray days = w["days"].as<JsonArray>();
    int on  = parse_hm(w["on"]  | (const char *)nullptr);
    int off = parse_hm(w["off"] | (const char *)nullptr);
    if (on < 0 || off < 0 || on == off) continue;

    if (on < off) {
      if (day_in(days, dow) && minute >= on && minute < off) return true;
    } else {
      if (day_in(days, dow)  && minute >= on)  return true;  // evening, start day
      if (day_in(days, prev) && minute <  off) return true;  // morning, next day
    }
  }
  return false;
}

// Energize relay `r` to cut AC and latch for the rest of off-hours.
static void cut_and_latch(uint8_t r, const char *why) {
  if (!s_r[r].energized) {
    write_pin(r, true);
    LOG_PRINTF("[relay%u] CUT (%s)\n", (unsigned)(r + 1), why);
  }
  s_r[r].sm = SmState::CUT_LATCHED;
}

// De-energize relay `r` (restore AC) and go to ALLOWED.
static void restore_and_allow(uint8_t r) {
  if (s_r[r].energized) {
    write_pin(r, false);
    LOG_PRINTF("[relay%u] restore AC (open hours)\n", (unsigned)(r + 1));
  }
  s_r[r].sm = SmState::ALLOWED;
}

// Drive ONE relay. Called for every relay from tick().
static void tick_one(uint8_t r, time_t now, const struct tm &lt, bool clock_ok) {
  RelayState &st = s_r[r];

  // Manual override from the app takes precedence over the schedule and needs
  // no wall clock. FORCE_ON = energize (cut), FORCE_OFF = de-energize (AC on).
  if (st.mode == Mode::FORCE_ON || st.mode == Mode::FORCE_OFF) {
    bool want = (st.mode == Mode::FORCE_ON);
    if (want != st.energized) {
      write_pin(r, want);
      LOG_PRINTF("[relay%u] %s (manual override)\n",
                 (unsigned)(r + 1), want ? "CUT" : "AC on");
    }
    return;
  }

  // Unconfigured relay, or wall clock not yet known: fail-safe AC on.
  if (!schedule_configured(r) || !clock_ok) {
    if (st.energized) write_pin(r, false);
    st.sm = SmState::ALLOWED;
    return;
  }

  int dow    = lt.tm_wday;                   // 0=Sun..6=Sat
  int minute = lt.tm_hour * 60 + lt.tm_min;

  if (ac_allowed(r, dow, minute)) {
    // Inside open hours — make sure the AC is powered.
    if (st.sm != SmState::ALLOWED || st.energized) restore_and_allow(r);
    return;
  }

  // ---- Off hours ----
  uint64_t now_us = time_source::monotonic_us();
  if (st.sm == SmState::ALLOWED) {
    // Just crossed into off hours: start the grace clock and begin monitoring.
    st.offhours_us = now_us;
    st.sm          = SmState::MONITORING;
    LOG_PRINTF("[relay%u] off-hours begin %02d:%02d — monitoring (cw=%uW grace=%umin)\n",
               (unsigned)(r + 1), lt.tm_hour, lt.tm_min,
               (unsigned)st.compressor_watts, (unsigned)st.grace_min);
  }

  bool grace_elapsed = (now_us - st.offhours_us) >=
                       (uint64_t)st.grace_min * 60ULL * 1000000ULL;
  // The compressor is idle only when a FRESH reading from THIS relay's own
  // channel is below the threshold. An invalid/stale reading counts as "still
  // running" so we never hard-cut a possibly-loaded compressor before the
  // grace deadline.
  bool idle = st.power_valid && st.latest_power_w < (float)st.compressor_watts;

  switch (st.sm) {
    case SmState::MONITORING:
      if (idle) {
        cut_and_latch(r, "power below threshold");  // compressor off — safe now
      } else if (grace_elapsed) {
        cut_and_latch(r, "grace deadline");         // still drawing — hard-cut
      }
      break;

    case SmState::CUT_LATCHED:
      if (!st.energized) write_pin(r, true);   // hold cut (defensive)
      break;

    default:
      break;   // stay de-energized until open hours resume
  }
}

void tick() {
  if (!s_initialised) return;

  // Resolve wall clock once for all relays.
  time_t now = time(nullptr);
  bool clock_ok = (now >= 1700000000);
  struct tm lt{};
  if (clock_ok) localtime_r(&now, &lt);

  for (uint8_t r = 0; r < RELAY_COUNT; ++r) tick_one(r, now, lt, clock_ok);
}

}  // namespace relay
