-- Migration: compressor-aware AC-cutoff knobs on device_relay_schedule.
--
-- The relay is an off-hours AC cutoff. The schedule (schedule_json) is now
-- interpreted as the AC-ALLOWED open hours; the firmware energizes the relay
-- to cut the AC OUTSIDE those windows. To avoid opening the relay while the
-- compressor is under load, the firmware waits for the compressor to cycle off
-- (wattage < compressor_watts) before cutting, hard-cutting only after
-- grace_min minutes. Both knobs are pushed to the device in each ingest
-- response (relay_compressor_watts / relay_grace_min) and bump the schedule
-- version so the device re-persists.
--
-- Idempotent: each ADD COLUMN is guarded so re-running is a no-op.
-- Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

-- compressor_watts: compressor "is-running" threshold in watts (100..10000).
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'device_relay_schedule'
              AND column_name = 'compressor_watts');
SET @sql := IF(@c = 0,
    'ALTER TABLE device_relay_schedule ADD COLUMN compressor_watts INT UNSIGNED NOT NULL DEFAULT 800 AFTER schedule_json',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- grace_min: minutes to wait for the compressor to idle before cutting (1..240).
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'device_relay_schedule'
              AND column_name = 'grace_min');
SET @sql := IF(@c = 0,
    'ALTER TABLE device_relay_schedule ADD COLUMN grace_min INT UNSIGNED NOT NULL DEFAULT 60 AFTER compressor_watts',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
