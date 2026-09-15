-- Migration 013: default BLE access PIN for newly registered devices.
--
-- Registration used to stamp each new device with a RANDOM 6-digit PIN
-- (gen_ble_pin()). That made commissioning a two-person job: the installer
-- standing in front of a freshly flashed meter could not open it from the app
-- until someone else read the random PIN out of the admin UI. New devices now
-- start on a fixed, known PIN — 112233 — which the admin changes per device
-- once the meter is installed.
--
-- The PIN only gates the Android app's own device list; BLE_PRESHARED_KEY in
-- the firmware is what actually authenticates a connection.
--
-- This sets the column DEFAULT so any INSERT that omits ble_pin gets it too.
-- EXISTING DEVICES ARE NOT TOUCHED: a PIN an admin has already set or
-- randomised stays exactly as it is. The only rows updated are ones with no
-- usable PIN at all (NULL or empty), which migration 004's backfill should
-- already have covered.
--
-- Idempotent: the ALTER is guarded on the column existing and re-running it
-- sets the same default; the UPDATE only ever touches blanks.
-- Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

-- Column default
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_energy_devices'
              AND column_name = 'ble_pin');
SET @sql := IF(@c = 1,
    "ALTER TABLE ed_energy_devices ALTER COLUMN ble_pin SET DEFAULT '112233'",
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Fill in any device that somehow has no PIN. Never overwrites an assigned one.
UPDATE ed_energy_devices
   SET ble_pin = '112233'
 WHERE ble_pin IS NULL OR ble_pin = '';
