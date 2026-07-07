-- Migration: log Wi-Fi signal strength (RSSI) alongside the RTC drift.
--
-- The firmware reports the connected-AP RSSI (dBm, negative) on each ingest
-- POST as `wifi_rssi`. We store it on each hourly RTC-drift log row so signal
-- quality can be tracked over time, and cache the latest on ed_device_meta for
-- the admin devices list (next to the RTC drift).
--
-- Idempotent: guarded ADD COLUMN. Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

-- Per-sample RSSI on the drift log.
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_rtc_drift_log'
              AND column_name = 'rssi_dbm');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_rtc_drift_log ADD COLUMN rssi_dbm SMALLINT NULL AFTER drift_sec',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Latest RSSI cached on device_meta for the admin list.
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'wifi_rssi');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN wifi_rssi SMALLINT NULL AFTER rtc_drift_at',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
