<?php
declare(strict_types=1);
require_once __DIR__ . '/../api/_db.php';
$user = require_login();
$pdo  = db();

// Devices the user can see
if (!empty($user['is_admin'])) {
    $dev_rows = $pdo->query(
        'SELECT d.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw,
                m.last_sync_at
           FROM ed_energy_devices d
           LEFT JOIN locations      l ON l.location_id = d.location
           LEFT JOIN ed_device_meta m ON m.device_id = d.device_id
          ORDER BY d.friendly_name'
    )->fetchAll();
} else {
    $st = $pdo->prepare(
        'SELECT d.device_id, d.friendly_name, d.location, l.location_name, d.capacity_kw,
                m.last_sync_at
           FROM ed_energy_devices d
           LEFT JOIN locations      l ON l.location_id = d.location
           LEFT JOIN ed_device_meta m ON m.device_id = d.device_id
          WHERE d.owner_user_id = ?
          ORDER BY d.friendly_name'
    );
    $st->execute([$user['id']]);
    $dev_rows = $st->fetchAll();
}
$selected = $_GET['device_id'] ?? ($dev_rows[0]['device_id'] ?? '');
$selected_meta = null;
foreach ($dev_rows as $d) {
    if ($d['device_id'] === $selected) { $selected_meta = $d; break; }
}

// How many PZEM channels the selected device reports. A multi-meter unit stores
// each meter's readings under the same device_id with a different `channel`, so
// the charts MUST be scoped to one of them — the two meters are separate
// cumulative counters and mixing them makes the kWh totals meaningless.
// Guarded so a DB without migration 010 still renders the dashboard.
$channel_count = 1;
if ($selected !== '') {
    try {
        $cs = $pdo->prepare('SELECT channel_count FROM ed_device_meta WHERE device_id = ?');
        $cs->execute([$selected]);
        $channel_count = max(1, (int)($cs->fetchColumn() ?: 1));
    } catch (Throwable $e) {
        $channel_count = 1;
    }
}
$channel = max(1, (int)($_GET['channel'] ?? 1));
if ($channel > $channel_count) $channel = 1;

