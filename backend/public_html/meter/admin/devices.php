<?php
declare(strict_types=1);
require_once __DIR__ . '/../api/_db.php';
require_admin();
$pdo = db();

// Optional columns arrive with migrations (relay_* = 003, ble_pin = 004).
// Try progressively fewer of them so the page still renders on an un-migrated
// DB.
$base_cols  = 'd.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw,
               d.owner_user_id, u.username AS owner_username, d.first_seen_at,
               (SELECT s.schedule_json FROM ed_device_relay_schedule s
                 WHERE s.device_id = d.device_id LIMIT 1) AS relay_schedule_json,
               m.fw_version, m.last_sync_at, m.last_seq, m.last_boot_id,
               m.total_readings, m.log_interval_sec';
$opt_relay  = ', m.relay_on, m.relay_mode, m.relay_reported_at';
$opt_drift  = ', m.rtc_drift_sec, m.rtc_drift_at';
$opt_rssi   = ', m.wifi_rssi';
$opt_coin   = ', m.coincell_mv';
$opt_pin    = ', d.ble_pin';
$opt_counts = ', m.channel_count, m.relay_count';
// NOTE: relay schedules are keyed (device_id, channel) since migration 011, so
// ed_device_relay_schedule must NOT be joined here — a 3-relay device would
// otherwise return three rows and appear three times in this list. The
// schedule-present indicator above uses a LIMIT 1 subquery instead, which is
// all it needs (it only asks "does any schedule exist").
$from       = ' FROM ed_energy_devices d
                LEFT JOIN ed_users               u ON u.id = d.owner_user_id
                LEFT JOIN locations              l ON l.location_id = d.location
                LEFT JOIN ed_device_meta         m ON m.device_id = d.device_id
               ORDER BY d.friendly_name';

// Firmware version at which the relay convention flipped to "AC-allowed open
// hours". Devices older than this energize INSIDE the schedule window (cutting
// AC during business hours), so we warn if such a device has a schedule set.
$RELAY_CONV_FW = '2.0.0';
$devices = [];
foreach ([
    $opt_pin . $opt_relay . $opt_drift . $opt_rssi . $opt_coin . $opt_counts,
    $opt_pin . $opt_relay . $opt_drift . $opt_rssi . $opt_coin,
    $opt_pin . $opt_relay . $opt_drift . $opt_rssi,
    $opt_pin . $opt_relay . $opt_drift,
    $opt_pin . $opt_relay,
    $opt_relay,
    $opt_pin,
    '',
] as $extra) {
    try { $devices = $pdo->query("SELECT $base_cols $extra $from")->fetchAll(); break; }
    catch (Throwable $e) { /* missing column — try a leaner select */ }
}

