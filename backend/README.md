# Backend (MilesWeb PHP 8.4 + MySQL)

Server-side counterpart to the ESP32 firmware. Multi-device, multi-user
with admin role.

## Host requirements

- **PHP 8.4** (8.2 minimum — `_db.php` exits early below that). Verified
  on 8.4; uses `Random\Randomizer`, `#[\SensitiveParameter]`,
  `JSON_THROW_ON_ERROR`, `match`, `?type`, `never` return, strict types.
- **MySQL 5.7+ or MariaDB 10.3+** (needs `JSON`-free schema and
  `ON DUPLICATE KEY UPDATE` — both old enough).
- **PDO_MYSQL** extension (default on MilesWeb / any cPanel host).
- **`mod_authz_core`** (for the `Require all denied` in `_config/.htaccess`).
  Standard on every Apache 2.4 install.
- **Let's Encrypt / Cloudflare TLS** for the public hostname so the
  ESP32's `WiFiClientSecure::setInsecure()` POST works (we accept any
  cert but still need *some* cert).

No Composer dependencies. No build step. No PHP frameworks.

## Layout

```
backend/
├── schema.sql                       ← one-time SQL setup
└── public_html/
    ├── _config/
    │   ├── .htaccess                ← Require all denied
    │   ├── secrets.php.example      ← copy to secrets.php, fill in
    │   └── secrets.php              ← gitignored; real creds live here
    └── meter/
        ├── api/
        │   ├── _db.php              ← PDO + auth + helpers
        │   ├── ingest.php           ← device POST endpoint (X-Device-Token)
        │   ├── readings.php         ← GET data for dashboard (session auth)
        │   ├── devices.php          ← list user's devices
        │   ├── login.php            ← POST login (form or JSON)
        │   ├── logout.php
        │   ├── admin_users.php      ← admin user CRUD
        │   └── admin_devices.php    ← admin device CRUD + binding
        ├── dashboard/
        │   ├── login.php            ← sign-in page
        │   ├── index.php            ← user dashboard with charts
        │   └── assets/style.css
        └── admin/
            ├── index.php            ← admin overview + recent ingest log
            ├── users.php            ← create / edit / delete users
            └── devices.php          ← assign devices to users
```

## Deploy

1. **Database** — the meter now shares the **WorkPulse database** (e.g.
   `gtvpheud_workpulse`). All meter tables are prefixed **`ed_`** so they sit
   alongside the WorkPulse tables, and each device is linked to a WorkPulse
   store via `ed_energy_devices.location → locations.location_id`.

   In phpMyAdmin → SQL tab (with the WorkPulse DB selected), paste the contents
   of `schema.sql` and run. It is idempotent (`CREATE TABLE IF NOT EXISTS`) and
   **requires the WorkPulse `locations` table to already exist** (for the FK).
   It creates: `ed_users`, `ed_energy_devices`, `ed_device_meta`,
   `ed_energy_readings`, `ed_ingest_log`, `ed_device_relay_schedule`.

   The `migrations/` files (001–005) are historical, written against the old
   un-prefixed standalone schema; on the shared DB use `schema.sql` (which
   already includes every column those migrations added). The one migration
   written for the shared/`ed_`-prefixed schema is
   `006_rtc_drift_log.sql` (hourly DS1307 drift log + latest-drift columns) —
   run it on an existing shared DB, or just re-run `schema.sql` (idempotent).
   ac.aromen.biz posts readings and renders its graphs from these tables as
   before; the WorkPulse app reads the same rows filtered by `location`.

2. **Secrets** — copy `public_html/_config/secrets.php.example` to
   `public_html/_config/secrets.php` and fill in DB creds + token. Point the
   DB creds at the **shared WorkPulse database** (the one holding `locations`
   and the `ed_*` tables). The `DEVICE_TOKEN` value must match
   `firmware/esp32.supermini_ds1307_ac_energy_meter/config.h`.

