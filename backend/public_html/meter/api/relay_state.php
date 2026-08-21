<?php
// GET /api/relay_state.php?device_id=X
// Session-authed. Returns the device's last-reported relay state for the
// live dashboard indicator. Respects per-user device visibility. The state
// is whatever the firmware last reported on an ingest POST.

declare(strict_types=1);
require_once __DIR__ . '/_db.php';

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    json_response(405, ['ok' => false, 'error' => 'method_not_allowed']);
}
$user = require_login();

$device_id = (string)($_GET['device_id'] ?? '');
if ($device_id === '' || !user_can_see_device($user, $device_id)) {
    json_response(403, ['ok' => false, 'error' => 'no_such_device']);
}

$pdo = db();
// Guarded: a DB without migration 003 (relay_* columns) returns "unknown".
try {
    $st = $pdo->prepare(
        'SELECT relay_on, relay_mode, relay_reported_at, log_interval_sec
           FROM ed_device_meta WHERE device_id = ?'
    );
    $st->execute([$device_id]);
    $r = $st->fetch() ?: null;
} catch (Throwable $e) {
    $r = null;
}

// Per-relay state (migration 012). A multi-relay device has one row per relay;
// the flat fields above only ever carried relay 1. Guarded so a DB without the
// table still answers with the single-relay shape.
$relays = [];
try {
    $rs = $pdo->prepare(
        'SELECT channel, relay_on, relay_mode, version, reported_at
           FROM ed_device_relay_state WHERE device_id = ? ORDER BY channel'
    );
    $rs->execute([$device_id]);
    foreach ($rs->fetchAll() as $row) {
        $relays[] = [
            'ch'          => (int)$row['channel'],
            'on'          => $row['relay_on'] !== null ? (bool)(int)$row['relay_on'] : null,
            'mode'        => $row['relay_mode'],
            'version'     => (int)$row['version'],
            'reported_at' => $row['reported_at'],
        ];
    }
} catch (Throwable $e) {
    $relays = [];
}
// Fall back to the device-level fields so a caller always gets at least one
// entry to render, even before migration 012 or the device's first sync.
if (!$relays && $r) {
    $relays[] = [
        'ch'          => 1,
        'on'          => $r['relay_on'] !== null ? (bool)(int)$r['relay_on'] : null,
        'mode'        => $r['relay_mode'],
        'version'     => 0,
        'reported_at' => $r['relay_reported_at'],
    ];
}

json_response(200, [
    'ok'          => true,
    // Flat fields kept for existing callers: they mirror relay 1.
    'on'          => ($r && $r['relay_on'] !== null) ? (bool)(int)$r['relay_on'] : null,
    'mode'        => $r['relay_mode'] ?? null,
    'reported_at' => $r['relay_reported_at'] ?? null,
    'interval'    => (int)($r['log_interval_sec'] ?? 900),
    'relays'      => $relays,
]);
