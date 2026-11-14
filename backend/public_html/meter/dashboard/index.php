<?php
declare(strict_types=1);
require_once __DIR__ . '/../api/_db.php';
require_once __DIR__ . '/../api/_ui.php';
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
// Only a device from the visibility list above may be selected. The readings
// API re-checks access, but this page also reads device meta (relay state,
// channel count) for $selected directly, so an arbitrary ?device_id= would
// expose another account's device.
if (!in_array($selected, array_column($dev_rows, 'device_id'), true)) {
    $selected = $dev_rows[0]['device_id'] ?? '';
}
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
<meta name="theme-color" content="#f6fbf2" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#101510" media="(prefers-color-scheme: dark)">
<link rel="stylesheet" href="/dashboard/assets/style.css?v=9">
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.6/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3.0.0/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
<script src="/dashboard/assets/theme.js?v=1"></script>
</head><body>

<header class="topbar">
  <div class="brand"><span class="logo"><?= ui_icon('bolt') ?></span>AC Energy Meter</div>
  <div class="user">
    <span class="who">Signed in as <b><?= h($user['username']) ?></b></span>
    <a href="/dashboard/report.php<?= $selected ? '?device_id=' . urlencode($selected) : '' ?>"><?= ui_icon('chart') ?>Reports</a>
    <?php if (!empty($user['is_admin'])): ?>
      <a href="/admin/"><?= ui_icon('shield') ?>Admin</a>
    <?php endif; ?>
    <a href="/api/logout.php"><?= ui_icon('logout') ?>Sign out</a>
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
    <!-- Fixed ranges as one segmented control; Custom as a chip beside it that
         only opens the date filter (Apply is what loads it). -->
    <div class="range-row">
      <div class="range-buttons" role="group" aria-label="Range">
        <button type="button" data-range="today">Today</button>
        <button type="button" data-range="24h">24 h</button>
        <button type="button" data-range="7d"><span class="lbl-full">7 days</span><span class="lbl-short">7 d</span></button>
        <button type="button" data-range="30d"><span class="lbl-full">30 days</span><span class="lbl-short">30 d</span></button>
        <button type="button" data-range="12m"><span class="lbl-full">12 months</span><span class="lbl-short">12 mo</span></button>
      </div>
      <button type="button" class="chip" id="btn-custom" aria-expanded="false"
              aria-controls="custom-range"><?= ui_icon('calendar') ?><span id="custom-label">Custom range</span></button>
    </div>
    <!-- Custom date range. Both ends are whole days, inclusive. -->
    <div class="custom-range" id="custom-range" hidden>
      <label>From <input type="date" id="custom-from"></label>
      <label>To <input type="date" id="custom-to"></label>
      <button type="button" id="custom-apply">Apply</button>
    </div>
    <?php if ($selected_meta): ?>
      <span class="last-sync">
        <?= ui_icon('clock') ?> Last sync:
        <?php if (!empty($selected_meta['last_sync_at'])): ?>
          <b><?= h($selected_meta['last_sync_at']) ?></b>
          <span class="rel" data-ts="<?= h($selected_meta['last_sync_at']) ?>"></span>
        <?php else: ?>
          <b>never</b>
        <?php endif; ?>
      </span>
      <!-- One pill per relay; extra pills are cloned from this one by JS. -->
      <div class="relay-row">
      <span class="relay-title"><?= ui_icon('relay') ?> Relay:</span>
      <span class="relay-state"
            data-on="<?= ($relay_meta && $relay_meta['relay_on'] !== null) ? (int)$relay_meta['relay_on'] : '' ?>"
            data-mode="<?= h((string)($relay_meta['relay_mode'] ?? '')) ?>"
            data-at="<?= h((string)($relay_meta['relay_reported_at'] ?? '')) ?>"
            data-int="<?= (int)($relay_meta['log_interval_sec'] ?? 900) ?>">
        <span class="relay-dot unknown"></span><span class="relay-label">—</span>
      </span>
      </div>
    <?php endif; ?>
  </form>

  <section class="cards stats">
    <div class="stat tone-primary">
      <span class="badge"><?= ui_icon('bolt') ?></span>
      <span>Current</span><div class="stat-val"><b id="stat-now">—</b><i>W</i></div>
    </div>
    <div class="stat tone-tertiary">
      <span class="badge"><?= ui_icon('trend') ?></span>
      <span>Peak</span><div class="stat-val"><b id="stat-peak">—</b><i>W</i></div>
    </div>
    <div class="stat tone-secondary">
      <span class="badge"><?= ui_icon('chart') ?></span>
      <span>Period total</span><div class="stat-val"><b id="stat-total">—</b><i>kWh</i></div>
    </div>
    <div class="stat" id="stat-meter-card" title="Latest cumulative meter reading">
      <span class="badge"><?= ui_icon('gauge') ?></span>
      <span>Meter reading</span><div class="stat-val"><b id="stat-meter">—</b><i>kWh</i></div>
    </div>
  </section>

  <!-- Per-meter breakdown. Populated only in "All meters" mode, where the four
       top cards show the COMBINED figures across every meter. -->
  <section class="cards stats" id="per-meter" hidden></section>

  <!-- Energy, then power, then the readings behind the energy bars — the same
       order as the Solar Monitor dashboard. -->
  <section class="card">
    <h2 class="sec"><span class="badge tint-primary"><?= ui_icon('bolt') ?></span>
      <span class="t" id="chart-title">Energy</span><span class="cap" id="chart-cap"></span></h2>
    <div class="chart-box sm"><canvas id="chart-energy"></canvas></div>
    <!-- Meter reading across the charted period. The per-bar readings drawn
         under the bars vanish once the bars get narrow (30 days, or any phone),
         so this line is what always carries the figure. -->
    <p class="meter-span" id="meter-reading"></p>
  </section>

  <section class="card">
    <h2 class="sec"><span class="badge tint-tertiary"><?= ui_icon('trend') ?></span>
      <span class="t">Power</span><span class="cap">W</span></h2>
    <div class="chart-box sm"><canvas id="chart-power"></canvas></div>
  </section>

  <section class="card" id="readings-card">
    <h2 class="sec"><span class="badge tint-secondary"><?= ui_icon('list') ?></span>
      <span class="t">Meter readings per bar</span></h2>
    <p class="muted">The cumulative meter reading at the start and end of each
       energy bar &mdash; continued from the old meter's baseline, so it reads
       like the physical meter. Generated = end &minus; start, and the newest end
       reading is the Meter reading card above. All values in kWh.</p>
    <table class="grid readings">
      <thead><tr id="readings-head"></tr></thead>
      <tbody id="readings-body"></tbody>
      <tfoot>
        <tr id="readings-foot"><th>Total</th><th id="readings-total">&mdash;</th><th></th><th></th></tr>
      </tfoot>
    </table>
    <p class="muted" id="readings-empty" style="display:none">No readings in this range.</p>
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
// Read from the theme tokens on every call, so a light/dark switch recolours.
const CH_TOKENS = ['--primary', '--tertiary', '--ch3'];
const chColour = ch => {
  const line = MeterTheme.cssVar(CH_TOKENS[(ch - 1) % CH_TOKENS.length]);
  return { line, fill: MeterTheme.withAlpha(line, 0.8) };
};
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
    // Midnight through the end of the hour in progress — the day so far.
    // This used to clamp to 07:00-19:00 on the assumption that a site only
    // draws power during business hours, which hid every overnight reading on
    // a meter that runs around the clock. The right edge tracks the current
    // hour rather than sitting at midnight so the elapsed part of the day
    // fills the plot instead of being squeezed against a mostly empty axis.
    xMin: () => hourOfToday(0),
    xMax: () => hourOfToday(new Date().getHours() + 1),
    xUnit: 'hour',
  },
  // Rolling window: the last 24 whole hours up to and including the hour in
  // progress, so it reaches back across midnight into yesterday. `today` is
  // the calendar day; this is what it has been doing for the past day.
  // Starts on an hour boundary so the first bar is a full hour, not a sliver.
  '24h': { aggregate: 'hourly', powerAggregate: '5min', from: () => hourOfToday(new Date().getHours() - 23),
           label: 'Last 24 hours', energyLabel: 'kWh / hour',
           xMin: () => hourOfToday(new Date().getHours() - 23),
           xMax: () => hourOfToday(new Date().getHours() + 1), xUnit: 'hour' },
  '7d':  { aggregate: 'daily',  from: () => daysAgo(7),   label: 'Last 7 days',              energyLabel: 'kWh / day',  xUnit: 'day'   },
  '30d': { aggregate: 'daily',  from: () => daysAgo(30),  label: 'Last 30 days',             energyLabel: 'kWh / day',  xUnit: 'day'   },
  '12m': { aggregate: 'monthly',from: () => monthsAgo(12),label: 'Last 12 months',           energyLabel: 'kWh / month',xUnit: 'month' },
};

