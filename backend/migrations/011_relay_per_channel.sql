-- Migration 011: one relay per PZEM channel.
--
-- A dual-PZEM unit has one relay per meter, and each relay needs its OWN
-- open-hours window and cutoff knobs (relay N is decided from channel N's
-- wattage). So ed_device_relay_schedule moves from one row per device to one
-- row per (device, channel).
--
-- Idempotent: each step is guarded, so re-running is a no-op.
-- Existing rows become channel 1, which is what they already configure.

-- ---- channel column ----
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_device_relay_schedule'
              AND column_name  = 'channel');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_relay_schedule ADD COLUMN channel TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER device_id',
    'DO 0 /* channel already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---- widen the primary key to (device_id, channel) ----
-- Detect by counting the columns in the PK: 1 = old per-device key, 2 = done.
SET @pk := (SELECT COUNT(*) FROM information_schema.statistics
             WHERE table_schema = DATABASE()
               AND table_name   = 'ed_device_relay_schedule'
               AND index_name   = 'PRIMARY');
SET @sql := IF(@pk = 1,
    'ALTER TABLE ed_device_relay_schedule DROP PRIMARY KEY, ADD PRIMARY KEY (device_id, channel)',
    'DO 0 /* primary key already (device_id, channel) */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---- ed_device_meta.relay_count (how many relays the unit has) ----
SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE()
              AND table_name   = 'ed_device_meta'
              AND column_name  = 'relay_count');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_device_meta ADD COLUMN relay_count TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER channel_count',
    'DO 0 /* relay_count already present */');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;
