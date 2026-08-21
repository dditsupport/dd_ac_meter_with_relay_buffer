#pragma once

#include "shared_state.h"

// Multi-channel PZEM driver. Every entry point takes a channel index in
// [0, PZEM_CHANNELS): 0 == "PZEM-1" as labelled in config.h / the wire format's
// 1-based `ch` field. Each channel owns an independent Modbus link, fault
// streak and reset flag, so one dead meter never masks or blocks the other.
namespace pzem {

void begin();

// Number of configured channels (== PZEM_CHANNELS).
uint8_t channel_count();

// Reads one sample from `ch`. On success returns true and fills `out`.
// On Modbus failure returns false; caller should not use `out`.
bool read(uint8_t ch, PzemSample &out);

// Classify a stream of samples for `ch` and return the right PzemStatus.
// Pass `ok = true` on a successful read, `ok = false` on a failed read.
// Fail streaks and low-voltage timers are tracked per channel.
PzemStatus classify(uint8_t ch, bool ok, const PzemSample &sample);

// Reset the cumulative energy counter on one channel's PZEM.
bool reset_energy(uint8_t ch);

// Request an energy-register reset from another task (e.g. the BLE service).
// The actual reset is performed by the PZEM-owning sampling task when it calls
// consume_reset_request(), so the resetEnergy() Modbus write never races a read.
// Pass ch = PZEM_RESET_ALL to reset every channel.
static const uint8_t PZEM_RESET_ALL = 0xFF;
void request_reset(uint8_t ch);
// Returns true if a reset is pending for `ch`, clearing that channel's flag.
bool consume_reset_request(uint8_t ch);

}  // namespace pzem
