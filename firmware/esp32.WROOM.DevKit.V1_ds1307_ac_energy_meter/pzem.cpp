#include "pzem.h"
#include "config.h"
#include "time_source.h"

#include <PZEM004Tv30.h>
#include <math.h>
#include "log_serial.h"

namespace pzem {

static PZEM004Tv30 *s_pzem = nullptr;
static uint8_t s_fail_streak = 0;
static uint64_t s_low_v_start_us = 0;
static bool s_low_v_active = false;

// ---------- Plausibility gate (see PZEM_* limits in config.h) ----------
// Last ACCEPTED energy reading, for the rate-of-rise check, and a candidate
// for a jump that has to repeat PZEM_JUMP_CONFIRM_READS times to be believed.
static bool     s_e_have      = false;
static float    s_e_last_wh   = 0.0f;
static uint64_t s_e_last_us   = 0;
static float    s_e_cand_wh   = 0.0f;
static uint8_t  s_e_cand_n    = 0;
// Serial log is rate-limited: a bad link can reject every sample.
static uint32_t s_reject_logged_ms = 0;
static uint32_t s_reject_suppressed = 0;

static void log_reject(const char *why, float v, float i, float p, float e_wh,
                       float pf, float f) {
  uint32_t now = millis();
  if (s_reject_logged_ms != 0 && now - s_reject_logged_ms < 60000UL) {
    s_reject_suppressed++;
    return;
  }
  LOG_PRINTF("[pzem] implausible reading rejected (%s): V=%.1f I=%.3f P=%.1f "
             "Wh=%.0f PF=%.2f Hz=%.1f [+%u more in the last minute]\n",
             why, v, i, p, e_wh, pf, f, (unsigned)s_reject_suppressed);
  s_reject_logged_ms = now ? now : 1;
  s_reject_suppressed = 0;
}

// Range checks on one reading. nullptr = plausible, else a short reason.
static const char *range_reason(float v, float i, float p, float e_wh,
                                float pf, float f) {
  if (v < 0.0f || v > PZEM_V_MAX)               return "voltage";
  if (i < 0.0f || i > PZEM_I_MAX_A)             return "current";
  if (p < 0.0f || p > PZEM_P_MAX_W)             return "power";
  // Real power can never exceed apparent power. 5 % + 2 W covers the PZEM's
  // rounding (0.1 V, 1 mA, 0.1 W) at small loads.
  if (p > v * i * 1.05f + 2.0f)                 return "power > V*I";
  if (pf < 0.0f || pf > 1.001f)                 return "power factor";
  if (e_wh < 0.0f || e_wh > PZEM_E_MAX_WH)      return "energy range";
  // Frequency is only meaningful with mains present.
  if (v >= SENSOR_LOW_V_THRESHOLD && (f < PZEM_HZ_MIN || f > PZEM_HZ_MAX))
                                                return "frequency";
  return nullptr;
}

// Rate-of-rise check on the energy counter. Accepting updates the reference.
static const char *energy_reason(float e_wh, uint64_t now_us) {
  if (s_e_have && e_wh > s_e_last_wh) {
    float dt_h = (float)((now_us - s_e_last_us) / 1000ULL) / 3600000.0f;
    float allow_wh = PZEM_P_MAX_W * dt_h + 50.0f;
    if (e_wh - s_e_last_wh > allow_wh) {
      if (s_e_cand_n > 0 && fabsf(e_wh - s_e_cand_wh) <= 1000.0f) {
        s_e_cand_n++;
      } else {
        s_e_cand_wh = e_wh;
        s_e_cand_n  = 1;
      }
      if (s_e_cand_n < PZEM_JUMP_CONFIRM_READS) return "energy jump";
      // Repeated often enough: a real swap to a used meter. Fall through.
    }
  }
  s_e_cand_n  = 0;
  s_e_have    = true;
  s_e_last_wh = e_wh;
  s_e_last_us = now_us;
  return nullptr;
}

void begin() {
  // PZEM-004T-v30 (mandulaj) takes (HardwareSerial&, rxPin, txPin) and runs
  // Serial2.begin() internally — no need for an external begin() call.
  // rxPin = ESP32's RX (GPIO 16, connected to PZEM TX);
  // txPin = ESP32's TX (GPIO 17, connected to PZEM RX).
  s_pzem = new PZEM004Tv30(Serial2, PIN_PZEM_RX, PIN_PZEM_TX);
}

bool read(PzemSample &out) {
  if (!s_pzem) return false;
  // Retry a missed Modbus transaction a couple times before failing. Each
  // s_pzem->voltage() call refreshes all registers in one transaction, so the
  // inter-try delay (longer than the library's value cache) makes the retry a
  // genuinely fresh read rather than the same cached NaN.
  for (int attempt = 0; attempt < PZEM_READ_ATTEMPTS; ++attempt) {
    if (attempt > 0) delay(PZEM_READ_RETRY_MS);
    float v = s_pzem->voltage();
    float i = s_pzem->current();
    float p = s_pzem->power();
    float e = s_pzem->energy();
    float pf = s_pzem->pf();
    float f = s_pzem->frequency();
    if (isnan(v) || isnan(i) || isnan(p) || isnan(e) || isnan(pf) || isnan(f)) {
      continue;
    }
    float e_wh = e * 1000.0f;  // library returns kWh
    // Garbage off a noisy link: retry like a missed transaction.
    if (const char *why = range_reason(v, i, p, e_wh, pf, f)) {
      log_reject(why, v, i, p, e_wh, pf, f);
      continue;
    }
    // A counter jump is judged once per sample (not per retry), so it takes
    // PZEM_JUMP_CONFIRM_READS separate samples to accept a genuine swap.
    if (const char *why = energy_reason(e_wh, time_source::monotonic_us())) {
      log_reject(why, v, i, p, e_wh, pf, f);
      return false;
    }
    out.voltage = v;
    out.current = i;
    out.power = p;
    out.energy_wh = e_wh;
    out.pf = pf;
    out.frequency = f;
    return true;
  }
  return false;
}

PzemStatus classify(bool ok, const PzemSample &sample) {
  if (!ok) {
    if (s_fail_streak < 255) s_fail_streak++;
    if (s_fail_streak >= PZEM_FAIL_THRESHOLD) return PZEM_STALE;
    // Below threshold, hold previous classification by returning PZEM_OK
    // (caller keeps last-known sample on screen).
    return PZEM_OK;
  }
  s_fail_streak = 0;

  // Voltage-based sensor-fault detection (distinguish broken vs night).
  uint64_t now = time_source::monotonic_us();
  if (sample.voltage < SENSOR_LOW_V_THRESHOLD) {
    if (!s_low_v_active) {
      s_low_v_active = true;
      s_low_v_start_us = now;
    }
    uint64_t dur_us = now - s_low_v_start_us;
    if (dur_us >= (uint64_t)SENSOR_FAULT_WINDOW_SEC * 1000000ULL) {
      return PZEM_SENSOR_FAULT;
    }
  } else {
    s_low_v_active = false;
  }
  return PZEM_OK;
}

bool reset_energy() {
  if (!s_pzem) return false;
  // The reset is a single Modbus command that misses on a flaky bus just like a
  // read, so retry it a few times before giving up.
  for (int attempt = 0; attempt < PZEM_READ_ATTEMPTS; ++attempt) {
    if (attempt > 0) delay(PZEM_READ_RETRY_MS);
    if (s_pzem->resetEnergy()) return true;
  }
  return false;
}

static volatile bool s_reset_requested = false;
void request_reset() { s_reset_requested = true; }
bool consume_reset_request() {
  if (!s_reset_requested) return false;
  s_reset_requested = false;
  return true;
}

}  // namespace pzem
