-- Migration: repurpose capacity_kw as the old meter's last reading (kWh).
--
-- The `capacity_kw` column on ed_energy_devices is no longer a kW capacity. It
-- now holds the reading shown on the physical meter this device replaced, in
-- kWh. The dashboard adds it to the Today and Period total figures so those
-- numbers continue from the old meter instead of restarting at zero.
--
-- Real meter readings routinely exceed the old DECIMAL(5,2) ceiling (999.99),
-- so widen the column to DECIMAL(12,2). The column name is kept to avoid
-- breaking the readings/devices API contract the Android app depends on.
--
-- MODIFY is idempotent (re-running sets the same type). Apply once via
-- phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

ALTER TABLE ed_energy_devices
  MODIFY capacity_kw DECIMAL(12,2) NULL;