// Build RANGES.custom from two YYYY-MM-DD strings (both days inclusive). The
// bucket size follows the span so the bar count stays readable: hourly for up
// to 2 days, daily up to ~3 months, monthly beyond that.
function buildCustomRange(fromStr, toStr){
  const [fy, fm, fd] = fromStr.split('-').map(Number);
  const [ty, tm, td] = toStr.split('-').map(Number);
  const from = new Date(fy, fm - 1, fd, 0, 0, 0);
  const to   = new Date(ty, tm - 1, td, 23, 59, 59);
  const endX = new Date(ty, tm - 1, td + 1, 0, 0, 0);
  const days = Math.round((endX - from) / 86400e3);
  const fmt  = d => d.toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
  const label = fromStr === toStr ? fmt(from) : `${fmt(from)} – ${fmt(to)}`;
  const base = { from: () => from, to: () => to, label };
  if (days <= 2)  return { ...base, aggregate: 'hourly', powerAggregate: '5min',
                           energyLabel: 'kWh / hour', xUnit: 'hour',
                           xMin: () => from, xMax: () => endX };
  if (days <= 92) return { ...base, aggregate: 'daily',   energyLabel: 'kWh / day',   xUnit: 'day' };
  return            { ...base, aggregate: 'monthly', energyLabel: 'kWh / month', xUnit: 'month' };
}

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