$users = $pdo->query('SELECT id, username FROM ed_users ORDER BY username')->fetchAll();
// WorkPulse stores, for the per-device location dropdown.
$locations = $pdo->query(
    'SELECT location_id, location_name FROM locations WHERE is_active = 1 ORDER BY location_name'
)->fetchAll();
?>
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AC Energy Meter — devices</title>
<link rel="stylesheet" href="/dashboard/assets/style.css?v=8">
<style>
  /* Devices as compact 2-line cards — everything fits the viewport width by
     wrapping, so there is no left/right scrolling. */
  .dev-list { display: flex; flex-direction: column; gap: 0.55rem; }
  .dev      { border: 1px solid var(--border); border-radius: 8px; padding: 0.6rem 0.8rem; background: #fff; }
  .dev:nth-child(even) { background: #fafbf8; }
  .dev-row  { display: flex; flex-wrap: wrap; gap: 0.5rem 0.9rem; align-items: flex-end; }
  .dev-row.line2 { margin-top: 0.55rem; padding-top: 0.55rem; border-top: 1px dashed var(--border);
                   align-items: center; }
  .field    { display: flex; flex-direction: column; gap: 0.15rem; min-width: 0; }
  .field > .lbl { font-size: 0.66rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.04em; }
  .field input, .field select { box-sizing: border-box; width: 100%; }
  .dev .f-id       { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
                     font-size: 0.82rem; white-space: nowrap; align-self: center; }
  .dev .f-name     { flex: 1 1 10rem; }
  .dev .f-location { flex: 3 1 18rem; }   /* location gets the most room */
  .dev .f-cap      { flex: 0 0 5rem; }
  .dev .f-owner    { flex: 1 1 9rem; }
  .dev .f-interval { flex: 0 0 auto; }
  .dev .f-interval .iwrap { display: flex; gap: 0.3rem; }
  .dev .f-interval input   { width: 5.5rem; }
  .dev-meta { font-size: 0.8rem; color: var(--muted); white-space: nowrap; }
  .dev-meta b { color: var(--text); font-weight: 600; }
  .relay-dot { display: inline-block; width: 0.62rem; height: 0.62rem; border-radius: 50%;
               margin-right: 0.4rem; vertical-align: middle; background: #c8ccc4; }
  .relay-dot.on      { background: #1f9d3a; box-shadow: 0 0 0 3px rgba(31,157,58,0.18); }
  .relay-dot.off     { background: #98a09a; }
  .relay-dot.stale   { background: #d8a200; }
  .relay-dot.unknown { background: #c8ccc4; }
  .col-relay { white-space: nowrap; font-size: 0.85rem; color: var(--muted); }
  .col-relay .relay-label { color: var(--text); vertical-align: middle; }
  .col-relay .relay-warn  { margin-left: 0.4rem; color: #b8860b; cursor: help;
                            font-size: 0.95rem; vertical-align: middle; }
  .dev-pin { white-space: nowrap; font-size: 0.8rem; color: var(--muted); }
  .dev-pin code.pin { font-size: 0.95rem; letter-spacing: 0.06em; color: var(--text); }
  .dev-pin .set-pin,
  .dev-pin .regen-pin { margin-left: 0.3rem; padding: 0.1rem 0.45rem; font-size: 0.9rem; }
  .dev-pin .set-pin   { font-size: 0.78rem; }
  .dev-actions { margin-left: auto; display: flex; gap: 0.4rem; align-items: center; }
  .dev-actions a { font-size: 0.85rem; }
</style>
</head><body>
<header class="topbar">
  <div class="brand">AC Energy Meter — admin</div>
  <div class="user">
    <a href="/admin/">overview</a>
    &middot; <a href="/admin/users.php">users</a>
    &middot; <a href="/api/logout.php">sign out</a>
  </div>
</header>
<main class="container">
  <section class="card">
    <h2>Devices</h2>
    <p class="muted">Devices auto-register on first ingest POST. Assign each one to a user below.
       A <span style="color:#b8860b">⚠</span> in the Relay column means the device's firmware
       predates v2.0.0 and would <b>cut the AC during open hours</b> — flash &ge; 2.0.0.</p>
    <div class="dev-list">
      <?php foreach ($devices as $d):
        // Old-convention warning: firmware < $RELAY_CONV_FW energizes the relay
        // INSIDE the schedule window, so a schedule on it cuts the AC during
        // open hours (the inverse of what the schedule now means).
        $fw        = (string)($d['fw_version'] ?? '');
        $fwOld     = version_compare($fw !== '' ? $fw : '0', $RELAY_CONV_FW, '<');
        $schedJson = (string)($d['relay_schedule_json'] ?? '');
        $schedSet  = $schedJson !== '' && trim($schedJson) !== '[]';
        $relayRisk = $fwOld && $schedSet;
      ?>
        <div class="dev" data-id="<?= h($d['device_id']) ?>" data-fw-old="<?= $relayRisk ? '1' : '' ?>"
             data-relay-count="<?= (int)($d['relay_count'] ?? 1) ?>">
          <div class="dev-row line1">
            <span class="f-id" title="Device ID"><?= h($d['device_id']) ?></span>
            <label class="field f-name"><span class="lbl">Friendly name</span>
              <input class="name" value="<?= h($d['friendly_name']) ?>"></label>
            <label class="field f-location"><span class="lbl">Location</span>
              <select class="location">
                <option value="">— none —</option>
                <?php foreach ($locations as $loc): ?>
                  <option value="<?= (int)$loc['location_id'] ?>"
                    <?= (int)($d['location'] ?? 0) === (int)$loc['location_id'] ? 'selected' : '' ?>>
                    <?= h($loc['location_name']) ?>
                  </option>
                <?php endforeach; ?>
              </select></label>
            <label class="field f-cap"
                   title="Old meter's last reading (kWh) at install. Added to the dashboard Today &amp; Period totals so the figures continue from the replaced meter."><span class="lbl">Old kWh</span>
              <input class="capacity" type="number" step="0.01" min="0" placeholder="—"
                     value="<?= h((string)($d['capacity_kw'] ?? '')) ?>"></label>
            <label class="field f-owner"><span class="lbl">Owner</span>
              <select class="owner">
                <option value="">— unassigned —</option>
                <?php foreach ($users as $u): ?>
                  <option value="<?= (int)$u['id'] ?>"
                    <?= $u['id'] == ($d['owner_user_id'] ?? -1) ? 'selected' : '' ?>>
                    <?= h($u['username']) ?>
                  </option>
                <?php endforeach; ?>
              </select></label>
          </div>
          <div class="dev-row line2">
            <label class="field f-interval"><span class="lbl">Interval (s)</span>
              <span class="iwrap">
                <input class="interval" type="number" min="60" max="86400" step="1"
                       value="<?= (int)($d['log_interval_sec'] ?? 900) ?>">
                <button class="set-interval">Set</button>
              </span></label>
            <span class="dev-meta">Sync: <b><?= h((string)($d['last_sync_at'] ?? '—')) ?></b></span>
            <span class="dev-meta">FW: <b><?= h((string)($d['fw_version'] ?? '—')) ?></b></span>
            <span class="dev-meta">Rows: <b><?= number_format((int)($d['total_readings'] ?? 0)) ?></b></span>
            <?php if (isset($d['rtc_drift_sec']) && $d['rtc_drift_sec'] !== null): ?>
              <span class="dev-meta" title="DS1307 vs NTP at <?= h((string)($d['rtc_drift_at'] ?? '')) ?> IST — positive = RTC running fast">RTC drift: <b><?= ((int)$d['rtc_drift_sec'] > 0 ? '+' : '') . (int)$d['rtc_drift_sec'] ?>s</b></span>
            <?php endif; ?>
            <?php if (isset($d['wifi_rssi']) && $d['wifi_rssi'] !== null): ?>
              <span class="dev-meta" title="Connected-AP Wi-Fi signal at the last report">Signal: <b><?= (int)$d['wifi_rssi'] ?> dBm</b></span>
            <?php endif; ?>
            <?php if (isset($d['coincell_mv']) && $d['coincell_mv'] !== null): ?>
              <span class="dev-meta" title="CR2032 RTC-backup coin-cell voltage at the last report — replace when it nears ~2.0 V">Coin cell: <b><?= number_format((int)$d['coincell_mv'] / 1000, 2) ?> V<?= (int)$d['coincell_mv'] < 2500 ? ' (low)' : '' ?></b></span>
            <?php endif; ?>
            <span class="col-relay"
                  data-on="<?= array_key_exists('relay_on', $d) && $d['relay_on'] !== null ? (int)$d['relay_on'] : '' ?>"
                  data-mode="<?= h((string)($d['relay_mode'] ?? '')) ?>"
                  data-at="<?= h((string)($d['relay_reported_at'] ?? '')) ?>"
                  data-int="<?= (int)($d['log_interval_sec'] ?? 900) ?>">
              Relay: <span class="relay-dot unknown"></span><span class="relay-label">—</span>
              <?php if ($relayRisk): ?>
                <span class="relay-warn"
                      title="Firmware <?= h($fw ?: '?') ?> uses the old relay convention — this schedule CUTS the AC during open hours. Flash &ge; <?= h($RELAY_CONV_FW) ?>.">⚠</span>
              <?php endif; ?>
            </span>
            <span class="dev-pin">PIN: <code class="pin"><?= h((string)($d['ble_pin'] ?? '—')) ?></code><button class="set-pin" title="Set a specific BLE PIN">Set</button><button class="regen-pin" title="Generate a new random BLE PIN">↻</button></span>
            <span class="dev-actions">
              <button class="rename">Save</button>
              <button class="relay">Relay</button>
              <a href="/dashboard/?device_id=<?= urlencode($d['device_id']) ?>">view</a>
              <button class="danger delete-device">Delete</button>
            </span>
          </div>
        </div>
      <?php endforeach; ?>
    </div>
  </section>
</main>

<!-- Relay-schedule editor dialog. -->
<dialog id="relay-dialog" class="relay-dialog">
  <form method="dialog">
    <h3 style="margin:0 0 0.5rem">AC-cutoff relay for <span id="relay-dev"></span></h3>
    <p id="relay-ch-wrap" class="muted" style="margin:0 0 0.75rem" hidden>
      Relay
      <select id="relay-ch"></select>
      &mdash; each relay has its own schedule and cuts only the AC its own meter measures.
      Switching here loads that relay's windows; save applies to the selected relay only.
    </p>
    <div id="relay-fw-warn" class="relay-fw-warn" hidden>
      ⚠ This device reports firmware &lt; 2.0.0, which uses the <b>old</b> relay convention —
      it energizes <i>inside</i> the window, so a schedule here <b>cuts the AC during open hours</b>.
      Flash firmware &ge; 2.0.0 before relying on this schedule.
    </div>
    <p class="muted" style="margin:0 0 0.75rem">
      These windows are the <b>AC-allowed open hours</b>. The AC keeps power during them; the relay
      <b>cuts</b> the AC outside them (a safety net for off hours). No windows = AC always allowed.
      <br>If <b>Off</b> is earlier than <b>On</b>, the window runs <b>overnight</b> into the next day
      — e.g. On 09:00, Off 02:00 means open 9 AM through 2 AM the next morning. The <b>+1 day</b>
      tick lights up and the selected days are the days the window <i>starts</i>.
    </p>
    <table class="grid relay-grid">
      <thead><tr>
        <th>Days</th><th>Open (On)</th><th>Close (Off)</th><th>+1&nbsp;day</th><th></th>
      </tr></thead>
      <tbody id="relay-rows"></tbody>
    </table>
    <p id="relay-empty" class="relay-empty" hidden>
      No windows &mdash; the AC is always allowed. Add one to restrict it to open hours.
    </p>
    <div style="margin-top:0.75rem;">
      <button type="button" id="relay-add">+ Add window</button>
    </div>
    <fieldset class="relay-knobs">
      <legend>Compressor-aware cutoff</legend>
      <p class="muted" style="margin:0 0 0.5rem">
        At closing the relay waits for the compressor to cycle off before cutting, so it never opens
        under load. It cuts by the grace deadline at the latest, then stays cut until the next
        open-hours start.
      </p>
      <label>Compressor threshold (W)
        <input type="number" id="relay-cw" min="100" max="10000" step="50" value="800">
      </label>
      <label>Grace before cut (min)
        <input type="number" id="relay-gm" min="1" max="240" step="1" value="60">
      </label>
    </fieldset>
    <div class="relay-actions" style="margin-top:0.75rem; display:flex; gap:0.5rem;">
      <span style="flex:1"></span>
      <button type="button" id="relay-cancel">Cancel</button>
      <button type="button" id="relay-save">Save</button>
    </div>
  </form>
</dialog>

<style>
/* Browsers without <dialog> render it as a plain inline element, i.e. always
   visible. Pin the open/closed behaviour ourselves so the fallback path below
   (setAttribute('open')) works the same everywhere. */
dialog        { display: none; }
dialog[open]  { display: block; }
.relay-dialog       { border:1px solid var(--border); border-radius:8px; padding:1rem 1.25rem; max-width:600px; width:90%;
                      max-height:90vh; overflow:auto; overscroll-behavior:contain; }
@supports (max-height: 90dvh) { .relay-dialog { max-height: 90dvh; } }
/* No <dialog> support: no top layer either, so place and stack it by hand. */
.relay-dialog.fallback-open { position:fixed; top:5vh; left:50%; transform:translateX(-50%); z-index:1000;
                              background:var(--surface); box-shadow:0 8px 30px rgba(0,0,0,0.3); }
.relay-dialog::backdrop { background: rgba(0,0,0,0.35); }
.relay-fw-warn      { margin:0 0 0.75rem; padding:0.5rem 0.7rem; border:1px solid #e6c200;
                      background:#fff8e1; border-radius:6px; font-size:0.82rem; line-height:1.35; }
.relay-knobs        { margin-top:0.75rem; border:1px solid var(--border); border-radius:6px; padding:0.5rem 0.75rem 0.75rem; }
.relay-knobs legend { font-size:0.82rem; font-weight:600; padding:0 0.35rem; }
.relay-knobs label  { display:inline-flex; flex-direction:column; font-size:0.8rem; gap:0.2rem; margin-right:1rem; }
.relay-knobs input  { width:7rem; }
.relay-grid td      { padding:0.4rem 0.4rem; vertical-align:middle; }
.relay-grid .dow    { display:flex; gap:0.15rem; flex-wrap:wrap; }
.relay-grid .dow label { font-size:0.78rem; border:1px solid var(--border); border-radius:4px; padding:1px 5px; cursor:pointer; }
.relay-grid .dow input { display:none; }
.relay-grid .dow input:checked + span { background:var(--primary); color:#fff; padding:1px 5px; border-radius:3px; margin:-1px -5px; }
.relay-grid input[type=time] { width:6.5rem; }
.relay-grid .overnight { display:inline-flex; align-items:center; gap:0.3rem; font-size:0.76rem; color:var(--muted); white-space:nowrap; }
.relay-grid .overnight input { margin:0; }
.relay-grid .overnight.on { color:var(--primary); font-weight:600; }
.relay-grid .rm-cell button { line-height:1; }
/* Shown only when there are no windows, so a blank schedule reads as blank
   rather than as the pre-filled default it used to render. */
.relay-empty        { margin:0.25rem 0 0; padding:0.9rem 0.75rem; border:1px dashed var(--border);
                      border-radius:6px; text-align:center; font-size:0.85rem; color:var(--muted); }

@media (max-width: 640px) {
  /* Full-bleed sheet: a 600px dialog at 90% width is still wider than a phone,
     which pushed the +1 day and remove columns off-screen. */
  .relay-dialog     { width:100%; max-width:100%; height:100%; max-height:100%; margin:0;
                      border:0; border-radius:0; padding:0.85rem 0.85rem 0; }
  @supports (height: 100dvh) { .relay-dialog { height:100dvh; max-height:100dvh; } }
  .relay-dialog.fallback-open { top:0; left:0; transform:none; }
  .relay-dialog h3  { font-size:1.05rem; }

  /* The schedule table can't fit five columns on a phone, so each window
     becomes a labelled card instead of a row. */
  .relay-grid, .relay-grid tbody, .relay-grid tr, .relay-grid td { display:block; width:100%; }
  /* ...but the display:block above outranks the UA's [hidden] rule, so the
     empty state could never hide the table without this. */
  .relay-grid[hidden] { display:none; }
  .relay-grid thead { display:none; }
  .relay-grid tr    { border:1px solid var(--border); border-radius:6px; padding:0.6rem 0.65rem;
                      margin-bottom:0.6rem; position:relative; }
  .relay-grid td    { border-bottom:0; padding:0.35rem 0; }
  .relay-grid td[data-label]::before {
      content:attr(data-label); display:block; margin-bottom:0.25rem;
      font-size:0.66rem; text-transform:uppercase; letter-spacing:0.04em; color:var(--muted); }
  .relay-grid input[type=time] { width:100%; }
  /* Seven day chips across, each a real tap target. */
  .relay-grid .dow  { display:grid; grid-template-columns:repeat(7, 1fr); gap:0.25rem; }
  .relay-grid .dow label       { padding:0; overflow:hidden; font-size:0.72rem; }
  .relay-grid .dow label > span { display:block; padding:0.45rem 0; text-align:center; border-radius:3px; }
  .relay-grid .dow input:checked + span { margin:0; padding:0.45rem 0; }
  .relay-grid .rm-cell button  { width:100%; }
  .relay-grid .rm-cell button::after { content:' Remove window'; }

  .relay-knobs label { display:flex; margin-right:0; margin-bottom:0.5rem; }
  .relay-knobs input { width:100%; }

  /* Keep Cancel/Save reachable without scrolling to the bottom of the sheet. */
  .relay-actions    { position:sticky; bottom:0; background:var(--surface); padding:0.75rem 0;
                      margin-top:0.75rem; border-top:1px solid var(--border); }

  .dev-row.line1, .dev-row.line2 { gap:0.5rem 0.6rem; }
  .dev .f-id       { white-space:normal; overflow-wrap:anywhere; align-self:flex-start; }
  .dev .f-name, .dev .f-location, .dev .f-owner { flex:1 1 100%; }
  .dev .f-cap      { flex:0 0 7rem; }
  .dev-actions     { margin-left:0; width:100%; flex-wrap:wrap; }
  .dev-actions button, .dev-actions a { flex:1 1 auto; text-align:center; }
  .dev-pin         { width:100%; }
}
</style>

<script>
const CSRF = <?= json_encode(csrf_token()) ?>;
// Server timestamps are IST; anchor parsing to that offset for staleness math.
const APP_TZ_OFFSET = <?= json_encode(app_tz_offset()) ?>;

async function post(action, fields){
  const fd = new FormData();
  fd.append('action', action);
  fd.append('csrf', CSRF);
  for (const k in fields) fd.append(k, fields[k]);
  const res = await fetch('/api/admin_devices.php', { method: 'POST', body: fd, credentials: 'same-origin' });
  return res.json();
}

document.querySelectorAll('select.owner').forEach(sel => sel.addEventListener('change', async () => {
  const tr = sel.closest('.dev');
  const r  = await post('bind', { device_id: tr.dataset.id, user_id: sel.value || '' });
  if (!r.ok) alert('Error: ' + r.error);
}));

document.querySelectorAll('button.rename').forEach(btn => btn.addEventListener('click', async () => {
  const tr = btn.closest('.dev');
  const r  = await post('rename', {
    device_id:     tr.dataset.id,
    friendly_name: tr.querySelector('.name').value,
    location:      tr.querySelector('.location').value,
    capacity_kw:   tr.querySelector('.capacity').value,
  });
  alert(r.ok ? 'Saved.' : 'Error: ' + r.error);
}));

document.querySelectorAll('button.set-interval').forEach(btn => btn.addEventListener('click', async () => {
  const tr = btn.closest('.dev');
  const r  = await post('set_interval', {
    device_id: tr.dataset.id,
    log_interval_sec: tr.querySelector('.interval').value,
  });
  alert(r.ok ? 'Saved. Takes effect on the device\'s next sync.' : 'Error: ' + r.error);
}));

document.querySelectorAll('button.delete-device').forEach(btn => btn.addEventListener('click', async () => {
  const tr = btn.closest('.dev');
  if (!confirm('Delete this device and ALL its readings? This cannot be undone.')) return;
  const r = await post('delete', { device_id: tr.dataset.id });
  if (!r.ok) { alert('Error: ' + r.error); return; }
  tr.remove();
}));

// The PIN is the admin's to choose: `Set` takes a specific one, `↻` keeps
// rolling a random one. Both are exactly six digits — see api/admin_devices.php
// for why anything else would lock the owner out of their own meter.
const PIN_RE = /^[0-9]{6}$/;
document.querySelectorAll('button.set-pin').forEach(btn => btn.addEventListener('click', async () => {
  const tr      = btn.closest('.dev');
  const pinEl   = tr.querySelector('.pin');
  const current = pinEl.textContent.trim();
  const entered = prompt(
    'Set this device\'s BLE PIN — exactly 6 digits.\n\n' +
    'The old PIN stops working; the device owner must re-enter the new one in ' +
    'the app after their next login.',
    PIN_RE.test(current) ? current : ''
  );
  if (entered === null) return;                       // cancelled
  const pin = entered.trim();
  if (!PIN_RE.test(pin)) { alert('PIN must be exactly 6 digits.'); return; }
  if (pin === current) return;                        // unchanged — nothing to do
  const r = await post('set_pin', { device_id: tr.dataset.id, ble_pin: pin });
  if (!r.ok) { alert('Error: ' + r.error); return; }
  pinEl.textContent = r.ble_pin;
}));

document.querySelectorAll('button.regen-pin').forEach(btn => btn.addEventListener('click', async () => {
  const tr = btn.closest('.dev');
  if (!confirm('Generate a new BLE PIN? The old PIN stops working; the device owner must re-enter the new one in the app after their next login.')) return;
  const r = await post('regen_pin', { device_id: tr.dataset.id });
  if (!r.ok) { alert('Error: ' + r.error); return; }
  tr.querySelector('.pin').textContent = r.ble_pin;
}));

/* ---------- Relay schedule editor ---------- */
const DOW_NAMES = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
const dlg       = document.getElementById('relay-dialog');
const dlgDevEl  = document.getElementById('relay-dev');
const chSel     = document.getElementById('relay-ch');
const chWrap    = document.getElementById('relay-ch-wrap');
let currentCh   = 1;
const rowsEl    = document.getElementById('relay-rows');
let currentDev  = null;

// Older mobile browsers (and a few in-app webviews) ship <dialog> without
// showModal(). Fall back to a plain fixed-position panel so the editor still
// opens rather than doing nothing on tap.
function openDialog(d) {
  if (typeof d.showModal === 'function') { d.showModal(); return; }
  d.setAttribute('open', '');
  d.classList.add('fallback-open');
}
function closeDialog(d) {
  d.classList.remove('fallback-open');
  if (typeof d.close === 'function') d.close();
  else d.removeAttribute('open');
}

async function postRelay(action, fields) {
  const fd = new FormData();
  fd.append('action', action);
  fd.append('csrf', CSRF);
  for (const k in fields) fd.append(k, fields[k]);
  const res = await fetch('/api/admin_relay.php',
                          { method:'POST', body:fd, credentials:'same-origin' });
  return res.json();
}

function renderRow(win) {
  const tr = document.createElement('tr');
  const tdDays = document.createElement('td');
  tdDays.className = 'dow-cell';
  const dowWrap = document.createElement('div');
  dowWrap.className = 'dow';
  DOW_NAMES.forEach((name, i) => {
    const lbl = document.createElement('label');
    const cb = document.createElement('input');
    cb.type = 'checkbox'; cb.value = i;
    if (win.days && win.days.includes(i)) cb.checked = true;
    const sp = document.createElement('span'); sp.textContent = name;
    lbl.appendChild(cb); lbl.appendChild(sp);
    dowWrap.appendChild(lbl);
  });
  tdDays.appendChild(dowWrap);
  tr.appendChild(tdDays);

  // Each cell carries the column name it sits under. On a phone the table
  // collapses into one card per window and the header row is hidden, so the
  // label is re-drawn above the control from this attribute.
  tdDays.dataset.label = 'Days';

  const tdOn = document.createElement('td');
  tdOn.dataset.label = 'Open (on)';
  const onIn = document.createElement('input'); onIn.type='time'; onIn.value = win.on || '06:00';
  tdOn.appendChild(onIn); tr.appendChild(tdOn);

  const tdOff = document.createElement('td');
  tdOff.dataset.label = 'Close (off)';
  const offIn = document.createElement('input'); offIn.type='time'; offIn.value = win.off || '18:00';
  tdOff.appendChild(offIn); tr.appendChild(tdOff);

  // Overnight (+1 day) tick. Auto-driven: lights up when Off is earlier than
  // On, i.e. the window runs past midnight into the next day. Read-only — the
  // configuration is the On/Off times themselves.
  const tdNext = document.createElement('td');
  const ovWrap = document.createElement('label'); ovWrap.className = 'overnight';
  const ov = document.createElement('input');
  ov.type = 'checkbox'; ov.disabled = true; ov.tabIndex = -1;
  ov.title = 'Window runs into the next day (Off earlier than On)';
  const ovTxt = document.createElement('span'); ovTxt.textContent = 'next day';
  ovWrap.appendChild(ov); ovWrap.appendChild(ovTxt);
  tdNext.appendChild(ovWrap); tr.appendChild(tdNext);

  const toMin = v => { const m = /^(\d\d):(\d\d)$/.exec(v || ''); return m ? (+m[1] * 60 + +m[2]) : null; };
  const refreshOvernight = () => {
    const a = toMin(onIn.value), b = toMin(offIn.value);
    const overnight = (a !== null && b !== null && b < a);
    ov.checked = overnight;
    ovWrap.classList.toggle('on', overnight);
  };
  onIn.addEventListener('input', refreshOvernight);
  offIn.addEventListener('input', refreshOvernight);
  refreshOvernight();

  const tdRm = document.createElement('td');
  tdRm.className = 'rm-cell';
  const rm = document.createElement('button'); rm.type='button'; rm.className='danger'; rm.textContent='×';
  rm.title = 'Remove this window';
  rm.addEventListener('click', () => { tr.remove(); syncEmptyState(); });
  tdRm.appendChild(rm); tr.appendChild(tdRm);

  rowsEl.appendChild(tr);
  syncEmptyState();
}

// A device with no stored windows means "AC always allowed". The editor used to
// render the 09:00–02:00 default into an empty schedule, which read as a saved
// schedule even though the DB row was `[]` — so show the blank state instead,
// and only produce the default when the admin actually adds a window.
function syncEmptyState() {
  const empty = rowsEl.querySelectorAll('tr').length === 0;
  document.getElementById('relay-empty').hidden = !empty;
  document.querySelector('.relay-grid').hidden  = empty;
}

function collectWindows() {
  const out = [];
  rowsEl.querySelectorAll('tr').forEach(tr => {
    const days = [];
    // Scope to the day cell so the overnight indicator checkbox isn't counted.
    tr.querySelectorAll('.dow input[type=checkbox]').forEach(cb => { if (cb.checked) days.push(+cb.value); });
    const times = tr.querySelectorAll('input[type=time]');
    if (!days.length || !times[0].value || !times[1].value) return;
    out.push({ days, on: times[0].value, off: times[1].value });
  });
  return out;
}

// Load one relay's stored config into the form. Each relay of a multi-PZEM
// device has its own schedule row, so this refetches per channel.
async function loadRelayChannel(ch) {
  currentCh = ch;
  rowsEl.innerHTML = '';
  const r = await postRelay('get', { device_id: currentDev, channel: ch });
  if (!r.ok) { alert('Error: ' + r.error); return false; }
  const schedule = r.schedule || [];
  schedule.forEach(renderRow);
  syncEmptyState();
  document.getElementById('relay-cw').value = r.compressor_watts ?? 800;
  document.getElementById('relay-gm').value = r.grace_min ?? 60;
  return true;
}

document.querySelectorAll('button.relay').forEach(btn => btn.addEventListener('click', async () => {
  const tr = btn.closest('.dev');
  currentDev = tr.dataset.id;
  dlgDevEl.textContent = currentDev;
  // Warn if this device's firmware predates the AC-allowed-hours convention.
  document.getElementById('relay-fw-warn').hidden = tr.dataset.fwOld !== '1';

  // The device reports how many relays it has, so the picker matches the
  // hardware instead of being hardcoded. One relay -> no picker at all.
  const relayCount = Math.max(1, parseInt(tr.dataset.relayCount || '1', 10));
  chSel.innerHTML = '';
  for (let i = 1; i <= relayCount; i++) {
    const opt = document.createElement('option');
    opt.value = String(i);
    opt.textContent = i + ' (meter ' + i + ')';
    chSel.appendChild(opt);
  }
  chSel.value = '1';
  chWrap.hidden = relayCount < 2;

  if (!await loadRelayChannel(1)) return;
  openDialog(dlg);
}));

chSel.addEventListener('change', () => {
  loadRelayChannel(parseInt(chSel.value, 10) || 1);
});

document.getElementById('relay-add').addEventListener('click', () => {
  // First window on an empty schedule gets the every-day 09:00–02:00 default;
  // extra windows start with no days ticked so they can't silently duplicate it.
  const first = rowsEl.querySelectorAll('tr').length === 0;
  renderRow({ days: first ? [0,1,2,3,4,5,6] : [], on:'09:00', off:'02:00' });
});
document.getElementById('relay-cancel').addEventListener('click', () => closeDialog(dlg));
document.getElementById('relay-save'  ).addEventListener('click', async () => {
  const windows = collectWindows();
  const r = await postRelay('set', {
    device_id: currentDev,
    channel: currentCh,
    schedule_json: JSON.stringify(windows),
    compressor_watts: document.getElementById('relay-cw').value,
    grace_min: document.getElementById('relay-gm').value,
  });
  if (!r.ok) { alert('Error: ' + (r.detail || r.error)); return; }
  alert('Saved relay ' + currentCh + ' (version ' + r.version + ').' +
        ' Takes effect on the device\'s next sync.');
  closeDialog(dlg);
});

/* ---------- Live relay-state indicator ----------
   Each device reports its relay state (relay_on / relay_mode) on every ingest
   POST; the server stores it on ed_device_meta. We render the last-known state
   here and refresh it every 20 s so the dot tracks the device's sync cadence. */
function renderRelayCell(cell, st) {
  const dot = cell.querySelector('.relay-dot');
  const lbl = cell.querySelector('.relay-label');
  const on  = st && st.on;
  const at  = st && st.at;
  if (st == null || on == null || !at) {
    dot.className = 'relay-dot unknown';
    lbl.textContent = '—';
    cell.title = 'No state reported yet';
    return;
  }
  const ageSec = (Date.now() - new Date(at.replace(' ', 'T') + APP_TZ_OFFSET).getTime()) / 1000;
  const stale  = !isFinite(ageSec) || ageSec > Math.max(2.5 * (st.interval || 900), 900);
  // NC wiring: relay DE-energized = load powered ("Load On"); energized = AC cut.
  // `on` is the reported relay-energized flag, so the load is on when !on.
  const loadOn = !on;
  dot.className = 'relay-dot ' + (stale ? 'stale' : (loadOn ? 'on' : 'off'));
  let text = loadOn ? 'LOAD ON' : 'AC CUT';
  if (st.mode && st.mode !== 'auto') text += ' · forced ' + (st.mode === 'on' ? 'cut' : 'on');
  if (stale) text += ' · stale';
  lbl.textContent = text;
  cell.title = (loadOn ? 'Relay de-energized — load on (AC powered)' : 'Relay energized — AC cut')
             + ' · reported ' + at + ' IST';
}

// Initial paint from the server-rendered data-* attributes.
function stateFromCell(cell) {
  const onAttr = cell.dataset.on;
  return {
    on:       onAttr === '' ? null : onAttr === '1',
    mode:     cell.dataset.mode || null,
    at:       cell.dataset.at || null,
    interval: parseInt(cell.dataset.int || '900', 10),
  };
}
document.querySelectorAll('.col-relay').forEach(cell => renderRelayCell(cell, stateFromCell(cell)));

async function refreshRelayStates() {
  let j;
  try { j = await postRelay('states', {}); } catch (e) { return; }
  if (!j || !j.ok) return;
  const byId = {};
  for (const s of j.states) {
    byId[s.device_id] = { on: s.relay_on, mode: s.relay_mode, at: s.reported_at, interval: s.interval };
  }
  document.querySelectorAll('.dev[data-id]').forEach(dev => {
    const cell = dev.querySelector('.col-relay');
    if (cell) renderRelayCell(cell, byId[dev.dataset.id] ?? null);
  });
}
refreshRelayStates();
setInterval(refreshRelayStates, 20000);
</script>
</body></html>
