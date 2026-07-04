#include "relay.h"
#include "config.h"
#include "time_source.h"
#include "log_serial.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

namespace relay {

// Cached config. Parsed lazily on tick(); we keep the raw schedule string in
// NVS so cutoff survives reboots even before the first sync.
static String   s_schedule_json    = "[]";
static uint32_t s_version          = 0;
static uint32_t s_compressor_watts = RELAY_COMPRESSOR_WATTS_DEFAULT;
static uint32_t s_grace_min        = RELAY_GRACE_MIN_DEFAULT;

static bool s_state       = false;   // true = energized (AC cut)
static bool s_initialised = false;
static Mode s_mode        = Mode::AUTO;  // RAM-only manual override

// Latest PZEM wattage, fed at 1 Hz by the sampler.
static float s_latest_power_w = 0.0f;
static bool  s_power_valid    = false;

// Cutoff state machine (per off-hours period).
//   ALLOWED       - inside open hours: de-energized (AC on).
//   MONITORING    - off hours, watching whether the AC was left running.
//   WAIT_FOR_IDLE - AC left running; de-energized, waiting for the compressor
//                   to cycle off before cutting.
//   IDLE_DONE     - AC deemed already-off at closing; stay de-energized.
//   CUT_LATCHED   - relay energized (AC cut) and latched for the rest of the
//                   off-hours period (PZEM now reads ~0, must not re-open).
enum class SmState : uint8_t { ALLOWED, MONITORING, WAIT_FOR_IDLE, IDLE_DONE, CUT_LATCHED };
static SmState  s_sm            = SmState::ALLOWED;
static uint64_t s_offhours_us   = 0;   // monotonic-us when off hours began

static inline void write_pin(bool on) {
#if RELAY_ACTIVE_HIGH
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
#else
  digitalWrite(PIN_RELAY, on ? LOW : HIGH);
#endif
  s_state = on;
}

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static const char *sm_str(SmState s) {
  switch (s) {
    case SmState::MONITORING:    return "monitoring";
    case SmState::WAIT_FOR_IDLE: return "wait_idle";
    case SmState::IDLE_DONE:     return "idle_done";
    case SmState::CUT_LATCHED:   return "cut";
    default:                     return "allowed";
  }
}

void begin() {
  pinMode(PIN_RELAY, OUTPUT);
  write_pin(false);  // fail-safe: de-energized = AC on at boot

  Preferences p;
  p.begin("relay", true);  // read-only first
  s_schedule_json    = p.getString("sched", "[]");
  s_version          = p.getUInt("ver",   0);
  s_compressor_watts = clamp_u32(p.getUInt("cw", RELAY_COMPRESSOR_WATTS_DEFAULT),
                                 RELAY_COMPRESSOR_WATTS_MIN, RELAY_COMPRESSOR_WATTS_MAX);
  s_grace_min        = clamp_u32(p.getUInt("gm", RELAY_GRACE_MIN_DEFAULT),
                                 RELAY_GRACE_MIN_MIN, RELAY_GRACE_MIN_MAX);
  p.end();
  s_sm          = SmState::ALLOWED;
  s_initialised = true;
  LOG_PRINTF("[relay] boot v=%u cw=%uW grace=%umin sched=%s\n",
                (unsigned)s_version, (unsigned)s_compressor_watts,
                (unsigned)s_grace_min, s_schedule_json.c_str());
}

void update_power(float watts, bool valid) {
  s_latest_power_w = watts;
  s_power_valid    = valid;
}

void apply(uint32_t version, const String &schedule_json_array,
           uint32_t compressor_watts, uint32_t grace_min) {
  String   new_sched = schedule_json_array.length() ? schedule_json_array : "[]";
  uint32_t new_cw    = compressor_watts
                         ? clamp_u32(compressor_watts, RELAY_COMPRESSOR_WATTS_MIN,
                                     RELAY_COMPRESSOR_WATTS_MAX)
                         : s_compressor_watts;   // 0 = leave unchanged
  uint32_t new_gm    = grace_min
                         ? clamp_u32(grace_min, RELAY_GRACE_MIN_MIN, RELAY_GRACE_MIN_MAX)
                         : s_grace_min;

  // Skip if nothing changed.
  if (version == s_version && new_sched == s_schedule_json &&
      new_cw == s_compressor_watts && new_gm == s_grace_min) return;

  s_schedule_json    = new_sched;
  s_version          = version;
  s_compressor_watts = new_cw;
  s_grace_min        = new_gm;

  Preferences p;
  p.begin("relay", false);
  p.putString("sched", s_schedule_json);
  p.putUInt("ver", s_version);
  p.putUInt("cw",  s_compressor_watts);
  p.putUInt("gm",  s_grace_min);
  p.end();
  LOG_PRINTF("[relay] config updated v=%u cw=%uW grace=%umin: %s\n",
                (unsigned)s_version, (unsigned)s_compressor_watts,
                (unsigned)s_grace_min, s_schedule_json.c_str());
  // The next loop() tick (<=50 ms) re-evaluates and drives the GPIO. We do NOT
  // call tick() here: apply() runs in the connectivity task, and tick() (the
  // stateful cutoff machine) is driven solely by loop() so its state is never
  // mutated from two tasks at once. We also deliberately do not reset the
  // machine, so a latched CUT stays cut rather than briefly re-powering the AC
  // when a config is pushed mid-night.
}

uint32_t version() { return s_version; }
bool     is_on()   { return s_state; }
Mode     mode()    { return s_mode; }

const char *mode_str() {
  switch (s_mode) {
    case Mode::FORCE_ON:  return "on";
    case Mode::FORCE_OFF: return "off";
    default:              return "auto";
  }
}

void set_mode(Mode m) {
  if (m == s_mode) return;
  s_mode = m;
  LOG_PRINTF("[relay] mode -> %s\n", mode_str());
  // The next loop() tick (<=50 ms) applies it; tick() is driven only by loop()
  // so the cutoff state machine is never mutated from the BLE-callback task.
}

String status_json() {
  StaticJsonDocument<192> doc;
  doc["mode"]            = mode_str();
  doc["energized"]       = s_state;          // true = AC cut
  doc["sm"]              = sm_str(s_sm);
  doc["latest_w"]        = s_power_valid ? (int)(s_latest_power_w + 0.5f) : -1;
  doc["compressor_w"]    = s_compressor_watts;
  doc["grace_min"]       = s_grace_min;
  doc["sched_version"]   = s_version;
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

// True if the cached schedule has at least one usable window. An empty
// schedule means "unconfigured" -> AC always allowed (fail-safe, never cut).
static bool schedule_configured() {
  return s_initialised && s_schedule_json.length() > 2;
}

// Returns whether AC is ALLOWED (open hours) at (dow, minute) given the cached
// schedule. dow: 0..6 (Sun..Sat). minute: minute-of-day 0..1439.
//
// Window semantics: allowed at `on`, ends at `off`, on the selected weekdays.
//   - Same-day window (on < off): allowed [on, off) on each selected day.
//   - Overnight window (off < on): runs past midnight into the NEXT day (e.g.
//     open 09:00 close 02:00 with Mon selected => allowed Mon 09:00 through
//     Tue 02:00). The selected days are the START days.
// Multiple windows OR together.
static bool ac_allowed(int dow, int minute) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, s_schedule_json)) return false;
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