// Matches the 640px breakpoint the stylesheet reflows at.
const narrowScreen = () => window.innerWidth < 640;

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
    ctx.font = '10px ' + getComputedStyle(document.body).fontFamily;
    ctx.fillStyle = MeterTheme.cssVar('--on-surface-variant');
    ctx.textAlign = 'center';
    const fmt = v => (v == null ? '' : Number(v).toLocaleString(undefined,
                       { minimumFractionDigits: 1, maximumFractionDigits: 1 }));

    const labels = meta.data.map((bar, i) => {
      const p = ds.data[i];
      if (!p || p.s == null || p.e == null) return null;
      return { bar, top: fmt(p.s), bot: '→ ' + fmt(p.e) };
    });

    // Measure the labels rather than assuming a width. These readings continue
    // from the replaced meter, so they run to five or six figures and are much
    // wider than the bare deltas this used to draw — a fixed threshold tuned
    // for "4.2" lets "8,694.5" overlap its neighbour. Drop the whole row when
    // the widest label wouldn't clear the next bar (which is what keeps it off
    // a phone-width chart too).
    const widthOf = l => Math.max(ctx.measureText(l.top).width, ctx.measureText(l.bot).width);
    const widest = labels.reduce((m, l) => l ? Math.max(m, widthOf(l)) : m, 0);
    const plotWidth = chart.chartArea?.width ?? chart.width;
    if (plotWidth / meta.data.length < widest + 8) { ctx.restore(); return; }

    labels.forEach(l => {
      if (!l) return;
      // Centre-aligned text on the first/last bar hangs off the canvas edge and
      // gets clipped; nudge those back inside.
      const half = widthOf(l) / 2;
      const x = Math.min(Math.max(l.bar.x, half + 1), chart.width - half - 1);
      ctx.fillText(l.top, x, yTop + 11);
      ctx.fillText(l.bot, x, yTop + 23);
    });
    ctx.restore();
  },
};

