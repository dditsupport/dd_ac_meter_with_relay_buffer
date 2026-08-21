-- Migration 012: per-relay live state.
--
-- ed_device_meta.relay_on/relay_mode only ever held ONE relay's state (the
-- firmware's flat relay_* fields, which are relay 1's), so a multi-relay device
-- had no way to report relays 2 and 3 to the dashboard. Each relay now gets its
-- own row here, refreshed on every ingest POST from the `relays[]` array.
--
-- Deliberately NOT stored on ed_device_relay_schedule: a relay reports state
-- whether or not anyone has configured a schedule for it, so reusing that table
-- would either drop the state or fabricate empty schedule rows and make the
-- "has a schedule" check lie.
--
-- The old ed_device_meta.relay_* columns are left in place; ingest still writes
-- relay 1 there so anything reading them keeps working.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS ed_device_relay_state (
  device_id   VARCHAR(32)      NOT NULL,
  channel     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  relay_on    TINYINT(1)       NULL,
  relay_mode  VARCHAR(8)       NULL,
  version     INT UNSIGNED     NOT NULL DEFAULT 0,
  reported_at TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP
                               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (device_id, channel),
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