3. **Upload** — the app is served from the **`ac.aromen.biz` subdomain**, whose
   document root **is** the app folder (there is no `/meter` path segment any
   more). Upload the contents of `public_html/meter/` into that document root,
   and `public_html/_config/` one level above it:

   ```
   /home/<cpaneluser>/
   ├── _config/                 <- secrets.php (outside the docroot)
   ├── meter_secrets/           <- optional alternative secrets location
   ├── meter_sessions/          <- auto-created session store
   └── ac.aromen.biz/           <- SUBDOMAIN DOCUMENT ROOT
       ├── api/                 <- ingest.php, login.php, readings.php, ...
       ├── dashboard/
       ├── admin/
       └── bootstrap.php
   ```

   URLs then come out as `https://ac.aromen.biz/api/ingest.php`,
   `https://ac.aromen.biz/dashboard/`, and so on.

   Because `api/` now sits **two** levels below the home directory (it was three
   under the old `public_html/meter/api/`), `api/_db.php` resolves its
   out-of-docroot paths with `dirname(__DIR__, 2)`. It tries `METER_SECRETS_PATH`,
   then `~/meter_secrets/secrets.php`, then `~/_config/secrets.php`, and finally
   `<docroot>/_config/secrets.php`. If you relocate the app to a different depth,
   those two `dirname(__DIR__, 2)` calls (secrets + session dir) are what to adjust.

4. **Verify secrets aren't served** — with the layout above, `_config/` is above
   the document root, so `https://ac.aromen.biz/_config/secrets.php` should
   return 404 (nothing is mapped there). Only if you put `_config/` *inside* the
   document root does it need the bundled `.htaccess` to return 403 — and if it
   serves the file instead, your host doesn't honor `Require all denied`, so move
   `_config/` back above the document root.

5. **First admin login** — visit
   `https://ac.aromen.biz/bootstrap.php`. The page creates the
   first admin account from a form, then locks itself the moment any
   admin row exists. Sign in at `/dashboard/login.php` afterwards
   and (optionally) delete `bootstrap.php` from the server.

6. **Point a device** — confirm `INGEST_HOST_DEFAULT` (firmware
   `config.h`) is `https://ac.aromen.biz` or push a different value via
   BLE. Power on the ESP32; within ~30 s its heartbeat POST lands and
   the device auto-registers. Then Admin → Devices to bind it to a user.

## Routes

### Device-facing

| Method | URL | Auth | Purpose |
|---|---|---|---|
| POST | `/api/ingest.php` | `X-Device-Token` | data ingest |

Request shape: per spec §3.5, plus `Hz` per reading. Response:

```json
{
  "ok": true,
  "acked_up_to_seq": 1248,
  "server_time": "2026-06-20T17:24:32+05:30",
  "log_interval_sec": 900
}
```

`log_interval_sec` is the effective cadence for the device — taken from
`device_meta.log_interval_sec` (per-device override), falling back to
`DEFAULT_LOG_INTERVAL_SEC` from `secrets.php`.

### Browser / Android app

All require a session cookie (`meter_sess`) from POST `/api/login.php`.

| Method | URL | Auth | Purpose |
|---|---|---|---|
| POST | `/api/login.php` | none | `{username,password}` → session |
| GET/POST | `/api/logout.php` | any | clear session |
| GET | `/api/devices.php` | session | list devices user can see |
| GET | `/api/readings.php` | session | data points; query params: `device_id`, `from`, `to`, `aggregate=raw\|hourly\|daily\|monthly` |
| GET | `/api/relay_state.php` | session | last-reported relay state of a device: `device_id` → `{on, mode, reported_at, interval}` |
| POST | `/api/admin_users.php` | admin | `action=list\|create\|set_password\|set_admin\|delete` (CSRF) |
| POST | `/api/admin_devices.php` | admin | `action=list\|bind\|rename\|set_interval\|regen_pin\|set_pin\|delete` (CSRF). `regen_pin` rolls a random 6-digit BLE PIN, `set_pin` stores a specific one |
| POST | `/api/admin_relay.php` | admin | `action=get\|set\|clear` per-device AC-cutoff config (open-hours schedule + `compressor_watts` + `grace_min`), `action=states` live relay state of all devices (CSRF) |

### Pages

