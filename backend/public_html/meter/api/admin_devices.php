<?php
// Admin-only device management.
//   action=list           -> all devices + owner + meta
//   action=bind           -> assign owner_user_id to a device (or null to unbind)
//   action=rename         -> set friendly_name / location / capacity_kw / notes
//   action=set_interval   -> override ed_device_meta.log_interval_sec (0 = use default)
//   action=set_maintenance-> override the nightly-reboot window and radio rest
//                            (blank field = clear the override, use the fleet default)
//   action=regen_pin      -> generate a new random BLE access PIN, returns it
//   action=set_pin        -> set a specific BLE access PIN (6 digits), returns it
//   action=delete         -> delete device + all its readings (cascades)

declare(strict_types=1);
require_once __DIR__ . '/_db.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_response(405, ['ok' => false, 'error' => 'method_not_allowed']);
}
require_admin();
check_csrf();

$pdo    = db();
$action = (string)($_POST['action'] ?? '');

switch ($action) {

case 'list':
    $rows = $pdo->query(
        'SELECT d.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw, d.notes,
                d.owner_user_id, u.username AS owner_username, d.first_seen_at,
                m.fw_version, m.last_sync_at, m.last_seq, m.last_boot_id,
                m.total_readings, m.log_interval_sec
           FROM ed_energy_devices d
           LEFT JOIN ed_users        u ON u.id = d.owner_user_id
           LEFT JOIN locations       l ON l.location_id = d.location
           LEFT JOIN ed_device_meta  m ON m.device_id = d.device_id
          ORDER BY d.friendly_name'
    )->fetchAll();
    json_response(200, ['ok' => true, 'devices' => $rows]);

case 'bind':
    $device_id = (string)($_POST['device_id'] ?? '');
    $user_id   = $_POST['user_id'] ?? '';
    $user_id   = ($user_id === '' || $user_id === '0') ? null : (int)$user_id;
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);
    if ($user_id !== null) {
        $st = $pdo->prepare('SELECT 1 FROM ed_users WHERE id = ?');
        $st->execute([$user_id]);
        if (!$st->fetchColumn()) json_response(404, ['ok' => false, 'error' => 'no_such_user']);
    }
    $pdo->prepare('UPDATE ed_energy_devices SET owner_user_id = ? WHERE device_id = ?')
        ->execute([$user_id, $device_id]);
    json_response(200, ['ok' => true]);

case 'rename':
    $device_id    = (string)($_POST['device_id'] ?? '');
    $friendly     = trim((string)($_POST['friendly_name'] ?? ''));
    // location is now a WorkPulse locations.location_id (int), or null.
    $location     = ($_POST['location'] ?? '') === '' ? null : (int)$_POST['location'];
    $capacity_kw  = $_POST['capacity_kw'] === '' || !isset($_POST['capacity_kw'])
                        ? null : (float)$_POST['capacity_kw'];
    $notes        = trim((string)($_POST['notes'] ?? '')) ?: null;
    if ($device_id === '' || $friendly === '') {
        json_response(400, ['ok' => false, 'error' => 'bad_input']);
    }
    if ($location !== null) {
        $st = $pdo->prepare('SELECT 1 FROM locations WHERE location_id = ?');
        $st->execute([$location]);
        if (!$st->fetchColumn()) json_response(404, ['ok' => false, 'error' => 'no_such_location']);
    }
    $pdo->prepare(
        'UPDATE ed_energy_devices SET friendly_name = ?, location = ?, capacity_kw = ?, notes = ? WHERE device_id = ?'
    )->execute([$friendly, $location, $capacity_kw, $notes, $device_id]);
    json_response(200, ['ok' => true]);

case 'set_interval':
    $device_id = (string)($_POST['device_id'] ?? '');
    $sec       = (int)($_POST['log_interval_sec'] ?? 0);
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);
    if ($sec !== 0 && ($sec < 60 || $sec > 86400)) {
        json_response(400, ['ok' => false, 'error' => 'interval_out_of_range']);
    }
    $pdo->prepare(
        'INSERT INTO ed_device_meta (device_id, log_interval_sec) VALUES (?, ?)
         ON DUPLICATE KEY UPDATE log_interval_sec = VALUES(log_interval_sec)'
    )->execute([$device_id, $sec ?: 900]);
    json_response(200, ['ok' => true]);

