#pragma once

#include <Arduino.h>

namespace ble_service {

void begin();

// Called periodically (e.g. 1 Hz) so the service can refresh dynamic values
// like the Device Info JSON, drive the data-stream pump, and update SharedState
// with current connection status.
void tick();

// True if the radio is currently streaming notifications (informational).
bool is_streaming();

// True if BLE is in a healthy state: either advertising or a client is
// currently connected. The stuck-BLE watchdog in the connectivity task
// reboots the chip if this stays false for too long.
bool is_alive();

// Temporarily stop / restart BLE advertising so the Wi-Fi side can have the
// single ESP32-C3 radio to itself during a connect attempt. On the C3 the
// radio is time-shared with BLE in software, and NimBLE advertising starves
// the 802.11 auth/assoc exchange (disconnect reason 2 = AUTH_EXPIRE) even at a
// strong signal. pause_advertising() only stops advertising; an already-
// connected client link is left untouched. resume_advertising() is a no-op if
// a client is connected (NimBLE re-advertises on its own after a disconnect).
void pause_advertising();
void resume_advertising();

}  // namespace ble_service
