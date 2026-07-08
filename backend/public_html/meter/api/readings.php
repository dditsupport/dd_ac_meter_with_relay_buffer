<?php
// GET /meter/api/readings.php?device_id=X&from=ISO&to=ISO&aggregate=raw|hourly|daily|monthly
// Auth: session (browser/app). Returns JSON.
//
// aggregate=daily / monthly compute generated kWh as MAX(energy_wh)-MIN(energy_wh)
// per bucket — works because the PZEM Wh counter is monotonically increasing
// across resets (and the firmware logs PZEM resets if they happen).

declare(strict_types=1);
require_once __DIR__ . '/_db.php';

// The PZEM-004T energy register counts in Wh and wraps back to 0 when it passes
// 9999.99 kWh (= 9,999,990 Wh). Differencing across that wrap must add the span
// past the ceiling instead of reading a huge negative delta.
const PZEM_WH_ROLLOVER = 9999990.0;

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    json_response(405, ['ok' => false, 'error' => 'method_not_allowed']);
}
$user = require_login();

$device_id = (string)($_GET['device_id'] ?? '');
$aggregate = (string)($_GET['aggregate'] ?? 'raw');
$from      = (string)($_GET['from'] ?? '');
$to        = (string)($_GET['to']   ?? '');

if ($device_id === '' || !user_can_see_device($user, $device_id)) {
    json_response(403, ['ok' => false, 'error' => 'no_such_device']);
}

if (!in_array($aggregate, ['raw', '5min', 'hourly', 'daily', 'monthly'], true)) {
    json_response(400, ['ok' => false, 'error' => 'bad_aggregate']);
}

[$from_str, $to_str] = resolve_range($aggregate, $from, $to);

$pdo  = db();
$meta = $pdo->prepare(
    'SELECT d.friendly_name, d.location, l.location_name, d.capacity_kw
       FROM ed_energy_devices d
       LEFT JOIN locations l ON l.location_id = d.location
      WHERE d.device_id = ?'
);
$meta->execute([$device_id]);
$dev  = $meta->fetch() ?: [];

// Bucket key expressions. All operate on wall_time's calendar components, so
// they are unaffected by the MySQL session time zone (which _db.php pins to
// APP_TIMEZONE anyway). The 5-minute bucket floors the minute to the nearest
// multiple of 5 on top of the hour.
$FIVE_MIN_BUCKET = "DATE_ADD(DATE_FORMAT(wall_time, '%Y-%m-%d %H:00:00'), "
                 . "INTERVAL FLOOR(MINUTE(wall_time) / 5) * 5 MINUTE)";

$points = match ($aggregate) {
    'raw'     => fetch_raw($device_id, $from_str, $to_str),
    '5min'    => fetch_bucketed($device_id, $from_str, $to_str, $FIVE_MIN_BUCKET),
    'hourly'  => fetch_bucketed($device_id, $from_str, $to_str, "DATE_FORMAT(wall_time, '%Y-%m-%d %H:00:00')"),
    'daily'   => fetch_bucketed($device_id, $from_str, $to_str, "DATE_FORMAT(wall_time, '%Y-%m-%d 00:00:00')", true),
    'monthly' => fetch_bucketed($device_id, $from_str, $to_str, "DATE_FORMAT(wall_time, '%Y-%m-01 00:00:00')", true),
};

// Whole-range energy total, in kWh. Normally the PZEM Wh register climbs
// monotonically, so this is just last-first (== MAX-MIN). If it went backwards
// over the window the register either WRAPPED past its 9999.99 kWh ceiling
// (peak MAX near the ceiling) or was RESET to 0 (the fresh-install BOOT
// long-press). Handle each so a wrap/reset doesn't explode the total to
// ~10,000 kWh or show a bogus negative. NULL when the range has no readings.
$rt = $pdo->prepare(
    'SELECT MIN(energy_wh) AS mn, MAX(energy_wh) AS mx,
            (SELECT energy_wh FROM ed_energy_readings
               WHERE device_id = ? AND wall_time BETWEEN ? AND ?
               ORDER BY wall_time ASC, id ASC LIMIT 1) AS fst,
            (SELECT energy_wh FROM ed_energy_readings
               WHERE device_id = ? AND wall_time BETWEEN ? AND ?
               ORDER BY wall_time DESC, id DESC LIMIT 1) AS lst
       FROM ed_energy_readings
      WHERE device_id = ? AND wall_time BETWEEN ? AND ?'
);
$rt->execute([$device_id, $from_str, $to_str,
              $device_id, $from_str, $to_str,
              $device_id, $from_str, $to_str]);
$row = $rt->fetch();
if ($row === false || $row['fst'] === null || $row['lst'] === null) {
    $total_kwh = null;
} else {
    $fst = (float)$row['fst']; $lst = (float)$row['lst'];
    $mn  = (float)$row['mn'];  $mx  = (float)$row['mx'];
    if ($lst >= $fst) {
        $total_wh = $mx - $mn;                         // monotonic over the window
    } elseif ($mx >= PZEM_WH_ROLLOVER * 0.99) {
        $total_wh = ($mx - $fst) + $lst;               // wrapped past the ceiling
    } else {
        $total_wh = $lst;                              // reset to 0 -> post-reset only
    }
    $total_kwh = round(max(0.0, $total_wh) / 1000.0, 3);
}

json_response(200, [
    'ok'            => true,
    'device_id'     => $device_id,
    'friendly_name' => $dev['friendly_name'] ?? $device_id,
    'location'      => isset($dev['location']) ? (int)$dev['location'] : null,
    'location_name' => $dev['location_name'] ?? null,
    'capacity_kw'   => $dev['capacity_kw'] ?? null,
    'from'          => $from_str,
    'to'            => $to_str,
    'aggregate'     => $aggregate,
    'total_kwh'     => $total_kwh,
    'points'        => $points,
]);