// Energize the relay to cut AC and latch for the rest of off-hours.
static void cut_and_latch(const char *why) {
  if (!s_state) {
    write_pin(true);
    LOG_PRINTF("[relay] CUT (%s)\n", why);
  }
  s_sm = SmState::CUT_LATCHED;
}

// De-energize (restore AC) and go to ALLOWED.
static void restore_and_allow() {
  if (s_state) {
    write_pin(false);
    LOG_PRINTLN("[relay] restore AC (open hours)");
  }
  s_sm = SmState::ALLOWED;
}

void tick() {
  if (!s_initialised) return;

  // Manual override from the app takes precedence over the schedule and needs
  // no wall clock. FORCE_ON = energize (cut), FORCE_OFF = de-energize (AC on).
  if (s_mode == Mode::FORCE_ON || s_mode == Mode::FORCE_OFF) {
    bool want = (s_mode == Mode::FORCE_ON);
    if (want != s_state) {
      write_pin(want);
      LOG_PRINTF("[relay] %s (manual override)\n", want ? "CUT" : "AC on");
    }
    return;
  }

  // Unconfigured device, or wall clock not yet known: fail-safe AC on.
  time_t now = time(nullptr);
  if (!schedule_configured() || now < 1700000000) {
    if (s_state) write_pin(false);
    s_sm = SmState::ALLOWED;
    return;
  }

  struct tm lt;
  localtime_r(&now, &lt);
  int dow    = lt.tm_wday;                   // 0=Sun..6=Sat
  int minute = lt.tm_hour * 60 + lt.tm_min;

  if (ac_allowed(dow, minute)) {
    // Inside open hours — make sure the AC is powered.
    if (s_sm != SmState::ALLOWED || s_state) restore_and_allow();
    return;
  }

  // ---- Off hours ----
  uint64_t now_us = time_source::monotonic_us();
  if (s_sm == SmState::ALLOWED) {
    // Just crossed into off hours: start the grace clock and begin monitoring.
    s_offhours_us = now_us;
    s_sm          = SmState::MONITORING;
    LOG_PRINTF("[relay] off-hours begin %02d:%02d — monitoring (cw=%uW grace=%umin)\n",
                  lt.tm_hour, lt.tm_min, (unsigned)s_compressor_watts,
                  (unsigned)s_grace_min);
  }

  bool     grace_elapsed = (now_us - s_offhours_us) >=
                           (uint64_t)s_grace_min * 60ULL * 1000000ULL;
  bool     running       = s_power_valid && s_latest_power_w >= (float)s_compressor_watts;

  switch (s_sm) {
    case SmState::MONITORING:
      if (running) {
        s_sm = SmState::WAIT_FOR_IDLE;
        LOG_PRINTLN("[relay] AC left running — waiting for compressor to cycle off");
      } else if (grace_elapsed) {
        s_sm = SmState::IDLE_DONE;   // AC already off at closing; nothing to cut
        LOG_PRINTLN("[relay] AC idle through grace window — no cut needed");
      }
      break;

    case SmState::WAIT_FOR_IDLE:
      if (!running) {
        cut_and_latch("compressor idle");
      } else if (grace_elapsed) {
        cut_and_latch("grace deadline");
      }
      break;

    case SmState::CUT_LATCHED:
      if (!s_state) write_pin(true);   // hold cut (defensive)
      break;

    case SmState::IDLE_DONE:
    default:
      break;   // stay de-energized until open hours resume
  }
}

}  // namespace relay
