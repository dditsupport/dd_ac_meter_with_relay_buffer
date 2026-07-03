#pragma once

// Thread-safe Serial logging.
//
// Why: Serial.printf() on ESP32 is NOT atomic. The sampling and connectivity
// tasks preempt each other on the C3's single core, so without serialisation
// the byte stream of one printf interleaves with another and high-bit bytes
// land mid-line, producing the classic "����������" garbage seen on the
// serial terminal. Wrapping every printf in a mutex serialises writes per
// line.
//
// Use LOG_PRINTF / LOG_PRINTLN exactly like Serial.printf / Serial.println.
// Call log_init() once from setup() AFTER Serial.begin().

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace log_serial {

// Install the mutex AND route ESP-IDF's esp_log_write() through it so that
// internal "wifi: ...", "lwip: ..." chatter no longer interleaves with our
// own Serial.printf output and clobbers entire log lines with high-bit
// garbage. Safe to call once from setup() after Serial.begin().
void init();

class Lock {
 public:
  Lock();
  ~Lock();
 private:
  bool taken_;
};

}  // namespace log_serial

// Macros. The Lock object is a temporary that lives across the full
// argument list of the Serial call.
#define LOG_PRINTF(...)  do { ::log_serial::Lock _llk; Serial.printf(__VA_ARGS__);  } while (0)
#define LOG_PRINTLN(...) do { ::log_serial::Lock _llk; Serial.println(__VA_ARGS__); } while (0)
#define LOG_PRINT(...)   do { ::log_serial::Lock _llk; Serial.print(__VA_ARGS__);   } while (0)
#define LOG_WRITE(...)   do { ::log_serial::Lock _llk; Serial.write(__VA_ARGS__);   } while (0)
