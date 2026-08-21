-- Migration 010: multi-PZEM support.
--
-- A dual-PZEM device reports BOTH meters under one device_id. Each reading now
-- carries a 1-based `channel`; rows from a single sampling instant share `seq`
-- and differ only by `channel`.
--
-- The unique key MUST move from (device_id, seq) to (device_id, seq, channel):
-- otherwise channel 2 collides with channel 1 on every insert and is silently
-- dropped by the ingest's ON DUPLICATE KEY UPDATE.
--
-- Idempotent: each step is guarded, so re-running is a no-op.
-- Existing single-meter rows default to channel 1, which is what they are.

-- ---- ed_energy_readings.channel ----
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_energy_readings'
              AND column_name  = 'channel');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_energy_readings ADD COLUMN channel TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER seq',
    'DO 0 /* channel already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---- swap the unique key to include channel ----
SET @k := (SELECT COUNT(*) FROM information_schema.statistics
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_energy_readings'
              AND index_name   = 'uq_device_seq');
SET @sql := IF(@k > 0,
    'ALTER TABLE ed_energy_readings DROP INDEX uq_device_seq',
    'DO 0 /* old key already dropped */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

SET @k := (SELECT COUNT(*) FROM information_schema.statistics
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_energy_readings'
              AND index_name   = 'uq_device_seq_ch');
SET @sql := IF(@k = 0,
    'ALTER TABLE ed_energy_readings ADD UNIQUE KEY uq_device_seq_ch (device_id, seq, channel)',
    'DO 0 /* new key already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---- per-channel lookup index ----
SET @k := (SELECT COUNT(*) FROM information_schema.statistics
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_energy_readings'
              AND index_name   = 'idx_device_ch_time');
SET @sql := IF(@k = 0,
    'ALTER TABLE ed_energy_readings ADD KEY idx_device_ch_time (device_id, channel, wall_time)',
    'DO 0 /* index already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---- ed_device_meta.channel_count (how many PZEMs the unit reports) ----
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_device_meta'
              AND column_name  = 'channel_count');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN channel_count TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER log_interval_sec',
    'DO 0 /* channel_count already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;
