#include "rtc.h"
#include "config.h"

#include <Wire.h>
#include <RTClib.h>
#include "log_serial.h"

namespace rtc {

static RTC_DS1307 s_rtc;
static bool s_available = false;
static bool s_lost_power_at_boot = false;

bool begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  if (!s_rtc.begin()) {
    LOG_PRINTLN("[rtc] DS1307 not found on I2C");
    s_available = false;
    return false;
  }
  // The DS1307 halts its oscillator (CH bit set) whenever it loses power with
  // no backup battery. isRunning() reflects that bit, so a stopped clock means
  // the retained time is meaningless until we resync.
  s_lost_power_at_boot = !s_rtc.isrunning();
  if (s_lost_power_at_boot) {
    LOG_PRINTLN("[rtc] DS1307 oscillator stopped; time unknown until NTP/BLE sync");
    s_available = false;  // present, but time not trustworthy
    return false;
  }
  s_available = true;
  return true;
}

bool available() { return s_available; }
bool lost_power() { return s_lost_power_at_boot; }

time_t read_epoch() {
  if (!s_available) return 0;
  DateTime now = s_rtc.now();
  if (!now.isValid()) return 0;
  return now.unixtime();
}

bool write_epoch(time_t epoch) {
  if (epoch < 1700000000) return false;
  DateTime dt((uint32_t)epoch);
  s_rtc.adjust(dt);
  // RTClib's adjust() clears the CH (clock-halt) bit, restarting the
  // oscillator if it had stopped after a power loss.
  s_available = true;
  s_lost_power_at_boot = false;
  return true;
}

}  // namespace rtc
