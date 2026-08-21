<?php
// GET /api/devices.php
// Returns the devices the current user is allowed to see, plus their latest meta.
// Admins see every device with its owner.

declare(strict_types=1);
require_once __DIR__ . '/_db.php';

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    json_response(405, ['ok' => false, 'error' => 'method_not_allowed']);
}
$user = require_login();
$pdo  = db();

// ble_pin arrives with migration 004; `$pin_col` degrades to NULL if absent so
// the endpoint keeps working on a pre-migration DB.
$admin = !empty($user['is_admin']);
$run = function (string $pin_col, string $cnt_cols) use ($pdo, $admin, $user) {
    if ($admin) {
        $st = $pdo->query(
            "SELECT d.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw,
                    d.owner_user_id, u.username AS owner_username, $pin_col,
                    m.fw_version, m.last_sync_at, m.last_seq, m.last_boot_id,
                    m.total_readings, m.log_interval_sec, $cnt_cols
               FROM ed_energy_devices d
               LEFT JOIN ed_users        u ON u.id = d.owner_user_id
               LEFT JOIN locations       l ON l.location_id = d.location
               LEFT JOIN ed_device_meta  m ON m.device_id = d.device_id
              ORDER BY d.friendly_name"
        );
    } else {
        $st = $pdo->prepare(
            "SELECT d.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw, $pin_col,
                    m.fw_version, m.last_sync_at, m.last_seq, m.last_boot_id,
                    m.total_readings, m.log_interval_sec, $cnt_cols
               FROM ed_energy_devices d
               LEFT JOIN locations      l ON l.location_id = d.location
               LEFT JOIN ed_device_meta m ON m.device_id = d.device_id
              WHERE d.owner_user_id = ?
              ORDER BY d.friendly_name"
        );
        $st->execute([$user['id']]);
    }
    return $st->fetchAll();
};
// channel_count/relay_count arrive with migrations 010/011; like ble_pin they
// degrade to a literal 1 so the endpoint keeps working on a pre-migration DB.
// The app uses channel_count to decide whether to offer a meter picker.
$cnt_real = 'm.channel_count, m.relay_count';
$cnt_stub = '1 AS channel_count, 1 AS relay_count';
$devices = null;
foreach ([['d.ble_pin', $cnt_real], ['d.ble_pin', $cnt_stub],
          ['NULL AS ble_pin', $cnt_real], ['NULL AS ble_pin', $cnt_stub]] as $combo) {
    try { $devices = $run($combo[0], $combo[1]); break; }
    catch (Throwable $e) { /* try the next, less demanding, combination */ }
}
if ($devices === null) $devices = [];
json_response(200, ['ok' => true, 'devices' => $devices]);
