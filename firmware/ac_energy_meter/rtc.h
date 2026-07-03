#pragma once

#include <Arduino.h>
#include <time.h>

namespace rtc {

// Initialize Wire on the configured I2C pins and probe the DS1307.
// Returns true if the chip responded; false if absent or stopped.
bool begin();

// True if a healthy DS1307 was detected at begin() and its oscillator is
// running (clock-halt bit clear or already cleared by a prior write).
bool available();

// Read the current time from the DS1307 as a Unix epoch (UTC, assuming the
// RTC was previously written with UTC). Returns 0 on failure or if not
// available.
time_t read_epoch();

// Write a Unix epoch (UTC) into the DS1307. Returns true on success.
// Clears the CH (clock-halt) bit so the oscillator runs and future reads
// are considered valid.
bool write_epoch(time_t epoch);

// True if the DS1307 was found with its oscillator stopped at boot (CH bit
// set after a power loss). Useful at boot to decide whether to trust
// read_epoch().
bool lost_power();

}  // namespace rtc
