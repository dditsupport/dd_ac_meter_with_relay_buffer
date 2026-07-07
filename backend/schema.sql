-- AC Energy Meter — schema for the SHARED WorkPulse database.
--
-- All meter tables are prefixed `ed_` so they live alongside the WorkPulse
-- tables in one database (e.g. gtvpheud_workpulse). Each meter is linked to a
-- WorkPulse store via ed_energy_devices.location -> locations.location_id, so
-- the WorkPulse app can show energy data per location while aromen.biz/meter
-- keeps posting readings and rendering its own graphs from the same tables.
--
-- REQUIRES the WorkPulse `locations` table to already exist (for the FK).
-- Idempotent: CREATE TABLE IF NOT EXISTS. Run in phpMyAdmin (SQL tab) or
--   mysql -u <user> -p <workpulse_db> < schema.sql
-- Charset: utf8mb4. Engine: InnoDB (for FK + transactions).

SET NAMES utf8mb4;

-- ---------------- ed_users ----------------
-- Meter-app admin/dashboard login. Separate from WorkPulse's own identity.
CREATE TABLE IF NOT EXISTS ed_users (
  id              INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  username        VARCHAR(32)  NOT NULL UNIQUE,
  password_hash   VARCHAR(255) NOT NULL,
  email           VARCHAR(128) NULL,
  is_admin        TINYINT(1)   NOT NULL DEFAULT 0,
  created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login_at   TIMESTAMP    NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_energy_devices ----------------
-- Auto-registered the first time an ingest POST arrives. `location` binds the
-- meter to a WorkPulse store (locations.location_id); owner_user_id is set by
-- an admin via the meter admin panel.
CREATE TABLE IF NOT EXISTS ed_energy_devices (
  device_id       VARCHAR(32)  PRIMARY KEY,
  friendly_name   VARCHAR(64)  NOT NULL,
  location        INT          NULL,          -- FK -> locations.location_id
  installed_at    DATE         NULL,
  -- Old meter's last reading (kWh) at install. Added to the dashboard Today &
  -- Period totals so the figures continue from the meter this device replaced.
  -- (Legacy column name; it no longer represents a kW capacity.)
  capacity_kw     DECIMAL(12,2) NULL,
  notes           TEXT         NULL,
  owner_user_id   INT UNSIGNED NULL,
  -- App-side BLE access PIN. Auto-generated at registration; the firmware
  -- never sees it. The Android app caches authorised PINs at login and gates
  -- BLE access locally.
  ble_pin         VARCHAR(12)  NULL,
  first_seen_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_owner    (owner_user_id),
  KEY idx_location (location),
  CONSTRAINT fk_ed_dev_owner    FOREIGN KEY (owner_user_id) REFERENCES ed_users(id)      ON DELETE SET NULL,
  CONSTRAINT fk_ed_dev_location FOREIGN KEY (location)      REFERENCES locations(location_id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_device_meta ----------------
CREATE TABLE IF NOT EXISTS ed_device_meta (
  device_id        VARCHAR(32)  PRIMARY KEY,
  fw_version       VARCHAR(16)  NULL,
  last_sync_at     TIMESTAMP    NULL,
  last_seq         BIGINT UNSIGNED NOT NULL DEFAULT 0,
  last_boot_id     INT UNSIGNED  NOT NULL DEFAULT 0,
  total_readings   BIGINT UNSIGNED NOT NULL DEFAULT 0,
  log_interval_sec INT UNSIGNED  NOT NULL DEFAULT 900,
  -- Last relay state the device reported on an ingest POST (live indicator).
  relay_on          TINYINT(1)  NULL,
  relay_mode        VARCHAR(8)  NULL,   -- 'auto' | 'on' | 'off'
  relay_reported_at TIMESTAMP   NULL,
  -- Latest DS1307 drift (signed sec, + = RTC fast) and when it was measured.
  rtc_drift_sec     INT         NULL,
  rtc_drift_at      DATETIME    NULL,
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_rtc_drift_log ----------------
-- One row per hourly NTP sync: how far the DS1307 had drifted from true time.
CREATE TABLE IF NOT EXISTS ed_rtc_drift_log (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id   VARCHAR(32) NOT NULL,
  measured_at DATETIME    NOT NULL,   -- NTP time at the sync
  drift_sec   INT         NOT NULL,   -- signed; + = RTC ahead of true time
  received_at TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_dev_measured (device_id, measured_at),
  KEY idx_dev_time (device_id, measured_at),
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_energy_readings ----------------
-- (device_id, seq) UNIQUE makes duplicate POSTs (firmware retry) a no-op.
CREATE TABLE IF NOT EXISTS ed_energy_readings (
  id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id       VARCHAR(32)   NOT NULL,
  seq             BIGINT UNSIGNED NOT NULL,
  wall_time       DATETIME       NOT NULL,
  time_confidence ENUM('exact','approx') NOT NULL DEFAULT 'exact',
  boot_id         INT UNSIGNED   NOT NULL,
  sec_since_boot  INT UNSIGNED   NOT NULL,
  voltage         DECIMAL(6,2)   NOT NULL,
  current_a       DECIMAL(8,3)   NOT NULL,
  power_w         DECIMAL(10,2)  NOT NULL,
  energy_wh       DECIMAL(14,2)  NOT NULL,
  power_factor    DECIMAL(4,3)   NOT NULL,
  frequency_hz    DECIMAL(5,2)   NULL,
  ingested_at     TIMESTAMP      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_device_seq    (device_id, seq),
  KEY idx_device_time          (device_id, wall_time),
  KEY idx_device_date_energy   (device_id, wall_time, energy_wh),
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_ingest_log ----------------
-- Lightweight audit of every POST attempt. Trim manually if it grows.
CREATE TABLE IF NOT EXISTS ed_ingest_log (
  id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id       VARCHAR(32)  NULL,
  received_at     TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  rows_in_payload INT          NULL,
  rows_inserted   INT          NULL,
  status          VARCHAR(32)  NOT NULL,
  client_ip       VARCHAR(45)  NULL,
  notes           TEXT         NULL,
  KEY idx_device_time (device_id, received_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- ed_device_relay_schedule ----------------
-- Per-device relay config for the off-hours AC cutoff. The firmware fetches
-- this in every ingest.php response and drives the relay accordingly.
CREATE TABLE IF NOT EXISTS ed_device_relay_schedule (
  device_id     VARCHAR(32)     NOT NULL PRIMARY KEY,
  -- AC-ALLOWED open hours as a JSON array of windows:
  --   [{days:[0..6], on:"HH:MM", off:"HH:MM"}, ...]
  -- days: 0=Sun, 1=Mon, ..., 6=Sat. The AC is powered inside these windows;
  -- the relay energizes to CUT the AC outside them. Empty array = AC always
  -- allowed (relay never cuts).
  schedule_json TEXT            NOT NULL DEFAULT ('[]'),
  -- Compressor "is-running" watt threshold. Below it the compressor is off, so
  -- cutting is safe; the firmware waits for wattage to drop below this before
  -- opening the relay (never hard-cuts a running compressor).
  compressor_watts INT UNSIGNED NOT NULL DEFAULT 800,
  -- Minutes after off-hours begin to wait for the compressor to cycle off
  -- before cutting anyway (hard-cut deadline).
  grace_min     INT UNSIGNED    NOT NULL DEFAULT 60,
  -- Monotonic version. Bumped on every update. Firmware uses it to detect
  -- whether the config has changed since the last poll.
  version       INT UNSIGNED    NOT NULL DEFAULT 1,
  updated_at    TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP
                                ON UPDATE CURRENT_TIMESTAMP,
  FOREIGN KEY (device_id) REFERENCES ed_energy_devices(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------- First admin user ----------------
-- Do NOT create the admin user via schema.sql — bcrypt hashes need to be
-- generated with PHP's password_hash() on the host. After running this
-- schema, visit
--   https://<your-domain>/meter/bootstrap.php
-- in a browser. It runs ONLY if no admin user exists (in ed_users); you'll set
-- the initial admin username and password from there, then the script refuses
-- to do anything on subsequent visits.