| URL | Auth | Purpose |
|---|---|---|
| `/dashboard/login.php` | none | sign-in form |
| `/dashboard/` | session | charts: Today / 24 h / 7 d / 30 d / 12 mo + live relay state |
| `/dashboard/report.php` | session | day-vs-day comparison: hourly kWh line per day, Weekly (last 7 days incl. today) or Monthly (pick a month) |
| `/admin/` | admin | overview + recent ingest activity |
| `/admin/users.php` | admin | user CRUD |
| `/admin/devices.php` | admin | device binding, per-device interval override, AC-cutoff config (open hours + compressor knobs) + live relay state |

## Aggregations

`readings.php?aggregate=hourly|daily|monthly` groups rows by bucket and
computes energy generated in the bucket as
`(MAX(energy_wh) - MIN(energy_wh)) / 1000` — works because the PZEM's
`Wh` counter is monotonically increasing. Also returns `P_avg`,
`P_peak`, `V_avg`, `samples`, and `approx` (true if any rows in the
bucket had `time_confidence='approx'`).

### Continuing from a replaced meter

`ed_energy_devices.capacity_kw` is repurposed as the **old meter's final
reading in kWh** at install ("Old kWh" in admin), so the dashboard can show a
meter reading that carries on from the unit this device replaced.

Adding it straight onto the PZEM counter overshoots, because that counter is
rarely at zero when a device goes into service — bench testing leaves a few
hundred Wh on it. `readings.php` therefore also returns `origin_kwh`, the
channel's **first-ever** reading, and the dashboard charts
`old_kwh + (counter − origin_kwh)`. A device entered as `1348.10` then starts
at exactly `1348.10` rather than `1348.52`.

`origin_kwh` is the earliest row by `wall_time`, deliberately not
`MIN(energy_wh)`: after a PZEM reset `MIN` would be the post-reset zero rather
than the value the counter held at install. It is `null` for a device with no
readings, in which case the dashboard falls back to `old_kwh` alone.

Note this anchors the *chart*; the Today and Period total cards still add the
raw `capacity_kw` to a window's consumption, so they agree with the chart's end
reading only when the window covers the device's whole history.

## Time zone (IST)

Everything runs in `APP_TIMEZONE` (default `Asia/Kolkata`, IST/+05:30):

- PHP's default zone is set from it, so device `wall_time` and `server_time`
  (`date('c')`) are formatted in IST.
- On every connection, `_db.php` runs `SET time_zone` to the matching numeric
  offset, so MySQL `NOW()` / `CURRENT_TIMESTAMP` columns (`last_sync_at`,
  `ingested_at`, `received_at`, relay `updated_at`, …) are IST too — not the
  shared host's default (often UTC). The numeric offset is used so it works
  without the named-time-zone tables many hosts omit.
- The relay is an **off-hours AC cutoff**: the schedule windows are the
  **AC-allowed open hours**, and the firmware energizes the relay (fail-safe
  NC — energize = cut) to switch the AC off *outside* them. It never opens
  under compressor load: it waits for wattage to drop below `compressor_watts`,
  cutting by `grace_min` minutes after closing at the latest, then holding the
  cut until the next open-hours start. Schedule times (HH:MM) are
  interpreted by the firmware in its own `TZ_INFO` (`IST-5:30`), which matches
  the backend `APP_TIMEZONE`. Keep the two in sync if you change region.

To run in a different zone, change `APP_TIMEZONE` in `secrets.php` **and**
`TZ_INFO` in `firmware/esp32.supermini_ds1307_ac_energy_meter/config.h`.

## Heartbeat POSTs

Devices POST every ~hour even with no readings, so the server can push
fresh `log_interval_sec` / `server_time` to idle devices. Heartbeat
requests look identical to data requests but have an empty
`readings: []` array. The endpoint treats them as ordinary POSTs;
`acked_up_to_seq` will be 0.

## Security TODO list

- [ ] Replace `setInsecure()` cert pinning on the firmware side
- [ ] HMAC-sign device payloads to prevent token replay over hostile Wi-Fi
- [ ] Rate-limit `/api/login.php` per source IP
- [ ] HTTPS everywhere (rely on host-supplied Let's Encrypt cert)
- [ ] Bonding on BLE before opening Wi-Fi credential characteristic