/* ---------- helpers ---------- */
function resolve_range(string $agg, string $from, string $to): array {
    $tz = new DateTimeZone(APP_TIMEZONE);
    $now = new DateTimeImmutable('now', $tz);
    $to_dt   = $to   !== '' ? new DateTimeImmutable($to,   $tz) : $now;
    $from_dt = $from !== '' ? new DateTimeImmutable($from, $tz) : $to_dt->modify('-1 day');

    // Sensible defaults per aggregate
    if ($from === '') {
        $from_dt = match ($agg) {
            'raw'     => $to_dt->modify('-1 day'),
            '5min'    => $to_dt->modify('-1 day'),
            'hourly'  => $to_dt->modify('-7 days'),
            'daily'   => $to_dt->modify('-30 days'),
            'monthly' => $to_dt->modify('-12 months'),
        };
    }
    return [
        $from_dt->format('Y-m-d H:i:s'),
        $to_dt->format('Y-m-d H:i:s'),
    ];
}

function fetch_raw(string $device, string $from, string $to): array {
    $st = db()->prepare(
        'SELECT wall_time, voltage, current_a, power_w, energy_wh, power_factor,
                frequency_hz, time_confidence
           FROM ed_energy_readings
          WHERE device_id = ? AND wall_time BETWEEN ? AND ?
          ORDER BY wall_time ASC
          LIMIT 5000'
    );
    $st->execute([$device, $from, $to]);
    return array_map(fn($r) => [
        't'    => format_iso($r['wall_time']),
        'V'    => (float)$r['voltage'],
        'I'    => (float)$r['current_a'],
        'P'    => (float)$r['power_w'],
        'Wh'   => (float)$r['energy_wh'],
        'PF'   => (float)$r['power_factor'],
        'Hz'   => $r['frequency_hz'] !== null ? (float)$r['frequency_hz'] : null,
        'conf' => $r['time_confidence'],
    ], $st->fetchAll());
}

function fetch_bucketed(string $device, string $from, string $to, string $bucketExpr,
                       bool $spanToNext = false): array {
    // Per-bucket: max-min of PZEM cumulative Wh = energy generated in bucket.
    // Plus avg/peak power for context. $bucketExpr is a server-side constant
    // (never user input), so it is safe to interpolate into the SQL text.
    $st = db()->prepare(
        "SELECT $bucketExpr        AS bucket,
                MIN(wall_time)      AS bucket_start,
                MAX(wall_time)      AS bucket_end,
                MIN(energy_wh)      AS wh_min,
                MAX(energy_wh)      AS wh_max,
                AVG(power_w)        AS p_avg,
                MAX(power_w)        AS p_peak,
                AVG(voltage)        AS v_avg,
                COUNT(*)            AS samples,
                SUM(time_confidence='approx') AS approx_count
           FROM ed_energy_readings
          WHERE device_id = ? AND wall_time BETWEEN ? AND ?
          GROUP BY bucket
          ORDER BY bucket ASC
          LIMIT 5000"
    );
    $st->execute([$device, $from, $to]);
    $rows = $st->fetchAll();
    $n    = count($rows);

    $out = [];
    foreach ($rows as $i => $r) {
        // Bucket energy. Default: this bucket's own max-min. In span-to-next
        // mode (daily/monthly) the bucket instead runs from its first reading to
        // the NEXT present bucket's first reading, so the gap between buckets
        // (e.g. overnight, day-to-day) is charged to the earlier bucket and the
        // buckets sum exactly to the range's start->end total. The last bucket
        // has no "next", so it keeps its own max-min to absorb the tail up to
        // the latest reading.
        if ($spanToNext && $i < $n - 1) {
            $kwh = wh_span_kwh((float)$r['wh_min'], (float)$rows[$i + 1]['wh_min']);
        } else {
            $kwh = max(0.0, ((float)$r['wh_max'] - (float)$r['wh_min']) / 1000.0);
        }
        $out[] = [
            't'        => format_iso($r['bucket']),
            't_end'    => format_iso($r['bucket_end']),
            'kwh'      => round($kwh, 3),
            'P_avg'    => round((float)$r['p_avg'], 1),
            'P_peak'   => round((float)$r['p_peak'], 1),
            'V_avg'    => round((float)$r['v_avg'], 1),
            'samples'  => (int)$r['samples'],
            'approx'   => (int)$r['approx_count'] > 0,
        ];
    }
    return $out;
}

function format_iso(string $datetime): string {
    return (new DateTimeImmutable($datetime, new DateTimeZone(APP_TIMEZONE)))
        ->format('c');
}

// Energy consumed between two cumulative Wh register readings, in kWh. A drop
// (curr < prev) means the register either wrapped past its 9999.99 kWh ceiling
// or was reset to 0. When prev is near the ceiling treat it as a wrap and add
// the span past it; a drop from a value not near the ceiling is a meter reset
// (fresh-install BOOT long-press), counted as no consumption so it never
// inflates a bucket's total.
function wh_span_kwh(float $prev, float $curr): float {
    $d = $curr - $prev;
    if ($d < 0) {
        $d = $prev >= PZEM_WH_ROLLOVER * 0.99
             ? (PZEM_WH_ROLLOVER - $prev) + $curr   // wrapped past the ceiling
             : 0.0;                                 // reset / anomaly
    }
    return max(0.0, $d) / 1000.0;
}
