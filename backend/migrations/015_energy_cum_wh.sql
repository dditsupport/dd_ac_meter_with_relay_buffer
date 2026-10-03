-- Migration 015: continuous energy counter on ed_energy_readings.
--
-- The PZEM's raw Wh register (energy_wh) is not continuous: it wraps to 0 past
-- 9999.99 kWh, drops to ~0 on an energy reset (BOOT long-press / app) or when a
-- fresh module is fitted, and jumps to whatever a used replacement module
-- holds. Every chart and total took MAX-MIN of it, so each of those events
-- produced a spike of thousands of kWh, lost energy, or made the dashboard's
-- meter reading step backwards.
--
-- energy_cum_wh is a per-(device, channel) counter that only moves forward by
-- the energy actually used; readings.php computes everything from it and
-- ingest.php fills it for new rows. The rules (keep in step with
-- energy_delta_wh() in api/_db.php), comparing each reading with the
-- channel's previous one by seq:
--   * normal step               -> + the difference
--   * wrap past 9999.99 kWh     -> + the span across the ceiling
--   * drop to a lower value     -> + 0 (energy reset, or a fresh module fitted)
--   * step larger than 26 kW could produce over the gap (min 300 s)
--                               -> + 0 (a used module fitted)
-- A channel's first reading starts the counter at its raw value.
--
-- Needs window functions: MySQL 8.0+ or MariaDB 10.2+.
-- Idempotent: the column/index are guarded and the backfill recomputes every
-- row. Apply once via phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <db> < this.sql

SET @c := (SELECT COUNT(*) FROM information_schema.columns
            WHERE table_schema = DATABASE() AND table_name = 'ed_energy_readings'
              AND column_name = 'energy_cum_wh');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_energy_readings ADD COLUMN energy_cum_wh DECIMAL(16,2) NULL AFTER energy_wh',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Previous-reading lookup on ingest, and origin/latest in readings.php.
SET @c := (SELECT COUNT(*) FROM information_schema.statistics
            WHERE table_schema = DATABASE() AND table_name = 'ed_energy_readings'
              AND index_name = 'idx_device_ch_seq');
SET @sql := IF(@c = 0,
    'ALTER TABLE ed_energy_readings ADD KEY idx_device_ch_seq (device_id, channel, seq)',
    'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Backfill. Zero-filled junk rows (V = 0 and Wh = 0, logged by early
-- single-meter firmware on a missed read) are left out of the chain and keep
-- a NULL counter; readings.php ignores them anyway.
UPDATE ed_energy_readings r
JOIN (
    SELECT id,
           FIRST_VALUE(energy_wh) OVER (PARTITION BY device_id, channel ORDER BY seq)
         + SUM(delta_wh) OVER (PARTITION BY device_id, channel ORDER BY seq
                               ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS cum_wh
      FROM (
        SELECT id, device_id, channel, seq, energy_wh,
               CASE
                 WHEN prev_wh IS NULL THEN 0
                 WHEN energy_wh >= prev_wh THEN
                   IF(energy_wh - prev_wh > 26000 * GREATEST(COALESCE(dt_sec, 0), 300) / 3600,
                      0, energy_wh - prev_wh)
                 WHEN prev_wh >= 9999990 * 0.99 THEN
                   IF((9999990 - prev_wh) + energy_wh > 26000 * GREATEST(COALESCE(dt_sec, 0), 300) / 3600,
                      0, (9999990 - prev_wh) + energy_wh)
                 ELSE 0
               END AS delta_wh
          FROM (
            SELECT id, device_id, channel, seq, energy_wh,
                   LAG(energy_wh) OVER (PARTITION BY device_id, channel ORDER BY seq) AS prev_wh,
                   TIMESTAMPDIFF(SECOND,
                       LAG(wall_time) OVER (PARTITION BY device_id, channel ORDER BY seq),
                       wall_time) AS dt_sec
              FROM ed_energy_readings
             WHERE NOT (voltage = 0 AND energy_wh = 0)
          ) l
      ) d
) c ON c.id = r.id
SET r.energy_cum_wh = c.cum_wh;
