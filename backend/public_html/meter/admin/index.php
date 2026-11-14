<?php
declare(strict_types=1);
require_once __DIR__ . '/../api/_db.php';
require_once __DIR__ . '/../api/_ui.php';
$user = require_admin();
$pdo  = db();

$stats = $pdo->query(
    "SELECT
       (SELECT COUNT(*) FROM ed_users)            AS user_count,
       (SELECT COUNT(*) FROM ed_energy_devices)          AS device_count,
       (SELECT COUNT(*) FROM ed_energy_devices WHERE owner_user_id IS NULL) AS unbound,
       (SELECT COUNT(*) FROM ed_energy_readings)   AS row_count,
       (SELECT MAX(received_at) FROM ed_ingest_log WHERE status='ok') AS last_ingest"
)->fetch();
$recent = $pdo->query(
    "SELECT received_at, device_id, rows_in_payload, rows_inserted, status, notes
       FROM ed_ingest_log ORDER BY received_at DESC LIMIT 25"
)->fetchAll();
?>
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AC Energy Meter — admin</title>
<meta name="theme-color" content="#f6fbf2" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#101510" media="(prefers-color-scheme: dark)">
<link rel="stylesheet" href="/dashboard/assets/style.css?v=9">
</head><body>
<header class="topbar">
  <div class="brand"><span class="logo"><?= ui_icon('shield') ?></span>Admin <small>overview</small></div>
  <div class="user">
    <a href="/dashboard/"><?= ui_icon('home') ?>Dashboard</a>
    <a href="/admin/users.php"><?= ui_icon('users') ?>Users</a>
    <a href="/admin/devices.php"><?= ui_icon('device') ?>Devices</a>
    <a href="/api/logout.php"><?= ui_icon('logout') ?>Sign out</a>
  </div>
</header>
<main class="container">
  <section class="cards stats">
    <div class="stat"><span>Users</span><b><?= (int)$stats['user_count'] ?></b></div>
    <div class="stat"><span>Devices</span><b><?= (int)$stats['device_count'] ?></b></div>
    <div class="stat"><span>Unbound devices</span><b><?= (int)$stats['unbound'] ?></b></div>
    <div class="stat"><span>Total readings</span><b><?= number_format((int)$stats['row_count']) ?></b></div>
  </section>
  <section class="card">
    <h2 class="sec"><span class="badge tint-secondary"><?= ui_icon('list') ?></span><span class="t">Recent ingest activity</span></h2>
    <table class="grid">
      <thead><tr><th>When</th><th>Device</th><th>Payload</th><th>Inserted</th><th>Status</th><th>Notes</th></tr></thead>
      <tbody>
      <?php foreach ($recent as $r): ?>
        <tr>
          <td><?= h($r['received_at']) ?></td>
          <td><?= h($r['device_id'] ?? '—') ?></td>
          <td><?= h((string)($r['rows_in_payload'] ?? '')) ?></td>
          <td><?= h((string)($r['rows_inserted'] ?? '')) ?></td>
          <td class="status status-<?= h($r['status']) ?>"><?= h($r['status']) ?></td>
          <td><?= h((string)($r['notes'] ?? '')) ?></td>
        </tr>
      <?php endforeach; ?>
      </tbody>
    </table>
  </section>
</main>
</body></html>
