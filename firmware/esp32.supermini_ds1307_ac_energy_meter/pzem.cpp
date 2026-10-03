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

void begin() {
  // PZEM-004T-v30 (mandulaj) takes (HardwareSerial&, rxPin, txPin) and runs
  // the port's begin() internally — no need for an external begin() call.
  // The ESP32-C3 has only UART0/UART1, so we use Serial1 (UART0 backs the
  // USB-CDC console). rxPin = C3's RX (GPIO 20, connected to PZEM TX);
  // txPin = C3's TX (GPIO 21, connected to PZEM RX).
  s_pzem = new PZEM004Tv30(Serial1, PIN_PZEM_RX, PIN_PZEM_TX);
}

bool read(PzemSample &out) {
  if (!s_pzem) return false;
  // Retry a missed Modbus transaction a couple times before failing. Each
  // s_pzem->voltage() call refreshes all registers in one transaction (the
  // rest read from that same cached update), so we only need to re-trigger it.
  // The inter-try delay is longer than the library's value cache so the retry
  // is a genuinely fresh read, not the same cached NaN.
  for (int attempt = 0; attempt < PZEM_READ_ATTEMPTS; ++attempt) {
    if (attempt > 0) delay(PZEM_READ_RETRY_MS);
    float v = s_pzem->voltage();
    float i = s_pzem->current();
    float p = s_pzem->power();
    float e = s_pzem->energy();
    float pf = s_pzem->pf();
    float f = s_pzem->frequency();
    if (isnan(v) || isnan(i) || isnan(p) || isnan(e) || isnan(pf) || isnan(f)) {
      continue;  // transaction missed — try again
    }
    out.voltage = v;
    out.current = i;
    out.power = p;
    out.energy_wh = e * 1000.0f;  // library returns kWh
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
