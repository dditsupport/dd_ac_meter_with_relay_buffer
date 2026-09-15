-- Migration 014: server-pushed maintenance config on ed_device_meta.
--
-- The Group C firmware reads five new keys from every ingest response and
-- caches them in NVS: nightly_reboot_enable / _start_hour / _end_hour, and
-- radio_rest_interval_sec / _duration_sec. Until this migration ran, the
-- server sent none of them, so every device stayed on the compile-time
-- *_DEFAULT values in config.h and the window could not be moved without
-- reflashing.
--
-- All five are NULLABLE and default to NULL, which means "not managed for this
-- device": ingest.php then pushes the fleet-wide DEFAULT_* value from
-- api/_db.php. Set a value on a row to override one device.
--
-- Nothing is pushed that the firmware will not re-validate. storage::
-- set_nightly_reboot() and set_radio_rest() reject an out-of-range value and
-- keep the previous one, so a bad row here cannot strand a device off-air or
-- mis-schedule its reboot.
--
-- Idempotent: each ADD COLUMN is guarded on the column not already existing.
-- Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

-- nightly_reboot_enable
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'nightly_reboot_enable');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN nightly_reboot_enable TINYINT(1) NULL AFTER coincell_mv',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- nightly_reboot_start_hour
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'nightly_reboot_start_hour');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN nightly_reboot_start_hour TINYINT UNSIGNED NULL AFTER nightly_reboot_enable',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- nightly_reboot_end_hour
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'nightly_reboot_end_hour');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN nightly_reboot_end_hour TINYINT UNSIGNED NULL AFTER nightly_reboot_start_hour',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- radio_rest_interval_sec
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'radio_rest_interval_sec');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN radio_rest_interval_sec INT UNSIGNED NULL AFTER nightly_reboot_end_hour',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- radio_rest_duration_sec
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'radio_rest_duration_sec');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN radio_rest_duration_sec SMALLINT UNSIGNED NULL AFTER radio_rest_interval_sec',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
