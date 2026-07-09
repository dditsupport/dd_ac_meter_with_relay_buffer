-- Migration: log the CR2032 coin-cell (RTC backup) voltage alongside the RTC
-- drift and RSSI.
--
-- The firmware reports the coin-cell voltage on each ingest POST as
-- `coincell_mv` (millivolts, unsigned). We store it on each hourly RTC-drift
-- log row so the voltage can be trended over time, and cache the latest on
-- ed_device_meta for the admin devices list (next to RTC drift and Wi-Fi
-- signal) so a dying cell can be spotted before the clock is lost.
--
-- Idempotent: guarded ADD COLUMN. Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

-- Per-sample coin-cell voltage on the drift log.
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_rtc_drift_log'
              AND column_name = 'coincell_mv');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_rtc_drift_log ADD COLUMN coincell_mv SMALLINT UNSIGNED NULL AFTER rssi_dbm',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Latest coin-cell voltage cached on device_meta for the admin list.
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'coincell_mv');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN coincell_mv SMALLINT UNSIGNED NULL AFTER wifi_rssi',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
