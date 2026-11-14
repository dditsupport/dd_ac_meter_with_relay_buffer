// Chart.js styling from the page's colour tokens (style.css :root), so the
// charts follow the same light / dark brand theme as the rest of the page.
// Load after chart.js.
(function () {
  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  // Re-read on every chart build: the tokens change with the colour scheme.
  function applyChartDefaults() {
    if (!window.Chart) return;
    const d = Chart.defaults;
    d.color = cssVar('--on-surface-variant');
    d.borderColor = cssVar('--grid');
    d.font.family = getComputedStyle(document.body).fontFamily;
    d.font.size = 12;
    d.plugins.tooltip.backgroundColor = cssVar('--inverse-surface');
    d.plugins.tooltip.titleColor = cssVar('--inverse-on-surface');
    d.plugins.tooltip.bodyColor = cssVar('--inverse-on-surface');
    d.plugins.tooltip.cornerRadius = 12;
    d.plugins.tooltip.padding = 10;
    d.plugins.tooltip.boxPadding = 4;
  }

  // A vertical fade from `color` (top) to transparent, sized to the plot area
  // once Chart.js knows it. Use as a scriptable backgroundColor.
  function areaFade(color, alpha) {
    return function (ctx) {
      const area = ctx.chart.chartArea;
      if (!area) return 'transparent';
      const g = ctx.chart.ctx.createLinearGradient(0, area.top, 0, area.bottom);
      g.addColorStop(0, withAlpha(color, alpha));
      g.addColorStop(1, withAlpha(color, 0));
      return g;
    };
  }

  // "#rrggbb" + alpha -> "rgba(...)".
  function withAlpha(hex, a) {
    const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
    if (!m) return hex;
    return `rgba(${parseInt(m[1], 16)},${parseInt(m[2], 16)},${parseInt(m[3], 16)},${a})`;
  }

  // Calls `cb` when the device switches between light and dark.
  function onSchemeChange(cb) {
    const mq = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)');
    if (mq && mq.addEventListener) mq.addEventListener('change', cb);
  }

  window.MeterTheme = { cssVar, applyChartDefaults, areaFade, withAlpha, onSchemeChange };
})();
