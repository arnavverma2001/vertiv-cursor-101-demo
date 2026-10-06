const ALERTS_LEDE =
  "Active conditions on the monitored fleet. Rules flag UPS load above 80% of nameplate, battery health below 70%, runtime under 10 minutes, and rack inlet temperature above the ASHRAE recommended 27°C.";

const TONE = {
  loadWarningPct: 80,
  loadCriticalPct: 95,
  batteryWarningPct: 70,
  batteryCriticalPct: 50,
  runtimeWarningMin: 10,
  runtimeCriticalMin: 5,
  inletWarningC: 27,
  inletCriticalC: 32,
  outletWarningKw: 6,
  outletCriticalKw: 8,
  supplyWarningDelta: 2.5,
  supplyCriticalDelta: 5,
  fanWarningPct: 90,
  fanCriticalPct: 97,
};

const SERIES_COLOR = {
  load_pct: "#e2a15a",
  output_kw: "#e2a15a",
  battery_health_pct: "#8eb4d6",
  runtime_minutes: "#d7c4a3",
  input_voltage_v: "#d7c4a3",
  output_voltage_v: "#d7c4a3",
  total_kw: "#e2a15a",
  max_outlet_kw: "#ef7a68",
  supply_temp_c: "#79d0c8",
  return_temp_c: "#4ea39c",
  fan_speed_pct: "#d7c4a3",
  inlet_temp_c: "#79d0c8",
  power_kw: "#e2a15a",
};

const state = {
  fleet: null,
  alerts: [],
  rules: [],
  extra: null,
  error: null,
  structureId: "",
  metric: {},
  alertFilter: "all",
  chart: null,
};

let frameValues = {};
let refreshing = false;

const view = document.getElementById("view");

