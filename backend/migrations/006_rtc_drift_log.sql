-- Migration: hourly DS1307 RTC drift log.
--
-- The firmware measures the RTC vs NTP offset at each hourly NTP sync and
-- reports it (rtc_drift_sec / rtc_drift_epoch) on the ingest POST. We log each
-- distinct measurement here so the RTC's drift can be tracked over time, and
-- cache the latest on ed_device_meta for the admin list.
--   drift_sec is SIGNED: + = RTC ahead of true time (running fast).
--
-- Idempotent: CREATE TABLE IF NOT EXISTS + guarded ADD COLUMN.
-- Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

CREATE TABLE IF NOT EXISTS ed_rtc_drift_log (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id   VARCHAR(32) NOT NULL,
  -- Wall-clock of the measurement (the NTP time at that sync). The device+time
  -- pair is unique so the same hourly sample, resent on every 2-minute POST
  -- until the next sync, is logged only once.
  measured_at DATETIME    NOT NULL,
  drift_sec   INT         NOT NULL,
  received_at TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_dev_measured (device_id, measured_at),
  KEY idx_dev_time (device_id, measured_at),
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Latest drift cached on device_meta for the admin devices list.
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'rtc_drift_sec');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN rtc_drift_sec INT NULL AFTER relay_reported_at',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_device_meta'
              AND column_name = 'rtc_drift_at');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN rtc_drift_at DATETIME NULL AFTER rtc_drift_sec',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
