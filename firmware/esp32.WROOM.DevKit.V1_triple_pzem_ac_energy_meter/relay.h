#pragma once

#include <Arduino.h>
#include "config.h"   // RELAY_COUNT

// Off-hours AC-cutoff relays — ONE PER PZEM CHANNEL (RELAY_COUNT of them).
// Relay N cuts the AC that PZEM N measures, and each runs a fully independent
// schedule, cutoff state machine, manual override and NVS-cached config. The
// compressor-idle check for relay N therefore looks only at channel N's
// wattage, which is the point of the pairing: deciding relay 1 from meter 2's
// load would either hard-cut a running compressor or never cut at all.
//
// Every entry point below takes a 0-based relay index `r` in [0, RELAY_COUNT),
// which is the same index as the PZEM channel it belongs to (r == 0 is
// "relay 1" / "PZEM 1" as labelled on the wire and in config.h).
//
// Fail-safe NC wiring: DE-ENERGIZED = AC powered (open hours), ENERGIZED =
// AC cut (off hours). A dead controller therefore leaves the AC on.
//
// The server pushes, per channel, in each ingest.php response:
//   "relay_channels": [ { "ch": 1,
//                         "version": <int>,
//                         "schedule": [ {days:[0..6], on:"HH:MM", off:"HH:MM"}, ... ],
//                         "compressor_watts": <int>,
//                         "grace_min": <int> }, ... ]
// All of it is cached in NVS (keys suffixed per relay) so cutoff keeps working
// during a Wi-Fi outage. With no schedule cached a relay stays de-energized
// (AC on) — fail-safe for an unconfigured device.
//
// tick() runs every loop tick (~50 ms) and drives ALL relays: outside its
// open-hours window a relay energizes to cut as soon as its channel's wattage
// is below the compressor threshold (idle = safe to open under no load), or by
// the grace deadline at the latest if it never idles. An AC already idle at
// closing is cut right away. Once cut it stays latched until the next
// open-hours start. update_power() feeds each relay its channel's wattage.

namespace relay {

// Manual override, set by the Android app over the BLE Relay characteristic.
//   AUTO      - follow the server schedule (default).
//   FORCE_ON  - hold the relay ENERGIZED (AC cut) regardless of schedule.
//   FORCE_OFF - hold the relay DE-ENERGIZED (AC on) regardless of schedule.
// The override lives in RAM only, so a reboot returns to AUTO (schedule
// driven). A manual toggle is a live action while the phone is connected,
// not a persistent configuration change.
enum class Mode : uint8_t { AUTO = 0, FORCE_ON = 1, FORCE_OFF = 2 };

void begin();

// Number of relays (== RELAY_COUNT == PZEM_CHANNELS).
uint8_t count();

// Apply a freshly-fetched config to ONE relay. Compares against stored values;
// persists + re-evaluates on change. Pass an empty array to clear the schedule;
// pass 0 for compressor_watts / grace_min to leave those unchanged.
void apply(uint8_t r, uint32_t version, const String &schedule_json_array,
           uint32_t compressor_watts, uint32_t grace_min);

// Feed one relay the latest wattage from ITS channel (called at 1 Hz from the
// sampler). `valid` is false when that PZEM read failed, so the state machine
// ignores stale power and treats the compressor as possibly still running.
void update_power(uint8_t r, float watts, bool valid);

// Last config version accepted for `r`. Informational.
uint32_t version(uint8_t r);

// Called every loop tick from the .ino. Recomputes the desired state of EVERY
// relay from its cached schedule + local time and drives the GPIOs.
void tick();

// True if relay `r` is currently energised (AC cut).
bool is_on(uint8_t r);

// Manual override control, per relay.
void set_mode(uint8_t r, Mode m);
Mode mode(uint8_t r);
const char *mode_str(uint8_t r);   // "auto" | "on" | "off"

// Status of ALL relays for the BLE Relay characteristic:
//   {"relays":[{"ch":1,"mode":"auto","energized":false,...}, ...]}
String status_json();

}  // namespace relay