function esc(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function fixed(value, digits) {
  return Number(value).toFixed(digits);
}

function formatMetric(value, unit) {
  if (value === undefined || value === null || Number.isNaN(Number(value))) return "–";
  if (unit === "%") return `${fixed(value, 1)}%`;
  if (unit === "°C") return `${fixed(value, 1)}°`;
  if (unit === "kW") return fixed(value, 2);
  if (unit === "V") return fixed(value, 1);
  if (unit === "min") return fixed(value, 1);
  return String(value);
}

function formatClock(iso) {
  return new Date(iso).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
}

function age(iso) {
  const seconds = Math.max(0, Math.floor((Date.now() - new Date(iso).getTime()) / 1000));
  if (seconds < 60) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
}

function above(value, warning, critical) {
  if (value > critical) return "critical";
  if (value > warning) return "warning";
  return "ok";
}

function below(value, warning, critical) {
  if (value < critical) return "critical";
  if (value < warning) return "warning";
  return "ok";
}

function loadTone(value) {
  return above(value, TONE.loadWarningPct, TONE.loadCriticalPct);
}

function inletTone(value) {
  return above(value, TONE.inletWarningC, TONE.inletCriticalC);
}

function outletTone(value) {
  return above(value, TONE.outletWarningKw, TONE.outletCriticalKw);
}

function metricTone(key, value, context) {
  if (key === "load_pct") return loadTone(value);
  if (key === "battery_health_pct") return below(value, TONE.batteryWarningPct, TONE.batteryCriticalPct);
  if (key === "runtime_minutes") return below(value, TONE.runtimeWarningMin, TONE.runtimeCriticalMin);
  if (key === "inlet_temp_c") return inletTone(value);
  if (key === "max_outlet_kw") return outletTone(value);
  if (key === "fan_speed_pct") return above(value, TONE.fanWarningPct, TONE.fanCriticalPct);
  if (key === "supply_temp_c" && context && context.setpoint_c != null) {
    return above(value - context.setpoint_c, TONE.supplyWarningDelta, TONE.supplyCriticalDelta);
  }
  return "ok";
}

function remember(key, text, tone) {
  frameValues[key] = text;
  if (tone) frameValues[`${key}:tone`] = tone;
}

function slot(key, text, tone) {
  remember(key, text, tone);
  const toneAttr = tone ? ` class="tone" data-tone-k="${key}:tone" data-tone="${tone}"` : "";
  return `<span data-k="${key}"${toneAttr}>${esc(text)}</span>`;
}

function pill(key, status) {
  const label = status === "critical" ? "Critical" : status === "warning" ? "Warning" : "Normal";
  remember(key, label, status);
  return `<span class="pill" data-k="${key}" data-tone-k="${key}:tone" data-tone="${status}">${label}</span>`;
}

function crumbs(parts) {
  const html = parts
    .map((part, index) => {
      const last = index === parts.length - 1;
      const label = esc(part.label);
      if (part.href && !last) return `<a href="${part.href}">${label}</a>`;
      return `<span>${label}</span>`;
    })
    .join('<span class="crumb-sep">/</span>');
  return `<nav class="crumbs" aria-label="Breadcrumb">${html}</nav>`;
}

function hrefFor(alert) {
  return alert.subject_kind === "rack" ? `#/racks/${alert.subject_id}` : `#/devices/${alert.subject_id}`;
}

function orderedAlerts(alerts) {
  return alerts.slice().sort((a, b) => a.subject_name.localeCompare(b.subject_name) || a.rule_id.localeCompare(b.rule_id));
}

function coolingLabel(subtype) {
  return subtype === "crah" ? "CRAH · chilled water" : "CRAC · direct expansion";
}

function findSite(siteId) {
  return state.fleet.sites.find((site) => site.id === siteId) || null;
}

async function getJSON(url) {
  const response = await fetch(url);
  if (!response.ok) {
    const error = new Error(`Request failed: ${response.status}`);
    error.status = response.status;
    throw error;
  }
  return response.json();
}

function paintChrome() {
  const hash = location.hash || "#/";
  const onAlerts = hash.startsWith("#/alerts");
  document.querySelectorAll("[data-nav]").forEach((link) => {
    const active = link.dataset.nav === "alerts" ? onAlerts : !onAlerts;
    link.classList.toggle("is-active", active);
  });
}

function applyValues(values) {
  document.querySelectorAll("[data-k]").forEach((el) => {
    const next = values[el.dataset.k];
    if (next !== undefined && el.textContent !== next) el.textContent = next;
  });
  document.querySelectorAll("[data-tone-k]").forEach((el) => {
    const tone = values[el.dataset.toneK];
    if (tone && el.dataset.tone !== tone) el.dataset.tone = tone;
  });
}

function chromeValues() {
  if (!state.fleet) return;
  const summary = state.fleet.summary;
  const tone = summary.critical_alerts ? "critical" : summary.warning_alerts ? "warning" : "ok";
  remember("header-alerts", String(summary.active_alerts), tone);
  remember("header-tick", `as of ${formatClock(state.fleet.generated_at)}`);
}

function render() {
  paintChrome();
  frameValues = {};
  const built = build();
  chromeValues();
  const structureId = `${built.key}§${Object.keys(frameValues).sort().join("|")}`;
  if (structureId !== state.structureId) {
    const y = window.scrollY;
    view.innerHTML = built.html;
    state.structureId = structureId;
    window.scrollTo(0, y);
  }
  applyValues(frameValues);
  state.chart = built.chart;
  const canvas = document.getElementById("chart-canvas");
  if (canvas && built.chart) drawChart(canvas, built.chart);
}

function build() {
  if (!state.fleet) {
    return {
      key: state.error ? "error" : "loading",
      html: state.error ? `<p class="banner">${esc(state.error)}</p>` : `<p class="loading">Loading the fleet…</p>`,
      chart: null,
    };
  }
  return buildPage();
}

function buildPage() {
  const hash = location.hash || "#/";
  const banner = state.error ? `<p class="banner">${esc(state.error)}</p>` : "";
  let page;
  if (hash === "#/" || hash === "#") page = buildFleet();
  else if (hash === "#/alerts") page = buildAlerts();
  else {
    const site = hash.match(/^#\/sites\/([a-z0-9-]+)$/);
    const rack = hash.match(/^#\/racks\/([a-z0-9-]+)$/);
    const device = hash.match(/^#\/devices\/([a-z0-9-]+)$/);
    if (site) page = buildSite(site[1]);
    else if (rack) page = buildRack(rack[1]);
    else if (device) page = buildDevice(device[1]);
    else page = { key: "missing", html: `<p class="empty">That page isn't on this fleet. <a href="#/">Back to the fleet</a>.</p>`, chart: null };
  }
  return { ...page, key: `${state.error ? "err:" : ""}${page.key}`, html: banner + page.html };
}

function kpis(summary) {
  const hottest = summary.hottest_rack_id
    ? `<a class="kpi" href="#/racks/${summary.hottest_rack_id}">
        <p class="eyebrow">Hottest inlet</p>
        <p class="kpi-value">${slot("kpi-inlet", `${fixed(summary.hottest_inlet_c, 1)}°`, inletTone(summary.hottest_inlet_c))}</p>
        <p class="kpi-sub">${esc(summary.hottest_rack_name)} · ${esc(summary.hottest_site_name)}</p>
      </a>`
    : "";
  const runtime = summary.shortest_runtime_device_id
    ? `<a class="kpi" href="#/devices/${summary.shortest_runtime_device_id}">
        <p class="eyebrow">Shortest runtime</p>
        <p class="kpi-value">${slot("kpi-runtime", `${fixed(summary.shortest_runtime_min, 1)} min`, metricTone("runtime_minutes", summary.shortest_runtime_min))}</p>
        <p class="kpi-sub">${esc(summary.shortest_runtime_name)}</p>
      </a>`
    : "";
  return `<section class="kpis">
    <div class="kpi">
      <p class="eyebrow">IT load</p>
      <p class="kpi-value">${slot("kpi-it", fixed(summary.it_load_kw, 1))} <span style="font-size:16px;color:var(--muted)">kW</span></p>
      <p class="kpi-sub">UPS output ${slot("kpi-ups", fixed(summary.ups_output_kw, 1))} kW</p>
    </div>
    <a class="kpi" href="#/alerts">
      <p class="eyebrow">Active alerts</p>
      <p class="kpi-value">${slot("kpi-alerts", String(summary.active_alerts), summary.critical_alerts ? "critical" : summary.warning_alerts ? "warning" : "ok")}</p>
      <p class="kpi-sub">${slot("kpi-crit", String(summary.critical_alerts))} critical · ${slot("kpi-warn", String(summary.warning_alerts))} warning</p>
    </a>
    ${hottest}
    ${runtime}
  </section>`;
}

function criticalStrip(alerts) {
  const criticals = alerts.filter((alert) => alert.severity === "critical");
  if (!criticals.length) return "";
  const links = criticals
    .map((alert) => `<a href="${hrefFor(alert)}">${esc(alert.subject_name)} · ${esc(alert.rule_name)}</a>`)
    .join("");
  return `<div class="crit-strip"><strong>Critical</strong>${links}</div>`;
}

function deviceChip(device, kind) {
  if (kind === "ups") {
    return `<a class="chip" href="#/devices/${device.id}">
      <div class="chip-top"><span><strong>${esc(device.name)}</strong><span class="chip-kind">UPS · ${fixed(device.rated_kw, 0)} kW nameplate</span></span>${pill(`pill:${device.id}`, device.status)}</div>
      <div class="chip-metrics">${slot(`load:${device.id}`, `${fixed(device.load_pct, 1)}%`, loadTone(device.load_pct))}<span>${slot(`runtime:${device.id}`, `${fixed(device.runtime_minutes, 1)} min`, metricTone("runtime_minutes", device.runtime_minutes))}</span></div>
      <p class="chip-foot">${slot(`out:${device.id}`, fixed(device.output_kw, 1))} kW output · battery ${slot(`batt:${device.id}`, `${fixed(device.battery_health_pct, 1)}%`, metricTone("battery_health_pct", device.battery_health_pct))}</p>
    </a>`;
  }
  return `<a class="chip" href="#/devices/${device.id}">
    <div class="chip-top"><span><strong>${esc(device.name)}</strong><span class="chip-kind">${coolingLabel(device.subtype)}</span></span>${pill(`pill:${device.id}`, device.status)}</div>
    <div class="chip-metrics">${slot(`supply:${device.id}`, `${fixed(device.supply_temp_c, 1)}°`, metricTone("supply_temp_c", device.supply_temp_c, device))}<span>→ ${slot(`return:${device.id}`, `${fixed(device.return_temp_c, 1)}°`)}</span></div>
    <p class="chip-foot">Fan ${slot(`fan:${device.id}`, `${fixed(device.fan_speed_pct, 0)}%`, metricTone("fan_speed_pct", device.fan_speed_pct))} · setpoint ${fixed(device.setpoint_c, 0)}°</p>
  </a>`;
}

function rackRows(room, wide) {
  const head = wide
    ? `<div class="rack-head wide"><span>Rack</span><span>Inlet</span><span>Power</span><span>Max outlet</span><span>Status</span></div>`
    : `<div class="rack-head"><span>Rack</span><span>Inlet</span><span>Power</span><span>Status</span></div>`;
  const rows = room.racks
    .map((rack) => {
      const inlet = slot(`inlet:${rack.id}`, `${fixed(rack.inlet_temp_c, 1)}°`, inletTone(rack.inlet_temp_c));
      const power = slot(`power:${rack.id}`, `${fixed(rack.power_kw, 2)} kW`);
      const outlet = wide ? `<span>${slot(`max:${rack.id}`, `${fixed(rack.max_outlet_kw, 2)} kW`, outletTone(rack.max_outlet_kw))}</span>` : "";
      return `<a class="rack-row${wide ? " wide" : ""}" href="#/racks/${rack.id}"><span class="name">${esc(rack.name)}</span><span>${inlet}</span><span>${power}</span>${outlet}${pill(`pill:${rack.id}`, rack.status)}</a>`;
    })
    .join("");
  return `<div class="rack-table">${head}${rows}</div>`;
}

function roomCard(room, wide) {
  const ups = room.ups.map((device) => deviceChip(device, "ups")).join("");
  const cooling = room.cooling.map((device) => deviceChip(device, "cooling")).join("");
  return `<section class="room card">
    <div class="room-head"><h3>${esc(room.name)}</h3><span>${esc(room.role)}</span></div>
    <div class="pair">${ups}${cooling}</div>
    ${rackRows(room, wide)}
  </section>`;
}

function siteCard(site, wide) {
  const rooms = site.rooms.map((room) => roomCard(room, wide)).join("");
  return `<article class="panel" style="padding:16px">
    <div class="site-head">
      <div>
        <h2><a href="#/sites/${site.id}">${esc(site.name)}</a></h2>
        <p class="sub">${esc(site.location)} · ${esc(site.climate_note)}</p>
      </div>
      ${pill(`pill:site:${site.id}`, site.critical_alerts ? "critical" : site.active_alerts ? "warning" : "ok")}
    </div>
    ${rooms}
  </article>`;
}

function buildFleet() {
  const alertSig = state.alerts.map((alert) => `${alert.id}:${alert.severity}`).join(",");
  return {
    key: `fleet:${alertSig}`,
    html: `${kpis(state.fleet.summary)}${criticalStrip(state.alerts)}<div class="sites">${state.fleet.sites.map((site) => siteCard(site, false)).join("")}</div>`,
    chart: null,
  };
}

function buildSite(siteId) {
  const site = findSite(siteId);
  if (!site) {
    return { key: "missing-site", html: `<p class="empty">Unknown site. <a href="#/">Back to the fleet</a>.</p>`, chart: null };
  }
  return {
    key: `site:${siteId}:${state.alerts.map((alert) => alert.id).join(",")}`,
    html: `${crumbs([{ href: "#/", label: "Fleet" }, { label: site.name }])}
      <div class="page-head"><div><h1>${esc(site.name)}</h1><p class="sub">${esc(site.location)} · ${esc(site.climate_note)}</p></div></div>
      <div class="stack">${site.rooms.map((room) => roomCard(room, true)).join("")}</div>`,
    chart: null,
  };
}

function outletMarkup(outlets) {
  return outlets
    .map((outlet) => {
      const width = Math.min(100, (outlet.kw / TONE.outletCriticalKw) * 100);
      const tone = outletTone(outlet.kw);
      const amps = (outlet.kw * 1000) / 208;
      return `<div class="outlet"><span>${esc(outlet.label)}</span><span class="bar"><span class="bar-fill tone-${tone}" style="width:${width.toFixed(1)}%"></span></span><span>${fixed(outlet.kw, 2)} kW · ${fixed(amps, 1)} A</span></div>`;
    })
    .join("");
}

function buildRack(rackId) {
  const detail = state.extra;
  if (!detail || detail.id !== rackId) {
    return { key: `rack-wait:${rackId}`, html: `<p class="loading">Loading rack…</p>`, chart: null };
  }
  const metricKey = "inlet_temp_c";
  const chart = chartSpec(detail.history, detail.series, detail.thresholds, metricKey);
  return {
    key: `rack:${rackId}:${detail.alerts.map((alert) => alert.id).join(",")}`,
    html: `${crumbs([
      { href: "#/", label: "Fleet" },
      { href: `#/sites/${detail.site_id}`, label: detail.site_name },
      { label: `Rack ${detail.name}` },
    ])}
    <div class="page-head"><div><h1>Rack ${esc(detail.name)}</h1><p class="sub">${esc(detail.site_name)} · ${esc(detail.room_name)}</p></div>${pill(`pill:rackpage:${detail.id}`, detail.alerts.some((alert) => alert.severity === "critical") ? "critical" : detail.alerts.length ? "warning" : "ok")}</div>
    <div class="layout">
      <aside class="side card">
        <p class="eyebrow">Inlet temperature</p>
        <p class="hero-metric">${slot("rack-hero", fixed(detail.inlet_temp_c, 1), inletTone(detail.inlet_temp_c))}<span>°C</span></p>
        <ul class="meta-list">
          <li><span>Rack power</span><strong>${slot("rack-kw", `${fixed(detail.power_kw, 2)} kW`)}</strong></li>
          <li><span>Fed by</span><strong><a href="#/devices/${detail.ups_id}">${esc(detail.ups_name)}</a></strong></li>
          <li><span>PDU</span><strong><a href="#/devices/${detail.pdu.id}">${esc(detail.pdu.name)}</a></strong></li>
          <li><span>Cooling</span><strong>${detail.cooling.map((unit) => `<a href="#/devices/${unit.id}">${esc(unit.name)}</a>`).join(", ")}</strong></li>
        </ul>
      </aside>
      <section class="chart-card card">
        <h2>Inlet history</h2>
        <p class="sub">Updated every ${fixed(state.fleet.tick_seconds, 0)}s · ${detail.history.length} readings</p>
        <div class="chart-wrap"><canvas id="chart-canvas"></canvas></div>
        <p class="chart-note" id="chart-note"></p>
      </section>
    </div>
    <section class="card" style="margin-top:14px;padding:16px">
      <h2 style="margin:0 0 10px;font-size:18px">Outlets · ${esc(detail.pdu.name)}</h2>
      <div class="outlets">${outletMarkup(detail.pdu.outlets)}</div>
      <p class="outlet-note">Bars scale to 8 kW. The mark is the 6 kW warning. Amps assume 208 V.</p>
    </section>`,
    chart,
  };
}

function buildDevice(deviceId) {
  const detail = state.extra;
  if (!detail || detail.id !== deviceId) {
    return { key: `device-wait:${deviceId}`, html: `<p class="loading">Loading device…</p>`, chart: null };
  }
  const metricKey = state.metric[deviceId] || (detail.series[0] && detail.series[0].key);
  const chart = chartSpec(detail.history, detail.series, detail.thresholds, metricKey);
  const tiles = detail.series
    .map((series) => {
      const value = detail.metrics[series.key];
      const on = series.key === metricKey ? " is-on" : "";
      return `<button type="button" class="series${on}" data-metric="${series.key}" data-subject="${detail.id}">
        <span><small>${esc(series.label)}</small></span>
        <b>${slot(`m:${detail.id}:${series.key}`, formatMetric(value, series.unit), metricTone(series.key, value, detail))}</b>
      </button>`;
    })
    .join("");
  const outlets = detail.outlets.length
    ? `<section class="card" style="margin-top:14px;padding:16px"><h2 style="margin:0 0 10px;font-size:18px">Outlets</h2><div class="outlets">${outletMarkup(detail.outlets)}</div><p class="outlet-note">Bars scale to 8 kW. The mark is the 6 kW warning. Amps assume 208 V.</p></section>`
    : "";
  const crumbsParts = [
    { href: "#/", label: "Fleet" },
    { href: `#/sites/${detail.site_id}`, label: detail.site_name },
  ];
  if (detail.rack_id) crumbsParts.push({ href: `#/racks/${detail.rack_id}`, label: `Rack ${detail.rack_name}` });
  crumbsParts.push({ label: detail.name });
  const kindLabel = detail.kind === "cooling" ? coolingLabel(detail.subtype) : detail.kind === "ups" ? "Uninterruptible power supply" : "Power distribution unit";
  const nameplate = detail.rated_kw ? ` · ${fixed(detail.rated_kw, 0)} kW nameplate` : "";
  const upstream = detail.fed_by_name ? ` · fed by ${detail.fed_by_name}` : "";
  const status = detail.alerts.some((alert) => alert.severity === "critical") ? "critical" : detail.alerts.length ? "warning" : "ok";
  const selected = detail.series.find((series) => series.key === metricKey);
  return {
    key: `device:${deviceId}:${metricKey}:${detail.alerts.map((alert) => alert.id).join(",")}`,
    html: `${crumbs(crumbsParts)}
      <div class="page-head"><div><h1>${esc(detail.name)}</h1><p class="sub">${esc(kindLabel)} · ${esc(detail.room_name)}${esc(upstream)}${nameplate}</p></div>${pill(`pill:device:${detail.id}`, status)}</div>
      <div class="layout">
        <aside class="side">${tiles}</aside>
        <section class="chart-card card">
          <h2>${esc(selected ? selected.label : "Telemetry")}</h2>
          <p class="sub">Updated every ${fixed(state.fleet.tick_seconds, 0)}s · ${detail.history.length} readings</p>
          <div class="chart-wrap"><canvas id="chart-canvas"></canvas></div>
          <p class="chart-note" id="chart-note"></p>
        </section>
      </div>
      ${outlets}
      ${detail.alerts.length ? `<section class="rules"><h2>Open on this device</h2><div class="alert-list">${detail.alerts.map(alertRow).join("")}</div></section>` : ""}`,
    chart,
  };
}

function alertRow(alert) {
  return `<a class="alert-row card" data-sev="${esc(alert.severity)}" href="${hrefFor(alert)}">
    ${pill(`pill:alert:${alert.id}`, alert.severity)}
    <span><strong>${esc(alert.rule_name)}</strong><p>${esc(alert.message)}</p></span>
    <span><strong>${esc(alert.subject_name)}</strong><p>${esc(alert.site_name)} · ${esc(alert.room_name)}</p></span>
    <span class="mono">${slot(`aval:${alert.id}`, formatMetric(alert.value, alert.unit), alert.severity)}</span>
    <span class="mono dim">${slot(`age:${alert.id}`, age(alert.opened_at))}</span>
  </a>`;
}

function buildAlerts() {
  const filtered = orderedAlerts(state.alerts).filter((alert) => state.alertFilter === "all" || alert.severity === state.alertFilter);
  const rows = filtered.length ? filtered.map(alertRow).join("") : `<p class="empty">No ${state.alertFilter === "all" ? "active " : state.alertFilter + " "}alerts.</p>`;
  const filters = ["all", "critical", "warning"]
    .map((name) => `<button type="button" class="chip-btn${state.alertFilter === name ? " is-on" : ""}" data-filter="${name}">${name[0].toUpperCase()}${name.slice(1)}</button>`)
    .join("");
  const rules = state.rules
    .map(
      (rule) => `<article class="rule card"><h3>${esc(rule.name)}</h3><p data-k="rule:${rule.id}">${esc(rule.description)}</p><code>${esc(rule.id)}</code></article>`,
    )
    .join("");
  state.rules.forEach((rule) => remember(`rule:${rule.id}`, rule.description));
  const sig = state.alerts.map((alert) => `${alert.id}:${alert.severity}`).join(",") + state.rules.map((rule) => rule.description).join("~");
  return {
    key: `alerts:${state.alertFilter}:${sig}`,
    html: `<div class="page-head"><div><h1>Active alerts</h1><p class="lede">${esc(ALERTS_LEDE)}</p></div></div>
      <div class="filters">${filters}</div>
      <div class="alert-list">${rows}</div>
      <section class="rules"><h2>Rule reference</h2><div class="rule-grid">${rules}</div></section>`,
    chart: null,
  };
}

function chartSpec(history, series, thresholds, metricKey) {
  const spec = series.find((item) => item.key === metricKey) || series[0];
  if (!spec) return null;
  return {
    unit: spec.unit,
    color: SERIES_COLOR[spec.key] || "#e2a15a",
    points: history.map((row) => ({ ts: row.ts, value: row[spec.key] })),
    thresholds: (thresholds || []).filter((line) => line.metric === spec.key),
  };
}

function drawChart(canvas, spec) {
  const dpr = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (!width || !height) return;
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  const pad = { l: 52, r: 14, t: 16, b: 28 };
  const points = spec.points.filter((point) => Number.isFinite(point.value));
  const note = document.getElementById("chart-note");
  if (!points.length) {
    ctx.fillStyle = "#a79b8c";
    ctx.font = "13px Outfit, sans-serif";
    ctx.fillText("Waiting for telemetry", pad.l, height / 2);
    return;
  }
  const dataMin = Math.min(...points.map((point) => point.value));
  const dataMax = Math.max(...points.map((point) => point.value));
  const visible = (spec.thresholds || []).filter((line) => nearSeries(line.value, dataMin, dataMax));
  const lineValues = visible.map((line) => line.value);
  let min = Math.min(dataMin, ...(lineValues.length ? lineValues : [dataMin]));
  let max = Math.max(dataMax, ...(lineValues.length ? lineValues : [dataMax]));
  if (min === max) {
    min -= 1;
    max += 1;
  }
  if (!visible.length) {
    const minSpan = spec.unit === "%" ? 4 : spec.unit === "°C" ? 1.5 : spec.unit === "V" ? 4 : spec.unit === "min" ? 2 : 0.4;
    if (max - min < minSpan) {
      const mid = (min + max) / 2;
      min = mid - minSpan / 2;
      max = mid + minSpan / 2;
    }
  }
  const padY = (max - min) * 0.18;
  min -= padY;
  max += padY;
  const plotW = width - pad.l - pad.r;
  const plotH = height - pad.t - pad.b;
  const yOf = (value) => pad.t + ((max - value) / (max - min)) * plotH;
  const xOf = (index) => (points.length === 1 ? pad.l + plotW / 2 : pad.l + (plotW * index) / (points.length - 1));

  ctx.font = "11px 'IBM Plex Mono', ui-monospace, monospace";
  ctx.lineWidth = 1;
  ctx.strokeStyle = "rgba(243,236,223,0.12)";
  ctx.fillStyle = "#a79b8c";
  for (let i = 0; i < 4; i += 1) {
    const y = pad.t + (plotH * i) / 3;
    ctx.beginPath();
    ctx.moveTo(pad.l, y);
    ctx.lineTo(width - pad.r, y);
    ctx.stroke();
    const value = max - ((max - min) * i) / 3;
    ctx.fillText(formatMetric(value, spec.unit), 6, y + 4);
  }

  visible.forEach((line) => {
    const y = yOf(line.value);
    ctx.save();
    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = line.severity === "critical" ? "rgba(239,122,104,0.9)" : "rgba(226,161,90,0.9)";
    ctx.beginPath();
    ctx.moveTo(pad.l, y);
    ctx.lineTo(width - pad.r, y);
    ctx.stroke();
    ctx.restore();
  });

  ctx.beginPath();
  points.forEach((point, index) => {
    const x = xOf(index);
    const y = yOf(point.value);
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.setLineDash([]);
  ctx.strokeStyle = spec.color;
  ctx.lineWidth = 2;
  ctx.stroke();
  const gradient = ctx.createLinearGradient(0, pad.t, 0, pad.t + plotH);
  gradient.addColorStop(0, hexAlpha(spec.color, 0.28));
  gradient.addColorStop(1, hexAlpha(spec.color, 0));
  ctx.lineTo(xOf(points.length - 1), pad.t + plotH);
  ctx.lineTo(xOf(0), pad.t + plotH);
  ctx.closePath();
  ctx.fillStyle = gradient;
  ctx.fill();

  const last = points[points.length - 1];
  ctx.beginPath();
  ctx.arc(xOf(points.length - 1), yOf(last.value), 3.5, 0, Math.PI * 2);
  ctx.fillStyle = spec.color;
  ctx.fill();

  ctx.fillStyle = "#a79b8c";
  ctx.fillText(formatClock(points[0].ts), pad.l, height - 8);
  const endLabel = formatClock(last.ts);
  ctx.fillText(endLabel, width - pad.r - ctx.measureText(endLabel).width, height - 8);
  if (note) note.textContent = visible.map((line) => line.label).join("   ·   ");
}

function nearSeries(value, dataMin, dataMax) {
  const mid = (dataMin + dataMax) / 2;
  const span = Math.max(dataMax - dataMin, Math.abs(mid) * 0.04, 0.8);
  return value >= dataMin - span * 3 && value <= dataMax + span * 3;
}

function hexAlpha(hex, alpha) {
  const raw = hex.replace("#", "");
  const r = parseInt(raw.slice(0, 2), 16);
  const g = parseInt(raw.slice(2, 4), 16);
  const b = parseInt(raw.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function onViewClick(event) {
  const filter = event.target.closest("[data-filter]");
  if (filter) {
    state.alertFilter = filter.dataset.filter;
    state.structureId = "";
    render();
    return;
  }
  const metric = event.target.closest("[data-metric]");
  if (metric) {
    state.metric[metric.dataset.subject] = metric.dataset.metric;
    state.structureId = "";
    render();
  }
}

async function refresh() {
  if (refreshing) return;
  refreshing = true;
  const hash = location.hash || "#/";
  const device = hash.match(/^#\/devices\/([a-z0-9-]+)$/);
  const rack = hash.match(/^#\/racks\/([a-z0-9-]+)$/);
  let extraError = null;
  try {
    const extraPromise = device
      ? getJSON(`/api/devices/${device[1]}`)
      : rack
        ? getJSON(`/api/racks/${rack[1]}`)
        : Promise.resolve(null);
    const [fleet, alerts, rules, extra] = await Promise.all([
      getJSON("/api/fleet"),
      getJSON("/api/alerts"),
      getJSON("/api/rules"),
      extraPromise.catch((error) => {
        extraError = error;
        return null;
      }),
    ]);
    state.fleet = fleet;
    state.alerts = alerts;
    state.rules = rules;
    state.extra = extra;
    state.error = extraError && extraError.status === 404 ? "That device or rack isn't on this fleet." : null;
  } catch (error) {
    state.error = "The monitor service isn't responding.";
    if (!state.fleet) state.extra = null;
  } finally {
    refreshing = false;
    render();
  }
}

view.addEventListener("click", onViewClick);
window.addEventListener("hashchange", () => {
  state.structureId = "";
  state.extra = null;
  refresh();
});
window.addEventListener("resize", () => {
  const canvas = document.getElementById("chart-canvas");
  if (canvas && state.chart) drawChart(canvas, state.chart);
});

refresh();
setInterval(() => {
  if (state.fleet) render();
}, 1000);
setInterval(refresh, 2000);
