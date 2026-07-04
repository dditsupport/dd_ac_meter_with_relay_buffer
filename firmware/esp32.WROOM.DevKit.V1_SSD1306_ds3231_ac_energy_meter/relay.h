#pragma once

#include <Arduino.h>

// Off-hours AC-cutoff relay. Fail-safe NC wiring: DE-ENERGIZED = AC powered
// (open hours), ENERGIZED = AC cut (off hours). A dead controller therefore
// leaves the AC on.
//
// The server pushes, per device, in each ingest.php response:
//   { "relay_version": <int>,
//     "relay_schedule": [ {days:[0..6], on:"HH:MM", off:"HH:MM"}, ... ],  // AC-ALLOWED open hours
//     "relay_compressor_watts": <int>,   // compressor "is-running" threshold
//     "relay_grace_min": <int> }         // minutes to wait for compressor idle before cutting
// All three are cached in NVS so cutoff keeps working during a Wi-Fi outage.
// With no schedule cached the relay stays de-energized (AC on) — fail-safe for
// an unconfigured device.
//
// tick() runs every loop tick (~50 ms) and drives the GPIO from a small state
// machine: outside the open-hours window it waits for the compressor to cycle
// off (wattage < threshold) before energizing to cut, and cuts by the grace
// deadline at the latest — even if the AC was already idle at closing. Once
// cut it stays latched until the next open-hours start. update_power() feeds it
// the latest PZEM wattage at 1 Hz.
//
// The Android app can also take manual control over BLE (see Mode below):
// FORCE_ON (energize = cut) / FORCE_OFF (de-energize = AC on) override the
// schedule until set back to AUTO.

namespace relay {

// Manual override, set by the Android app over the BLE Relay characteristic.
//   AUTO      - follow the server schedule (default).
//   FORCE_ON  - hold the relay ON  regardless of schedule.
//   FORCE_OFF - hold the relay OFF regardless of schedule.
// The override lives in RAM only, so a reboot returns to AUTO (schedule
// driven). A manual toggle is a live action while the phone is connected,
// not a persistent configuration change.
enum class Mode : uint8_t { AUTO = 0, FORCE_ON = 1, FORCE_OFF = 2 };

void begin();

// Apply a freshly-fetched relay config. Caller passes the parsed schedule
// (AC-allowed open hours) as a JSON-array string, plus the compressor watt
// threshold and grace minutes. Compares against stored values; persists +
// re-evaluates on change. Pass an empty array to clear the schedule; pass 0
// for compressor_watts / grace_min to leave those unchanged.
void apply(uint32_t version, const String &schedule_json_array,
           uint32_t compressor_watts, uint32_t grace_min);

// Feed the latest PZEM wattage (called at 1 Hz from the sampler). `valid` is
// false when the PZEM read failed, so the state machine ignores stale power.
void update_power(float watts, bool valid);

// Last version we accepted. Firmware sends this back so the server can
// short-circuit on no-change later if it wants to. Currently informational.
uint32_t version();

// Called every loop tick from ac_energy_meter.ino. Recomputes the
// desired on/off state from the cached schedule + local time, drives GPIO.
void tick();

// True if the relay is currently energised.
bool is_on();

// Manual override control. set_mode() drives the GPIO immediately.
void set_mode(Mode m);
Mode mode();
const char *mode_str();   // "auto" | "on" | "off"

// Compact status for the BLE Relay characteristic, e.g.
//   {"mode":"auto","on":false,"sched_version":3}
String status_json();

}  // namespace relay
