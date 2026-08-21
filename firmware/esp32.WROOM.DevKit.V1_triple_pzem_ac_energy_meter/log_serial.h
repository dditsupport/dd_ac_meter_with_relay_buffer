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

#include "config.h"   // DEBUG_SERIAL

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
//
// With DEBUG_SERIAL == 0 they expand to nothing at all. In the triple-PZEM
// build that is mandatory, not an optimisation: UART0 is PZEM-3's Modbus bus,
// so a stray print would land inside a Modbus transaction and corrupt the
// reply. They discard their arguments entirely: the codebase calls LOG_PRINTLN()
// with NO arguments, so a sizeof-based trick on an empty pack would not compile.
#if DEBUG_SERIAL
#define LOG_PRINTF(...)  do { ::log_serial::Lock _llk; Serial.printf(__VA_ARGS__);  } while (0)
#define LOG_PRINTLN(...) do { ::log_serial::Lock _llk; Serial.println(__VA_ARGS__); } while (0)
#define LOG_PRINT(...)   do { ::log_serial::Lock _llk; Serial.print(__VA_ARGS__);   } while (0)
#define LOG_WRITE(...)   do { ::log_serial::Lock _llk; Serial.write(__VA_ARGS__);   } while (0)
#else
#define LOG_PRINTF(...)  do { } while (0)
#define LOG_PRINTLN(...) do { } while (0)
#define LOG_PRINT(...)   do { } while (0)
#define LOG_WRITE(...)   do { } while (0)
#endif