function makeChart(canvasId, type, datasets, yLabel, xOpts, showEndpoints, showLegend, tooltipCallbacks){
  MeterTheme.applyChartDefaults();
  const ctx = document.getElementById(canvasId).getContext('2d');
  const x = {
    type: 'time',
    time: { tooltipFormat: 'PPp', unit: xOpts.unit || undefined },
    grid: { display: false },
    border: { display: false },
    // A phone fits roughly half the tick labels a laptop does; let Chart.js
    // drop the rest instead of overprinting them on top of each other.
    ticks: { autoSkip: true, autoSkipPadding: 12, maxRotation: 0,
             maxTicksLimit: narrowScreen() ? 6 : 12 },
  };
  if (xOpts.min) x.min = xOpts.min.getTime();
  if (xOpts.max) x.max = xOpts.max.getTime();
  return new Chart(ctx, {
    type, data: { datasets },
    plugins: showEndpoints ? [endpointLabelsPlugin] : [],
    options: {
      // The .chart-box parent owns the height (see style.css); keeping the
      // canvas's attribute ratio instead collapses the chart to a ~120px strip
      // on a phone.
      responsive: true, maintainAspectRatio: false, animation: false,
      parsing: { xAxisKey: 't', yAxisKey: 'y' },
      // Reserve room under the x-axis for the start/end reading labels.
      layout: showEndpoints ? { padding: { bottom: 28 } } : {},
      scales: {
        x,
        y: { beginAtZero: true, border: { display: false },
             title: { display: true, text: yLabel } },
      },
      plugins: {
        // A legend only earns its space when more than one meter is plotted;
        // with a single series the chart title already says what it is.
        legend: { display: !!showLegend, position: 'bottom',
                  labels: { boxWidth: 10, boxHeight: 10, usePointStyle: true, pointStyle: 'circle' } },
        tooltip: tooltipCallbacks ? { callbacks: tooltipCallbacks } : {},
      },
    },
  });
}

/* ---------- Meter readings per bar ---------- */

const RD_MON = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];

// Label a bucket from the ISO string's own fields rather than via new Date().
// The server already emits the bucket in APP_TIMEZONE, so re-parsing it into
// the viewer's zone would shift a midnight bucket back a day for anyone outside
// IST and label every row with the wrong date.
function bucketLabel(iso, unit){
  if (typeof iso !== 'string' || iso.length < 10) return String(iso);
  const y = iso.slice(0, 4), m = +iso.slice(5, 7), d = +iso.slice(8, 10);
  if (unit === 'month') return `${RD_MON[m - 1]} ${y}`;
  if (unit === 'hour') {
    const h = +iso.slice(11, 13);
    return `${RD_MON[m - 1]} ${d}, ${(h % 12) || 12} ${h < 12 ? 'AM' : 'PM'}`;
  }
  return `${RD_MON[m - 1]} ${d}`;
}

// Drop leading/trailing buckets where the meter never moved — on Today those
// are the night hours, which draw no bar and would pad the table with zeros.
// Anything in between is kept, so the rows still sum to the range's total.
function trimIdleEdges(points){
  let a = 0, b = points.length - 1;
  while (a <= b && !(points[a].y > 0)) a++;
  while (b >= a && !(points[b].y > 0)) b--;
  return a > b ? points : points.slice(a, b + 1);
}

// One row per bar: the two meter readings it spans and their difference.
// s/e already carry the old-meter offset the charts use, and e - s is exactly
// the bar's kWh, so this table is the arithmetic behind the chart rather than
// a second estimate. With several meters a leading Meter column is added —
// each channel is its own cumulative counter and they cannot share a row.
function renderReadings(per, R){
  const headEl  = document.getElementById('readings-head');
  const bodyEl  = document.getElementById('readings-body');
  const footEl  = document.getElementById('readings-foot');
  const totalEl = document.getElementById('readings-total');
  const emptyEl = document.getElementById('readings-empty');
  const table   = document.querySelector('table.readings');
  const unitHead = R.xUnit === 'month' ? 'Month' : (R.xUnit === 'hour' ? 'Hour' : 'Day');

  // Phones get the short labels so every column fits without scrolling.
  const lbl = (full, short) => `<span class="lbl-full">${full}</span><span class="lbl-short">${short}</span>`;
  headEl.innerHTML = (MULTI ? '<th>Meter</th>' : '') +
    `<th>${unitHead}</th>` +
    `<th>${lbl('Generated (kWh)', 'Generated')}</th>` +
    `<th>${lbl('Start reading (kWh)', 'Start')}</th>` +
    `<th>${lbl('End reading (kWh)', 'End')}</th>`;
  // Total sits under Generated; keep the blank cells aligned with the header.
  footEl.innerHTML = (MULTI ? '<th></th>' : '') + '<th>Total</th>' +
    '<th id="readings-total">\u2014</th><th></th><th></th>';

  const rows = [];
  per.filter(r => r.ok).forEach(r => {
    trimIdleEdges(r.energy).forEach(pt => {
      if (pt.s == null || pt.e == null) return;
      rows.push({ ch: r.ch, t: pt.t, y: pt.y || 0, s: pt.s, e: pt.e });
    });
  });
  // Newest first — over 30 days the bar you care about is the one on top.
  rows.sort((a, b) => (a.t < b.t ? 1 : a.t > b.t ? -1 : a.ch - b.ch));

  bodyEl.innerHTML = '';
  if (!rows.length) {
    table.style.display = 'none';
    emptyEl.style.display = '';
    return;
  }
  table.style.display = '';
  emptyEl.style.display = 'none';

  let sum = 0;
  rows.forEach(r => {
    sum += r.y;
    const tr = document.createElement('tr');
    tr.innerHTML =
      (MULTI ? `<td>Meter ${r.ch}</td>` : '') +
      `<td>${bucketLabel(r.t, R.xUnit)}</td>` +
      `<td class="mono gen">${r.y.toFixed(3)}</td>` +
      `<td class="mono">${r.s.toFixed(3)}</td>` +
      `<td class="mono">${r.e.toFixed(3)}</td>`;
    bodyEl.appendChild(tr);
  });
  document.getElementById('readings-total').textContent = sum.toFixed(3);
}