// Last-reported relay state for the selected device (live indicator). Guarded
// so a DB without migration 003 (relay_* columns) still renders the dashboard.
$relay_meta = null;
if ($selected !== '') {
    try {
        $rs = $pdo->prepare(
            'SELECT relay_on, relay_mode, relay_reported_at, log_interval_sec
               FROM ed_device_meta WHERE device_id = ?'
        );
        $rs->execute([$selected]);
        $relay_meta = $rs->fetch() ?: null;
    } catch (Throwable $e) {
        $relay_meta = null;
    }
}
?>
<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AC Energy Meter — dashboard</title>
<link rel="stylesheet" href="/dashboard/assets/style.css?v=7">
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.6/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
<style>
  .relay-state { display: inline-flex; align-items: baseline; gap: 0.3rem; font-size: 0.8rem; color: var(--muted); }
  .relay-dot { display: inline-block; width: 0.6rem; height: 0.6rem; border-radius: 50%;
               background: #c8ccc4; align-self: center; }
  .relay-dot.on      { background: #1f9d3a; box-shadow: 0 0 0 3px rgba(31,157,58,0.18); }
  .relay-dot.off     { background: #98a09a; }
  .relay-dot.stale   { background: #d8a200; }
  .relay-dot.unknown { background: #c8ccc4; }
  .relay-state .relay-label { color: var(--text); }
</style>
</head><body>

<header class="topbar">
  <div class="brand">AC Energy Meter</div>
  <div class="user">
    Signed in as <b><?= h($user['username']) ?></b>
    &middot; <a href="/dashboard/report.php">reports</a>
    <?php if (!empty($user['is_admin'])): ?>
      &middot; <a href="/admin/">admin</a>
    <?php endif; ?>
    &middot; <a href="/api/logout.php">sign out</a>
  </div>
</header>

<?php if (!$dev_rows): ?>
<main class="container">
  <div class="card empty">
    <p>You don't have any devices bound to your account yet.</p>
    <?php if (!empty($user['is_admin'])): ?>
      <p>Go to <a href="/admin/">Admin</a> &rarr; Devices to bind one.</p>
    <?php else: ?>
      <p>Ask an administrator to bind your device to this account.</p>
    <?php endif; ?>
  </div>
</main>
<?php else: ?>
<main class="container">
  <form class="card controls" method="get">
    <label>Device
      <select name="device_id" onchange="this.form.submit()">
        <?php foreach ($dev_rows as $d): ?>
          <option value="<?= h($d['device_id']) ?>"
            <?= $d['device_id'] === $selected ? 'selected' : '' ?>>
            <?= h($d['friendly_name']) ?>
            <?php if (!empty($d['location_name'])) echo ' — ' . h($d['location_name']); ?>
          </option>
        <?php endforeach; ?>
      </select>
    </label>
    <?php if ($channel_count > 1): ?>
    <label>Meter
      <select name="channel" onchange="this.form.submit()">
        <?php for ($c = 1; $c <= $channel_count; $c++): ?>
          <option value="<?= $c ?>" <?= $c === $channel ? 'selected' : '' ?>>
            Meter <?= $c ?>
          </option>
        <?php endfor; ?>
      </select>
    </label>
    <?php endif; ?>
    <div class="range-buttons">
      <button type="button" data-range="today">Today</button>
      <button type="button" data-range="24h">24 h</button>
      <button type="button" data-range="7d">7 days</button>
      <button type="button" data-range="30d">30 days</button>
      <button type="button" data-range="12m">12 months</button>
    </div>
    <?php if ($selected_meta): ?>
      <span class="last-sync">
        Last sync:
        <?php if (!empty($selected_meta['last_sync_at'])): ?>
          <b><?= h($selected_meta['last_sync_at']) ?></b>
          <span class="rel" data-ts="<?= h($selected_meta['last_sync_at']) ?>"></span>
        <?php else: ?>
          <b>never</b>
        <?php endif; ?>
      </span>
      <span class="relay-state"
            data-on="<?= ($relay_meta && $relay_meta['relay_on'] !== null) ? (int)$relay_meta['relay_on'] : '' ?>"
            data-mode="<?= h((string)($relay_meta['relay_mode'] ?? '')) ?>"
            data-at="<?= h((string)($relay_meta['relay_reported_at'] ?? '')) ?>"
            data-int="<?= (int)($relay_meta['log_interval_sec'] ?? 900) ?>">
        <span class="relay-dot unknown"></span><span class="relay-label">—</span>
      </span>
    <?php endif; ?>
  </form>

  <section class="cards stats">
    <div class="stat"><span>Current</span>     <div class="stat-val"><b id="stat-now">—</b><i>W</i></div></div>
    <div class="stat"><span>Today</span>       <div class="stat-val"><b id="stat-today">—</b><i>kWh</i></div></div>
    <div class="stat"><span>Peak</span>        <div class="stat-val"><b id="stat-peak">—</b><i>W</i></div></div>
    <div class="stat"><span>Period total</span><div class="stat-val"><b id="stat-total">—</b><i>kWh</i></div></div>
  </section>

  <!-- Per-meter breakdown. Populated only in "All meters" mode, where the four
       cards above show the COMBINED figures across every meter. -->
  <section class="cards stats" id="per-meter" hidden></section>

  <section class="card">
    <h2 id="chart-title">Energy</h2>
    <canvas id="chart-energy" height="120"></canvas>
  </section>

  <section class="card">
    <h2>Power</h2>
    <canvas id="chart-power" height="120"></canvas>
  </section>
</main>

<script>
const DEVICE_ID = <?= json_encode($selected) ?>;
// Every readings query is scoped to one meter; see the channel note above.
const CHANNEL       = <?= json_encode($channel) ?>;   // 0 = all meters
const CHANNEL_COUNT = <?= json_encode($channel_count) ?>;
// Channels this view covers: every meter in "All meters" mode, else just one.
const CHANNELS = CHANNEL === 0
  ? Array.from({ length: CHANNEL_COUNT }, (_, i) => i + 1)
  : [CHANNEL];
const MULTI = CHANNELS.length > 1;
// One colour per meter, reused by both charts so a meter reads the same in each.
const CH_COLOURS = [
  { line: '#1f6e2a', fill: 'rgba(31,110,42,0.7)'  },
  { line: '#c97a1a', fill: 'rgba(201,122,26,0.7)' },
  { line: '#1a5fa8', fill: 'rgba(26,95,168,0.7)'  },
];
const chColour = ch => CH_COLOURS[(ch - 1) % CH_COLOURS.length];
// Server timestamps are in APP_TIMEZONE (IST). Anchor parsing to that offset
// so "X ago" is correct regardless of the viewer's browser time zone.
const APP_TZ_OFFSET = <?= json_encode(app_tz_offset()) ?>;

// aggregate    -> bucket size for the energy bars (kept coarse so max-min of
//                 the cumulative Wh counter spans several samples per bucket).
// powerAggregate-> bucket size for the Watt line. When set finer than
//                 `aggregate` the power chart is fetched separately, so short
//                 ranges show every ~5-minute posting instead of an hourly mean.
const RANGES = {
  today: {
    aggregate: 'hourly', powerAggregate: '5min', from: () => startOfToday(),
    label: "Today's energy (per hour)", energyLabel: 'kWh / hour',
    // Clamp the X axis to the daylight window so the shape of the day
    // is consistent and "nothing yet" is obvious.
    xMin: () => hourOfToday(7), xMax: () => hourOfToday(19),
    xUnit: 'hour',
  },
  '24h': { aggregate: 'hourly', powerAggregate: '5min', from: () => startOfToday(),
           label: '24 hours (12 AM – 12 AM)', energyLabel: 'kWh / hour',
           // Frame the whole calendar day midnight-to-midnight, not a rolling
           // "last 24 h from now", so the bars line up on hour boundaries.
           xMin: () => hourOfToday(0), xMax: () => hourOfToday(24), xUnit: 'hour' },
  '7d':  { aggregate: 'daily',  from: () => daysAgo(7),   label: 'Last 7 days',              energyLabel: 'kWh / day',  xUnit: 'day'   },
  '30d': { aggregate: 'daily',  from: () => daysAgo(30),  label: 'Last 30 days',             energyLabel: 'kWh / day',  xUnit: 'day'   },
  '12m': { aggregate: 'monthly',from: () => monthsAgo(12),label: 'Last 12 months',           energyLabel: 'kWh / month',xUnit: 'month' },
};

function startOfToday(){ const d=new Date(); d.setHours(0,0,0,0); return d; }
function hourOfToday(h){ const d=new Date(); d.setHours(h,0,0,0); return d; }
function hoursAgo(h){ return new Date(Date.now() - h*3600e3); }
function daysAgo(d){ return new Date(Date.now() - d*86400e3); }
function monthsAgo(m){ const d=new Date(); d.setMonth(d.getMonth()-m); return d; }
function isoLocal(d){
  const pad=n=>String(n).padStart(2,'0');
  return d.getFullYear()+'-'+pad(d.getMonth()+1)+'-'+pad(d.getDate())+'T'+
         pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
}

let energyChart, powerChart;

// Draws the cumulative meter reading (kWh) at the start and end of each bucket
// just under its bar: "start" on one line, "→ end" on the next. Only shown when
// there are few enough bars (<= 12) that the labels don't collide — so the
// 12-month and 7-day views get them, but 30-day / hourly stay uncluttered. The
// bar's own value equals end - start.
const endpointLabelsPlugin = {
  id: 'endpointLabels',
  afterDatasetsDraw(chart) {
    const ds = chart.data.datasets[0];
    const meta = chart.getDatasetMeta(0);
    if (!ds || !meta || !meta.data || meta.data.length === 0) return;
    if (meta.data.length > 12) return;              // too many bars — skip
    if (ds.data[0]?.s == null) return;              // not the energy (bar) chart
    const ctx = chart.ctx;
    const yTop = chart.scales.x.bottom + 2;         // just below the month labels
    ctx.save();
    ctx.font = '10px system-ui, -apple-system, sans-serif';
    ctx.fillStyle = '#6b7280';
    ctx.textAlign = 'center';
    const fmt = v => (v == null ? '' : Number(v).toLocaleString(undefined,
                       { minimumFractionDigits: 1, maximumFractionDigits: 1 }));
    meta.data.forEach((bar, i) => {
      const p = ds.data[i];
      if (!p || p.s == null || p.e == null) return;
      ctx.fillText(fmt(p.s), bar.x, yTop + 11);
      ctx.fillText('→ ' + fmt(p.e), bar.x, yTop + 23);
    });
    ctx.restore();
  },
};

function makeChart(canvasId, type, datasets, yLabel, xOpts, showEndpoints, showLegend){
  const ctx = document.getElementById(canvasId).getContext('2d');
  const x = {
    type: 'time',
    time: { tooltipFormat: 'PPp', unit: xOpts.unit || undefined },
  };
  if (xOpts.min) x.min = xOpts.min.getTime();
  if (xOpts.max) x.max = xOpts.max.getTime();
  return new Chart(ctx, {
    type, data: { datasets },
    plugins: showEndpoints ? [endpointLabelsPlugin] : [],
    options: {
      responsive: true, animation: false,
      parsing: { xAxisKey: 't', yAxisKey: 'y' },
      // Reserve room under the x-axis for the start/end reading labels.
      layout: showEndpoints ? { padding: { bottom: 28 } } : {},
      scales: {
        x,
        y: { beginAtZero: true, title: { display: true, text: yLabel } },
      },
      // A legend only earns its space when more than one meter is plotted;
      // with a single series the chart title already says what it is.
      plugins: { legend: { display: !!showLegend, position: 'bottom' } },
    },
  });
}

async function loadRange(rangeKey){
  const R = RANGES[rangeKey];
  document.getElementById('chart-title').textContent = R.label;
  const from = isoLocal(R.from());
  const readingsUrl = (agg, ch) =>
    `/api/readings.php?device_id=${encodeURIComponent(DEVICE_ID)}&channel=${ch}` +
    `&aggregate=${agg}&from=${encodeURIComponent(from)}`;

  // One request per meter. Each channel is a separate cumulative counter, so
  // they must be queried (and totalled) separately — the server cannot mix
  // them into one series without making the kWh figures meaningless.
  const per = await Promise.all(CHANNELS.map(async ch => {
    const j = await (await fetch(readingsUrl(R.aggregate, ch), { credentials: 'same-origin' })).json();
    if (!j.ok) return { ch, ok: false, energy: [], power: [], total: 0, baseline: 0 };

    const energy = j.points.map(p => ({ t: p.t, y: p.kwh, s: p.kwh_start, e: p.kwh_end }));

    // The Watt line can be finer-grained than the energy bars. When a distinct
    // powerAggregate is set, pull the power series from its own request so
    // short ranges plot every posted reading instead of an hourly average.
    let power = j.points.map(p => ({ t: p.t, y: p.P_avg }));
    if (R.powerAggregate && R.powerAggregate !== R.aggregate) {
      try {
        const pr = await (await fetch(readingsUrl(R.powerAggregate, ch), { credentials: 'same-origin' })).json();
        if (pr.ok) power = pr.points.map(p => ({ t: p.t, y: p.P_avg }));
      } catch (e) { /* keep the coarser power series on error */ }
    }

    const total = typeof j.total_kwh === 'number'
      ? j.total_kwh
      : energy.reduce((a, p) => a + (p.y || 0), 0);
    return { ch, ok: true, energy, power, total, baseline: Number(j.capacity_kw) || 0 };
  }));

  if (!per.some(r => r.ok)) { alert('Error loading readings'); return; }

  if (energyChart) energyChart.destroy();
  if (powerChart)  powerChart.destroy();
  const xOpts = {
    unit: R.xUnit,
    min:  R.xMin ? R.xMin() : null,
    max:  R.xMax ? R.xMax() : null,
  };
  // One dataset per meter, colour-matched across both charts. The start/end
  // endpoint labels are only drawn for a single series — with several meters
  // overlaid they would collide into unreadable clutter.
  energyChart = makeChart('chart-energy', 'bar', per.map(r => ({
    label: MULTI ? `Meter ${r.ch}` : R.energyLabel,
    data: r.energy,
    backgroundColor: MULTI ? chColour(r.ch).fill : 'rgba(31,110,42,0.7)',
  })), R.energyLabel, xOpts, !MULTI, MULTI);
  powerChart = makeChart('chart-power', 'line', per.map(r => ({
    label: MULTI ? `Meter ${r.ch}` : 'Avg power (W)',
    data: r.power,
    borderColor: MULTI ? chColour(r.ch).line : '#c97a1a',
    tension: 0.25,
  })), 'W', xOpts, false, MULTI);

  // Stats. Period total is each range's single start->end meter difference
  // (server total_kwh, one MAX-MIN over the whole window) rather than a sum of
  // the bars, which would drop the energy accrued in the gaps between buckets.
  // capacity_kw is repurposed as the old meter's last reading (kWh) at install,
  // so it is added on to continue from the meter this device replaced.
  //
  // Across meters these are SUMMED for the combined cards: total energy is
  // additive, and peak is the highest instantaneous draw seen on any meter.
  // The install baseline (capacity_kw, repurposed as the replaced meter's last
  // reading) belongs to the DEVICE, so it is added ONCE — summing it per channel
  // would multiply it by the meter count.
  const baseline = per.reduce((b, r) => b || r.baseline, 0);
  const periodTotal = per.reduce((a, r) => a + r.total, 0) + baseline;
  const peakP = per.reduce((m, r) =>
    Math.max(m, r.power.reduce((n, p) => Math.max(n, p.y || 0), 0)), 0);
  document.getElementById('stat-total').textContent = periodTotal.toFixed(2);
  document.getElementById('stat-peak').textContent  = peakP.toFixed(0);

  // Per-meter breakdown row (All-meters mode only).
  const pmEl = document.getElementById('per-meter');
  pmEl.hidden = !MULTI;
  if (MULTI) {
    pmEl.innerHTML = '';
    per.forEach(r => {
      const peak = r.power.reduce((n, p) => Math.max(n, p.y || 0), 0);
      const d = document.createElement('div');
      d.className = 'stat';
      d.dataset.ch = String(r.ch);   // loadLive() fills in the live watts below
      d.innerHTML =
        `<span style="color:${chColour(r.ch).line}">\u25CF Meter ${r.ch}</span>` +
        `<div class="stat-val"><b>${r.total.toFixed(2)}</b><i>kWh</i></div>` +
        `<div class="muted" style="font-size:0.8rem">` +
        `<span class="live-w">— W now</span> · peak ${peak.toFixed(0)} W</div>`;
      pmEl.appendChild(d);
    });
  }

  // "Today" + "Current" come from a raw query of the last hour
  loadLive();
}

async function loadLive(){
  const from = isoLocal(hoursAgo(1));
  // "Current" is the sum of each meter's latest reading — the site's live draw.
  // Queried per channel because each meter is its own counter.
  let now = null;
  const nowPer = new Map();
  try {
    const results = await Promise.all(CHANNELS.map(async ch => {
      const url = `/api/readings.php?device_id=${encodeURIComponent(DEVICE_ID)}&channel=${ch}` +
                  `&aggregate=raw&from=${encodeURIComponent(from)}`;
      const j = await (await fetch(url, { credentials: 'same-origin' })).json();
      if (!j.ok || !j.points.length) return null;
      const p = j.points[j.points.length-1];
      return typeof p.P === 'number' ? { ch, P: p.P } : null;
    }));
    results.filter(Boolean).forEach(r => {
      now = (now || 0) + r.P;
      nowPer.set(r.ch, r.P);
    });
  } catch (e) { /* network/parse error — fall through to dash */ }
  document.getElementById('stat-now').textContent =
    now === null ? '—' : now.toFixed(0);

  // Annotate the per-meter cards with each meter's own live draw.
  if (MULTI) {
    document.querySelectorAll('#per-meter .stat[data-ch]').forEach(el => {
      const ch = Number(el.dataset.ch);
      const w  = nowPer.has(ch) ? nowPer.get(ch).toFixed(0) + ' W now' : '— W now';
      const liveEl = el.querySelector('.live-w');
      if (liveEl) liveEl.textContent = w;
    });
  }

  // Today kWh as the sum of today's per-hour deltas (each hourly bucket's own
  // max-min), so it matches the hourly bars on the chart. The Period total card
  // carries the whole-day start->end figure instead. Add the old-meter baseline
  // (capacity_kw, repurposed as the replaced meter's last reading in kWh) so the
  // figure continues from that meter.
  let today_kwh = null;
  let baseline = 0;
  try {
    const today = isoLocal(startOfToday());
    const rows = await Promise.all(CHANNELS.map(async ch => {
      const url2 = `/api/readings.php?device_id=${encodeURIComponent(DEVICE_ID)}&channel=${ch}` +
                   `&aggregate=hourly&from=${encodeURIComponent(today)}`;
      return (await fetch(url2, { credentials: 'same-origin' })).json();
    }));
    // The install baseline is a property of the DEVICE, not of each meter, so
    // it is taken once rather than added per channel (which would multiply it).
    const withCap = rows.find(r => r && Number(r.capacity_kw));
    baseline = withCap ? Number(withCap.capacity_kw) : 0;
    rows.filter(r => r && r.ok).forEach(r => {
      today_kwh = (today_kwh || 0) + r.points.reduce((a, p) => a + (p.kwh || 0), 0);
    });
  } catch (e) { /* fall through */ }
  document.getElementById('stat-today').textContent =
    today_kwh === null ? '—' : (today_kwh + baseline).toFixed(2);
}

document.querySelectorAll('.range-buttons button').forEach(b => {
  b.addEventListener('click', () => {
    document.querySelectorAll('.range-buttons button').forEach(x => x.classList.remove('on'));
    b.classList.add('on');
    loadRange(b.dataset.range);
  });
});
// initial load: today
document.querySelector('.range-buttons button[data-range="today"]').click();

// "Last sync" relative time. Server timestamp is in APP_TIMEZONE (IST);
// append that offset so the instant is correct in any viewer's browser.
(function annotateLastSync(){
  const el = document.querySelector('.last-sync .rel');
  if (!el) return;
  const ts = el.dataset.ts;
  if (!ts) return;
  const d = new Date(ts.replace(' ', 'T') + APP_TZ_OFFSET);
  if (isNaN(d.getTime())) return;
  const tick = () => {
    const secs = Math.max(0, Math.round((Date.now() - d.getTime()) / 1000));
    let s;
    if      (secs < 60)        s = `${secs}s ago`;
    else if (secs < 3600)      s = `${Math.round(secs/60)} min ago`;
    else if (secs < 86400)     s = `${Math.round(secs/3600)} h ago`;
    else                       s = `${Math.round(secs/86400)} d ago`;
    el.textContent = ` (${s})`;
  };
  tick();
  setInterval(tick, 30_000);
})();

// Live relay indicator. The device reports its relay state on every ingest
// POST; we render the last-known state and refresh on the sync cadence.
(function relayIndicator(){
  const el = document.querySelector('.relay-state');
  if (!el) return;
  const dot = el.querySelector('.relay-dot');
  const lbl = el.querySelector('.relay-label');

  function render(st){
    const on = st && st.on, at = st && st.at;
    if (st == null || on == null || !at){
      dot.className = 'relay-dot unknown'; lbl.textContent = '—';
      el.title = 'No state reported yet'; return;
    }
    const ageSec = (Date.now() - new Date(at.replace(' ', 'T') + APP_TZ_OFFSET).getTime()) / 1000;
    const stale  = !isFinite(ageSec) || ageSec > Math.max(2.5 * (st.interval || 900), 900);
    // NC wiring: relay de-energized = load powered ("Load On"); energized = AC cut.
    const loadOn = !on;
    dot.className = 'relay-dot ' + (stale ? 'stale' : (loadOn ? 'on' : 'off'));
    let text = loadOn ? 'LOAD ON' : 'AC CUT';
    if (st.mode && st.mode !== 'auto') text += ' · forced ' + (st.mode === 'on' ? 'cut' : 'on');
    if (stale) text += ' · stale';
    lbl.textContent = text;
    el.title = (loadOn ? 'Relay de-energized — load on (AC powered)' : 'Relay energized — AC cut')
             + ' · reported ' + at + ' IST';
  }

  // Initial paint from the server-rendered data-* attributes.
  render({
    on:       el.dataset.on === '' ? null : el.dataset.on === '1',
    mode:     el.dataset.mode || null,
    at:       el.dataset.at || null,
    interval: parseInt(el.dataset.int || '900', 10),
  });

  async function refresh(){
    try {
      const r = await (await fetch(
        `/api/relay_state.php?device_id=${encodeURIComponent(DEVICE_ID)}`,
        { credentials: 'same-origin' })).json();
      if (r && r.ok) render({ on: r.on, mode: r.mode, at: r.reported_at, interval: r.interval });
    } catch (e) { /* keep last paint */ }
  }
  refresh();
  setInterval(refresh, 20_000);
})();
</script>
<?php endif; ?>

</body></html>
