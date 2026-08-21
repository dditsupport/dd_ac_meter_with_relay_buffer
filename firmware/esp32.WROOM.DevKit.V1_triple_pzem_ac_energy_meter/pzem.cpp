#include "pzem.h"
#include "config.h"
#include "time_source.h"

#include <PZEM004Tv30.h>
#include <math.h>
#include "log_serial.h"

namespace pzem {

// Per-channel wiring, taken straight from config.h. Adding a third meter is a
// matter of bumping PZEM_CHANNELS and adding a row here (UART0 is the console,
// so a third channel would need SoftwareSerial or a different chip).
struct ChannelCfg {
  HardwareSerial *port;
  int rx_pin;
  int tx_pin;
  uint8_t addr;
};

static const ChannelCfg s_cfg[PZEM_CHANNELS] = {
  { &Serial2, PIN_PZEM1_RX, PIN_PZEM1_TX, PZEM1_ADDR },
#if PZEM_CHANNELS > 1
  { &Serial1, PIN_PZEM2_RX, PIN_PZEM2_TX, PZEM2_ADDR },
#endif
#if PZEM_CHANNELS > 2
  // UART0. Only reached when DEBUG_SERIAL == 0 — config.h drops the channel
  // count to 2 whenever the console owns this port, so the two can never both
  // be driving it.
  { &Serial,  PIN_PZEM3_RX, PIN_PZEM3_TX, PZEM3_ADDR },
#endif
};

// Per-channel state. Keeping these in arrays (rather than the single set of
// file statics the one-meter build used) is what stops a fault on one meter
// from being attributed to the other.
static PZEM004Tv30 *s_pzem[PZEM_CHANNELS]        = {nullptr};
static uint8_t      s_fail_streak[PZEM_CHANNELS] = {0};
static uint64_t     s_low_v_start_us[PZEM_CHANNELS] = {0};
static bool         s_low_v_active[PZEM_CHANNELS]   = {false};
static volatile bool s_reset_requested[PZEM_CHANNELS] = {false};

#if PZEM_DEMO_MODE
static float s_demo_energy_wh[PZEM_CHANNELS] = {0.0f};
#endif

uint8_t channel_count() { return PZEM_CHANNELS; }

static inline bool valid_ch(uint8_t ch) { return ch < PZEM_CHANNELS; }

void begin() {
#if PZEM_DEMO_MODE
  LOG_PRINTF("[pzem] DEMO MODE: %u synthetic channels, hardware not queried\n",
             (unsigned)PZEM_CHANNELS);
  return;
#else
  // PZEM004Tv30 (mandulaj) takes (HardwareSerial&, rxPin, txPin, addr) and runs
  // the port's begin() internally — no external begin() call needed. rxPin is
  // the ESP32's RX (wired to the PZEM's TX) and vice versa.
  for (uint8_t ch = 0; ch < PZEM_CHANNELS; ++ch) {
    s_pzem[ch] = new PZEM004Tv30(*s_cfg[ch].port, s_cfg[ch].rx_pin,
                                 s_cfg[ch].tx_pin, s_cfg[ch].addr);
    LOG_PRINTF("[pzem] ch%u on RX=%d TX=%d addr=0x%02X\n",
               (unsigned)(ch + 1), s_cfg[ch].rx_pin, s_cfg[ch].tx_pin,
               s_cfg[ch].addr);
  }
#endif
}

bool read(uint8_t ch, PzemSample &out) {
  if (!valid_ch(ch)) return false;
#if PZEM_DEMO_MODE
  // Synthetic generator, phase-shifted per channel so the two meters don't
  // produce identical curves while bench-testing.
  float t = (float)(time_source::monotonic_us() / 1000000ULL) + ch * 37.0f;
  float i = 4.0f + 3.5f * sinf(t * 0.05f);   // 0.5 .. 7.5 A
  out.voltage   = 230.0f + 1.5f * sinf(t * 0.13f);
  out.current   = i < 0 ? 0 : i;
  out.power     = out.voltage * out.current;
  s_demo_energy_wh[ch] += out.power / 3600.0f;   // ~1 sample/sec assumed
  out.energy_wh = s_demo_energy_wh[ch];
  out.pf        = 0.98f;
  out.frequency = 50.0f;
  return true;
#else
  if (!s_pzem[ch]) return false;
  // Retry a missed Modbus transaction a couple times before failing. Each
  // voltage() call refreshes all registers in one transaction, so the inter-try
  // delay (longer than the library's value cache) makes the retry a genuinely
  // fresh read rather than the same cached NaN.
  for (int attempt = 0; attempt < PZEM_READ_ATTEMPTS; ++attempt) {
    if (attempt > 0) delay(PZEM_READ_RETRY_MS);
    float v  = s_pzem[ch]->voltage();
    float i  = s_pzem[ch]->current();
    float p  = s_pzem[ch]->power();
    float e  = s_pzem[ch]->energy();
    float pf = s_pzem[ch]->pf();
    float f  = s_pzem[ch]->frequency();
    if (isnan(v) || isnan(i) || isnan(p) || isnan(e) || isnan(pf) || isnan(f)) {
      continue;
    }
    out.voltage   = v;
    out.current   = i;
    out.power     = p;
    out.energy_wh = e * 1000.0f;  // library returns kWh
    out.pf        = pf;
    out.frequency = f;
    return true;
  }
  return false;
#endif
}

PzemStatus classify(uint8_t ch, bool ok, const PzemSample &sample) {
  if (!valid_ch(ch)) return PZEM_STALE;
  if (!ok) {
    if (s_fail_streak[ch] < 255) s_fail_streak[ch]++;
    if (s_fail_streak[ch] >= PZEM_FAIL_THRESHOLD) return PZEM_STALE;
    // Below threshold, hold previous classification by returning PZEM_OK
    // (caller keeps the last-known sample).
    return PZEM_OK;
  }
  s_fail_streak[ch] = 0;

  // Voltage-based sensor-fault detection (distinguish broken vs night).
  uint64_t now = time_source::monotonic_us();
  if (sample.voltage < SENSOR_LOW_V_THRESHOLD) {
    if (!s_low_v_active[ch]) {
      s_low_v_active[ch] = true;
      s_low_v_start_us[ch] = now;
    }
    uint64_t dur_us = now - s_low_v_start_us[ch];
    if (dur_us >= (uint64_t)SENSOR_FAULT_WINDOW_SEC * 1000000ULL) {
      return PZEM_SENSOR_FAULT;
    }
  } else {
    s_low_v_active[ch] = false;
  }
  return PZEM_OK;
}

bool reset_energy(uint8_t ch) {
  if (!valid_ch(ch) || !s_pzem[ch]) return false;
  // The reset is a single Modbus command that misses on a flaky bus just like a
  // read, so retry it a few times before giving up.
  for (int attempt = 0; attempt < PZEM_READ_ATTEMPTS; ++attempt) {
    if (attempt > 0) delay(PZEM_READ_RETRY_MS);
    if (s_pzem[ch]->resetEnergy()) return true;
  }
  return false;
}

void request_reset(uint8_t ch) {
  if (ch == PZEM_RESET_ALL) {
    for (uint8_t i = 0; i < PZEM_CHANNELS; ++i) s_reset_requested[i] = true;
  } else if (valid_ch(ch)) {
    s_reset_requested[ch] = true;
  }
}

bool consume_reset_request(uint8_t ch) {
  if (!valid_ch(ch) || !s_reset_requested[ch]) return false;
  s_reset_requested[ch] = false;
  return true;
}

}  // namespace pzem