// Grouped thousands — readings continued from the old meter run to five digits,
// and 8,984.85 is a lot easier to read than 8984.85.
const fmtKwhGrouped = v => Number(v).toLocaleString(undefined,
                             { minimumFractionDigits: 2, maximumFractionDigits: 2 });

async function loadRange(rangeKey){
  const R = RANGES[rangeKey];
  document.getElementById('chart-title').textContent = R.label;
  document.getElementById('chart-cap').textContent = R.energyLabel;
  document.getElementById('custom-label').textContent =
    rangeKey === 'custom' ? R.label : 'Custom range';
  const from = isoLocal(R.from());
  // Preset ranges run up to now (the server's default `to`); a custom range
  // passes its own end.
  const toParam = R.to ? `&to=${encodeURIComponent(isoLocal(R.to()))}` : '';
  const readingsUrl = (agg, ch) =>
    `/api/readings.php?device_id=${encodeURIComponent(DEVICE_ID)}&channel=${ch}` +
    `&aggregate=${agg}&from=${encodeURIComponent(from)}${toParam}`;

  // One request per meter. Each channel is a separate cumulative counter, so
  // they must be queried (and totalled) separately — the server cannot mix
  // them into one series without making the kWh figures meaningless.
  const per = await Promise.all(CHANNELS.map(async ch => {
    const j = await (await fetch(readingsUrl(R.aggregate, ch), { credentials: 'same-origin' })).json();
    if (!j.ok) return { ch, ok: false, energy: [], power: [], total: 0, baseline: 0, latest: null, peak: 0 };

    // Readings that continue from the meter this device replaced.
    //
    // capacity_kw is repurposed as that meter's final reading at install, but
    // adding it straight onto the raw PZEM counter overshoots: the counter is
    // rarely at zero when a unit goes into service (bench testing leaves a few
    // hundred Wh on it), so a device entered as 1348.10 charted from 1348.52.
    // Anchor on origin_kwh — this channel's first-ever reading — so the point
    // where this device took over IS the old meter's final reading.
    //
    // Only s/e are offset. `y` is a within-bucket delta, and shifting both ends
    // of a subtraction by a constant cancels out.
    const base   = Number(j.capacity_kw) || 0;
    const origin = Number(j.origin_kwh)  || 0;
    const offset = base - origin;
    const energy = j.points.map(p => ({
      t: p.t,
      y: p.kwh,
      s: p.kwh_start == null ? null : p.kwh_start + offset,
      e: p.kwh_end   == null ? null : p.kwh_end   + offset,
    }));

    // The Watt line can be finer-grained than the energy bars. When a distinct
    // powerAggregate is set, pull the power series from its own request so
    // short ranges plot every posted reading instead of an hourly average.
    let power = j.points.map(p => ({ t: p.t, y: p.P_avg }));
    // True peak: each bucket's MAX(power_w). The plotted line is the bucket
    // AVERAGE, so its maximum understated the peak on the daily/monthly views.
    let peak = j.points.reduce((m, p) => Math.max(m, p.P_peak || 0), 0);
    if (R.powerAggregate && R.powerAggregate !== R.aggregate) {
      try {
        const pr = await (await fetch(readingsUrl(R.powerAggregate, ch), { credentials: 'same-origin' })).json();
        if (pr.ok) {
          power = pr.points.map(p => ({ t: p.t, y: p.P_avg }));
          peak  = pr.points.reduce((m, p) => Math.max(m, p.P_peak || 0), peak);
        }
      } catch (e) { /* keep the coarser power series on error */ }
    }

    const total = typeof j.total_kwh === 'number'
      ? j.total_kwh
      : energy.reduce((a, p) => a + (p.y || 0), 0);
    // Where this meter's counter stands now, continuing from the old meter.
    const latest = typeof j.latest_kwh === 'number' ? j.latest_kwh + offset : null;
    return { ch, ok: true, energy, power, total, baseline: base, latest, peak };
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
    backgroundColor: chColour(r.ch).fill,
    hoverBackgroundColor: chColour(r.ch).line,
    borderRadius: 6, borderSkipped: 'start', maxBarThickness: 28,
  })), R.energyLabel, xOpts, !MULTI, MULTI, {
    // The arithmetic behind the bar: its two meter readings and their difference.
    afterBody: (items) => {
      const r = (items[0] && items[0].raw) || {};
      if (r.s == null || r.e == null) return [];
      return [
        'Start reading: ' + r.s.toFixed(3) + ' kWh',
        'End reading: '   + r.e.toFixed(3) + ' kWh',
        'Generated: '     + (r.y || 0).toFixed(3) + ' kWh',
      ];
    },
  });
  powerChart = makeChart('chart-power', 'line', per.map(r => {
    const c = chColour(r.ch).line;
    // A day at the 5-min log interval is ~288 points; full-size markers turn
    // that into a solid band, so drop them once the series gets dense.
    const dense = r.power.length > 60;
    return {
      label: MULTI ? `Meter ${r.ch}` : 'Avg power (W)',
      data: r.power,
      borderColor: c, pointBackgroundColor: c,
      // Soft fade under a single line; overlapping fades would muddy several.
      fill: MULTI ? false : 'origin',
      backgroundColor: MULTI ? c : MeterTheme.areaFade(c, 0.35),
      tension: dense ? 0.1 : 0.25,
      pointRadius: dense ? 0 : 3, pointHitRadius: 6,
      borderWidth: dense ? 2 : 2.5,
      borderCapStyle: 'round', borderJoinStyle: 'round',
    };
  }), 'W', xOpts, false, MULTI);

  // Stats. Period total is the energy used in the window: each range's single
  // start->end meter difference (server total_kwh, one MAX-MIN over the whole
  // window) rather than a sum of the bars, which would drop the energy accrued
  // in the gaps between buckets. It is a difference, so the old-meter baseline
  // does NOT belong in it — that goes on the Meter reading card instead.
  //
  // Across meters these are SUMMED for the combined cards: total energy is
  // additive, and peak is the highest instantaneous draw seen on any meter.
  const periodTotal = per.reduce((a, r) => a + r.total, 0);

  // Meter reading card: the latest cumulative reading, continued from the
  // replaced meter (capacity_kw). Each channel's `latest` already carries
  // (capacity_kw - origin); with several meters the baseline belongs to the
  // DEVICE, so it is counted once — the extra copies are taken back out.
  const withLatest = per.filter(r => r.ok && r.latest != null);
  const baseline   = per.reduce((b, r) => b || r.baseline, 0);
  const meterNow   = withLatest.length
    ? withLatest.reduce((a, r) => a + r.latest, 0) - baseline * (withLatest.length - 1)
    : null;
  const meterEl = document.getElementById('stat-meter');
  meterEl.textContent = meterNow === null ? '—' : fmtKwhGrouped(meterNow);
  // A five-digit reading drops a size so it stays inside the tile on phones.
  meterEl.classList.toggle('long', meterEl.textContent.length > 7);

  // Meter reading across the charted window: where the counter stood at the
  // first bucket and where it stands at the last, both already continuing from
  // the replaced meter's reading. Shown as text so it survives the bar labels
  // being dropped on a narrow chart.
  const fmtKwh = v => Number(v).toLocaleString(undefined,
                       { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const readings = per.filter(r => r.ok).map(r => {
    const first = r.energy.find(p => p.s != null);
    const last  = r.energy.findLast ? r.energy.findLast(p => p.e != null)
                                    : [...r.energy].reverse().find(p => p.e != null);
    if (!first || !last) return null;
    return (MULTI ? `Meter ${r.ch}: ` : '') +
           `<b>${fmtKwh(first.s)}</b> &rarr; <b>${fmtKwh(last.e)}</b> kWh ` +
           `<span class="gen">(${fmtKwh(last.e - first.s)} kWh)</span>`;
  }).filter(Boolean);
  document.getElementById('meter-reading').innerHTML =
    readings.length ? 'Meter reading: ' + readings.join('<br>') : '';
  renderReadings(per, R);

  const peakP = per.reduce((m, r) => Math.max(m, r.peak || 0), 0);
  document.getElementById('stat-total').textContent = periodTotal.toFixed(2);
  document.getElementById('stat-peak').textContent  = peakP.toFixed(0);

  // Per-meter breakdown row (All-meters mode only).
  const pmEl = document.getElementById('per-meter');
  pmEl.hidden = !MULTI;
  if (MULTI) {
    pmEl.innerHTML = '';
    per.forEach(r => {
      const peak = r.peak || 0;
      const d = document.createElement('div');
      d.className = 'stat';
      d.dataset.ch = String(r.ch);   // loadLive() fills in the live watts below
      d.innerHTML =
        `<span class="meter-name"><span class="swatch" style="background:${chColour(r.ch).line}"></span>Meter ${r.ch}</span>` +
        `<div class="stat-val"><b>${r.total.toFixed(2)}</b><i>kWh</i></div>` +
        `<div class="meter-sub">` +
        `<span class="live-w">— W now</span> · peak ${peak.toFixed(0)} W</div>`;
      pmEl.appendChild(d);
    });
  }

  // "Current" comes from a raw query of the last hour
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

}

let currentRangeKey = 'today';
const customToggle = document.getElementById('btn-custom');
const customPanel  = document.getElementById('custom-range');
const customFrom   = document.getElementById('custom-from');
const customTo     = document.getElementById('custom-to');
const customApply  = document.getElementById('custom-apply');
const ymd = d => isoLocal(d).slice(0, 10);

function setActiveRange(btn){
  document.querySelectorAll('.range-buttons button, #btn-custom').forEach(x => x.classList.remove('on'));
  btn.classList.add('on');
}
function showCustomPanel(show){
  customPanel.hidden = !show;
  customToggle.setAttribute('aria-expanded', String(show));
  customToggle.classList.toggle('open', show);
}

// The fixed ranges load straight away and put the date filter away again.
document.querySelectorAll('.range-buttons button[data-range]').forEach(b => {
  b.addEventListener('click', () => {
    showCustomPanel(false);
    setActiveRange(b);
    currentRangeKey = b.dataset.range;
    loadRange(currentRangeKey);
  });
});

// "Custom range" only reveals the From/To filter — Apply is what loads it, so
// the chart doesn't change under you while you're still picking dates.
customToggle.addEventListener('click', () => {
  const opening = customPanel.hidden;
  showCustomPanel(opening);
  if (opening) customFrom.focus();
});

customApply.addEventListener('click', () => {
  const f = customFrom.value, t = customTo.value;
  // Half-filled or reversed dates leave the current view alone rather than
  // blanking the chart.
  if (!f || !t || f > t) {
    alert('Pick a From and To date, with From on or before To.');
    return;
  }
  RANGES.custom = buildCustomRange(f, t);
  setActiveRange(customToggle);   // Custom stays lit as the active range
  currentRangeKey = 'custom';
  loadRange(currentRangeKey);
});

// Custom range starts on the last week and can't be pointed at the future.
(function initCustomDates(){
  const today = ymd(new Date());
  customFrom.value = ymd(daysAgo(6));
  customTo.value   = today;
  customFrom.max = customTo.max = today;
  // Enter in a date field applies the range; without this the surrounding
  // form submits and the page reloads on the old range.
  [customFrom, customTo].forEach(el => el.addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); customApply.click(); }
  }));
})();