case 'set_maintenance':
    // Nightly reboot window + periodic radio rest, pushed on the device's next
    // ingest response. Every field is optional: an EMPTY string clears the
    // override so the device falls back to the fleet default in api/_db.php,
    // which is what "blank means don't manage this device" means in the UI.
    // A present value is validated against the same bounds the firmware uses,
    // so a bad entry is refused here instead of being stored and then silently
    // ignored by every device that receives it.
    $device_id = (string)($_POST['device_id'] ?? '');
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);

    $fields = ['nightly_reboot_enable', 'nightly_reboot_start_hour', 'nightly_reboot_end_hour',
               'radio_rest_interval_sec', 'radio_rest_duration_sec'];
    $set = [];
    $vals = [];
    foreach ($fields as $f) {
        if (!array_key_exists($f, $_POST)) continue;      // field not submitted: leave as is
        $raw = trim((string)$_POST[$f]);
        if ($raw === '') {                                 // blank: clear the override
            $set[] = "$f = NULL";
            continue;
        }
        if (!preg_match('/^-?[0-9]+$/', $raw)) {
            json_response(400, ['ok' => false, 'error' => "not_a_number:$f"]);
        }
        $v = (int)$raw;
        if (!maintenance_value_ok($f, $v)) {
            json_response(400, ['ok' => false, 'error' => "out_of_range:$f"]);
        }
        $set[]  = "$f = ?";
        $vals[] = $v;
    }
    if (!$set) json_response(400, ['ok' => false, 'error' => 'nothing_to_set']);

    // The window must be a non-empty span inside one local day. Check the
    // EFFECTIVE pair — one hour submitted, the other already stored — so a
    // half-update cannot leave an unusable window behind.
    $eff = device_maintenance_config($pdo, $device_id);
    $start = array_key_exists('nightly_reboot_start_hour', $_POST) && trim((string)$_POST['nightly_reboot_start_hour']) !== ''
        ? (int)$_POST['nightly_reboot_start_hour'] : (int)($eff['nightly_reboot_start_hour'] ?? DEFAULT_NIGHTLY_REBOOT_START_HOUR);
    $end = array_key_exists('nightly_reboot_end_hour', $_POST) && trim((string)$_POST['nightly_reboot_end_hour']) !== ''
        ? (int)$_POST['nightly_reboot_end_hour'] : (int)($eff['nightly_reboot_end_hour'] ?? DEFAULT_NIGHTLY_REBOOT_END_HOUR);
    if ($end <= $start) {
        json_response(400, ['ok' => false, 'error' => 'reboot_window_empty']);
    }

    // The row may not exist yet (device registered but never synced).
    $pdo->prepare('INSERT IGNORE INTO ed_device_meta (device_id) VALUES (?)')->execute([$device_id]);
    try {
        $vals[] = $device_id;
        $pdo->prepare('UPDATE ed_device_meta SET ' . implode(', ', $set) . ' WHERE device_id = ?')
            ->execute($vals);
    } catch (Throwable $e) {
        // Columns absent => migration 014 has not been applied on this DB.
        json_response(500, ['ok' => false, 'error' => 'migration_014_required']);
    }
    json_response(200, ['ok' => true,
                        'config' => maintenance_config_for_response(device_maintenance_config($pdo, $device_id))]);

case 'regen_pin':
    $device_id = (string)($_POST['device_id'] ?? '');
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);
    $exists = $pdo->prepare('SELECT 1 FROM ed_energy_devices WHERE device_id = ?');
    $exists->execute([$device_id]);
    if (!$exists->fetchColumn()) json_response(404, ['ok' => false, 'error' => 'no_such_device']);
    $pin = gen_ble_pin();
    $pdo->prepare('UPDATE ed_energy_devices SET ble_pin = ? WHERE device_id = ?')
        ->execute([$pin, $device_id]);
    json_response(200, ['ok' => true, 'ble_pin' => $pin]);

case 'set_pin':
    // Manual counterpart to regen_pin, for handing out a memorable or
    // pre-agreed PIN instead of a random one.
    $device_id = (string)($_POST['device_id'] ?? '');
    $pin       = trim((string)($_POST['ble_pin'] ?? ''));
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);
    // Exactly six digits — the same shape gen_ble_pin() produces and the
    // migration backfilled. The Android app compares the typed PIN against
    // this string verbatim, so a longer or non-numeric value would lock the
    // owner out of their own meter (its PIN field is digits-only).
    if (!preg_match('/^[0-9]{6}$/', $pin)) {
        json_response(400, ['ok' => false, 'error' => 'bad_pin']);
    }
    $exists = $pdo->prepare('SELECT 1 FROM ed_energy_devices WHERE device_id = ?');
    $exists->execute([$device_id]);
    if (!$exists->fetchColumn()) json_response(404, ['ok' => false, 'error' => 'no_such_device']);
    $pdo->prepare('UPDATE ed_energy_devices SET ble_pin = ? WHERE device_id = ?')
        ->execute([$pin, $device_id]);
    json_response(200, ['ok' => true, 'ble_pin' => $pin]);

case 'delete':
    $device_id = (string)($_POST['device_id'] ?? '');
    if ($device_id === '') json_response(400, ['ok' => false, 'error' => 'bad_input']);
    $pdo->prepare('DELETE FROM ed_energy_devices WHERE device_id = ?')->execute([$device_id]);
    json_response(200, ['ok' => true]);

default:
    json_response(400, ['ok' => false, 'error' => 'unknown_action']);
}
