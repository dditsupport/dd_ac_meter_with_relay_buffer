#pragma once

#include "shared_state.h"

namespace pzem {

void begin();

// Reads one sample. On success returns true and fills `out`.
// On Modbus failure, returns false; caller should not use `out`.
bool read(PzemSample &out);

// Classify a stream of samples and return the right PzemStatus.
// Pass `ok = true` on a successful read, `ok = false` on a failed read.
// Internally counts consecutive failures and tracks low-voltage duration.
PzemStatus classify(bool ok, const PzemSample &sample);

// Reset the cumulative energy counter on the PZEM (not used in normal operation).
bool reset_energy();

// Request an energy-register reset from another task (e.g. the BLE service).
// The actual reset is performed by the PZEM-owning sampling task when it calls
// consume_reset_request(), so the resetEnergy() Modbus write never races a read.
void request_reset();
bool consume_reset_request();

}  // namespace pzem