// initial load: today
document.querySelector('.range-buttons button[data-range="today"]').click();
// Charts take their colours from the theme tokens, so redraw on a light/dark switch.
MeterTheme.onSchemeChange(() => loadRange(currentRangeKey));

// Chart.js resizes its canvas on rotation by itself, but the tick budget and
// the endpoint labels are decided at build time — so rebuild when the viewport
// crosses the phone/desktop breakpoint. Debounced: a rotation fires a burst of
// resize events, and each rebuild refetches the range.
(function rebuildOnBreakpointChange(){
  let wasNarrow = narrowScreen(), timer = null;
  window.addEventListener('resize', () => {
    const isNarrow = narrowScreen();
    if (isNarrow === wasNarrow) return;
    wasNarrow = isNarrow;
    clearTimeout(timer);
    timer = setTimeout(() => loadRange(currentRangeKey), 250);
  });
})();

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

  // A multi-relay device gets one pill per relay. The markup ships with a
  // single pill (relay 1), so extra ones are cloned from it on first use and
  // kept in this map keyed by channel.
  const pills = new Map([[1, { el, dot, lbl }]]);

  function pillFor(ch){
    if (pills.has(ch)) return pills.get(ch);
    const clone = el.cloneNode(true);
    clone.removeAttribute('data-on');
    el.parentNode.insertBefore(clone, el.nextSibling);
    const p = {
      el:  clone,
      dot: clone.querySelector('.relay-dot'),
      lbl: clone.querySelector('.relay-label'),
    };
    pills.set(ch, p);
    return p;
  }

  function render(st, target, ch){
    const el  = (target || pills.get(1)).el;
    const dot = (target || pills.get(1)).dot;
    const lbl = (target || pills.get(1)).lbl;
    const prefix = ch && pills.size > 1 ? 'R' + ch + ' ' : '';
    const on = st && st.on, at = st && st.at;
    if (st == null || on == null || !at){
      dot.className = 'relay-dot unknown'; lbl.textContent = prefix + '—';
      el.title = 'No state reported yet'; return;
    }
    const ageSec = (Date.now() - new Date(at.replace(' ', 'T') + APP_TZ_OFFSET).getTime()) / 1000;
    const stale  = !isFinite(ageSec) || ageSec > Math.max(2.5 * (st.interval || 900), 900);
    // NC wiring: relay de-energized = load powered ("Load On"); energized = AC cut.
    const loadOn = !on;
    dot.className = 'relay-dot ' + (stale ? 'stale' : (loadOn ? 'on' : 'off'));
    let text = prefix + (loadOn ? 'LOAD ON' : 'AC CUT');
    if (st.mode && st.mode !== 'auto') text += ' · forced ' + (st.mode === 'on' ? 'cut' : 'on');
    if (stale) text += ' · stale';
    lbl.textContent = text;
    el.title = (ch ? 'Relay ' + ch + ' (meter ' + ch + '): ' : '')
             + (loadOn ? 'Relay de-energized — load on (AC powered)' : 'Relay energized — AC cut')
             + ' · reported ' + at + ' IST';
  }

  // Initial paint of relay 1 from the server-rendered data-* attributes, so the
  // pill isn't blank before the first fetch returns.
  render({
    on:       el.dataset.on === '' ? null : el.dataset.on === '1',
    mode:     el.dataset.mode || null,
    at:       el.dataset.at || null,
    interval: parseInt(el.dataset.int || '900', 10),
  }, pills.get(1), null);

  async function refresh(){
    try {
      const r = await (await fetch(
        `/api/relay_state.php?device_id=${encodeURIComponent(DEVICE_ID)}`,
        { credentials: 'same-origin' })).json();
      if (!r || !r.ok) return;
      // Prefer the per-relay array; fall back to the flat fields for a device
      // (or DB) that predates it.
      const list = (Array.isArray(r.relays) && r.relays.length)
        ? r.relays
        : [{ ch: 1, on: r.on, mode: r.mode, reported_at: r.reported_at }];
      const multi = list.length > 1;
      // Create every pill first: the "R1 " prefix depends on how many pills
      // exist, and relay 1 used to be drawn before relay 2's pill was made.
      list.forEach(x => pillFor(Number(x.ch) || 1));
      list.forEach(x => {
        const ch = Number(x.ch) || 1;
        render({ on: x.on, mode: x.mode, at: x.reported_at, interval: r.interval },
               pillFor(ch), multi ? ch : null);
      });
    } catch (e) { /* keep last paint */ }
  }
  refresh();
  setInterval(refresh, 20_000);
})();
</script>
<?php endif; ?>

</body></html>
