#pragma once

#include <Arduino.h>

namespace wifi_sync {

void begin();

// Called periodically from ConnectivityTask. Returns true if a sync cycle ran
// to completion (regardless of how many rows transferred).
bool run_cycle();

// Pause notifications during HTTPS POST to ease 2.4 GHz coexistence with BLE.
// Set/cleared internally; ble_service.cpp consults this via is_radio_busy().
bool is_radio_busy();

// Ask the connectivity task to attempt a Wi-Fi cycle right away instead of
// waiting for the next periodic tick. Called by ble_service::WifiCfgCallbacks
// after a successful credential write.
void request_immediate_sync();

// Returns true (and clears) if an immediate-sync request is pending.
bool consume_immediate_sync_request();

// Ask the connectivity task to run a Wi-Fi scan and update the cached results
// JSON. Triggered by a BLE write of {"action":"scan"} to Wi-Fi Config.
void request_scan();
bool consume_scan_request();

// Run a synchronous Wi-Fi scan and update the cached JSON. Should only be
// called from the ConnectivityTask context (it briefly switches the Wi-Fi mode).
void run_scan();

// Returns the most recent scan result as a JSON string (array). Empty array
// "[]" if no scan has completed yet.
String get_scan_results_json();

// Monotonic counter, incremented every time the scan results are refreshed.
// ble_service uses this to decide when to push a NOTIFY.
uint32_t scan_results_version();

// Seconds since the last successful ingest POST. UINT32_MAX if there has
// never been a successful POST since boot. Used by the stuck-Wi-Fi watchdog
// in the connectivity task.
uint32_t seconds_since_last_successful_post();

// Consecutive Wi-Fi cycles whose POST the heap guard deferred because free heap
// or the largest contiguous free block was below the HEAP_MIN_* thresholds in
// config.h. Reset to 0 by any cycle with a healthy heap. The heap watchdog in
// the connectivity task reboots once this reaches HEAP_LOW_REBOOT_CYCLES —
// deferring alone cannot recover a fragmented heap, only a reboot can.
uint32_t consecutive_low_heap_cycles();

// True while the station is actually associated with an AP. The stuck-Wi-Fi
// escalation in the connectivity task gates on this so that ordinary offline
// time — no AP in range — is never mistaken for a wedged radio.
bool is_associated();

// Radio rest (see RADIO_REST_* in config.h). radio_off() disassociates the STA
// and powers the Wi-Fi PHY down; radio_on() brings the interface back up in STA
// mode. Because the STA returns unassociated, the next try_connect_known()
// takes the full scan + WiFi.begin() path instead of the WL_CONNECTED fast
// path, which is the whole point: a fresh association means a fresh DHCP lease
// and fresh DNS servers. Called only from the connectivity task's rest window.
void radio_off();
void radio_on();

}  // namespace wifi_sync
