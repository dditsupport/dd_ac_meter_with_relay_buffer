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

// True while a client (the app) is currently connected over BLE. Used to defer
// the BLE->Wi-Fi handoff shutdown so an in-progress app session isn't cut off;
// the handoff waits for the app to disconnect. Safe no-op (false) after
// shutdown().
bool is_connected();

// True if BLE is in a healthy state: either advertising or a client is
// currently connected. The stuck-BLE watchdog in the connectivity task
// reboots the chip if this stays false for too long.
bool is_alive();

// Temporarily stop / restart BLE advertising so the Wi-Fi side can own the
// single ESP32-C3 radio during a connect + TLS sync (avoids a coexistence
// crash under radio load). pause_advertising() only stops advertising; an
// already-connected client link is left untouched, and resume_advertising() is
// a no-op while a client is connected (NimBLE re-advertises on disconnect).
// Both are safe no-ops before begin().
void pause_advertising();
void resume_advertising();

// Fully de-initialize the BLE stack (controller + host), freeing the radio for
// Wi-Fi. Used at the BLE→Wi-Fi handoff on the single-core C3, where Wi-Fi is
// unstable while the BLE controller is co-active. Irreversible until reboot;
// tick()/is_alive() become safe no-ops afterward.
void shutdown();

}  // namespace ble_service
