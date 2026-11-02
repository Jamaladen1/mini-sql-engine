// Browser front end for minisql_server. No dependencies; every value from
// the server is inserted with textContent, never as HTML.
"use strict";

const EXAMPLES = [
  ["Design doc example", "SELECT id, name, age FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10"],
  ["All users", "SELECT * FROM users"],
  ["Escaped quote", "SELECT * FROM users WHERE name = 'O''Brien'"],
  ["Full scan, 100k rows", "SELECT * FROM users_100k WHERE id = 50007"],
  ["Index lookup, 100k rows", "SELECT * FROM users_100k_indexed WHERE id = 50007"],
  ["Index + filter", "SELECT id, name, age FROM users_100k_indexed WHERE city = 'Denver' AND age >= 75 ORDER BY age DESC LIMIT 20"],
  ["LIMIT stops early", "SELECT * FROM users_100k WHERE age > 30 LIMIT 5"],
  ["Type error", "SELECT * FROM users WHERE age = '34'"],
];

const $ = (id) => document.getElementById(id);

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") node.className = v;
    else if (k === "text") node.textContent = v;
    else node.setAttribute(k, v);
  }
  for (const c of children) node.append(c);
  return node;
}

async function api(path, body) {
  const options = body === undefined
    ? { method: "GET" }
    : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) };
  const res = await fetch(path, options);
  let data;
  try {
    data = await res.json();
  } catch {
    throw { kind: "http", message: `Unexpected response (${res.status})` };
  }
  if (!res.ok) throw data.error || { kind: "http", message: `HTTP ${res.status}` };
  return data;
}

// ---- tables sidebar ----

function renderTables(tables) {
  const box = $("tables");
  box.replaceChildren();
  for (const t of tables) {
    const nameButton = el("button", { type: "button", text: t.name });
    nameButton.addEventListener("click", () => {
      $("sql").value = `SELECT * FROM ${t.name} LIMIT 20`;
      run(false);
    });
    const list = el("ul");
    for (const c of t.columns) {
      const li = el("li", {}, el("span", { text: c.name }), el("span", { class: "type", text: c.type }));
      if (c.indexed) li.append(el("span", { class: "badge", title: "hash index", text: "index" }));
      list.append(li);
    }
    box.append(el("div", { class: "table-card" },
      el("h3", {}, nameButton, el("span", { class: "rows", text: `${t.rows.toLocaleString()} rows` })),
      list));
  }
}

async function loadTables(reset = false) {
  try {
    const data = await api(reset ? "/api/load-sample" : "/api/tables", reset ? {} : undefined);
    renderTables(data.tables);
  } catch (e) {
    $("tables").replaceChildren(el("p", { class: "muted", text: `Could not load tables: ${e.message}` }));
  }
}

// ---- query ----

// The engine reports positions as UTF-8 byte offsets; JavaScript strings
// index UTF-16 code units. Walk the string to convert.
function byteToCharIndex(text, byteOffset) {
  const encoder = new TextEncoder();
  let bytes = 0;
  let i = 0;
  for (const ch of text) {
    if (bytes >= byteOffset) break;
    bytes += encoder.encode(ch).length;
    i += ch.length;
  }
  return i;
}

function showError(err, sql) {
  const box = $("error");
  // The engine's message already names the error kind and position.
  box.replaceChildren(el("span", { class: "kind", text: err.message || "Unknown error" }));
  if (typeof err.position === "number" && sql !== undefined && err.kind !== "http") {
    // Point at the failing spot in the query: the line it is on plus a caret.
    const pos = byteToCharIndex(sql, err.position);
    const lineStart = sql.lastIndexOf("\n", pos - 1) + 1;
    const lineEnd = sql.indexOf("\n", pos);
    const line = sql.slice(lineStart, lineEnd === -1 ? undefined : lineEnd);
    const caret = " ".repeat(Math.max(0, pos - lineStart)) + "^";
    box.append(el("pre", { text: `${line}\n${caret}` }));
  }
  box.hidden = false;
  $("output").hidden = true;
}

function stat(label, value) {
  return el("span", { class: "stat" }, document.createTextNode(label + " "), el("strong", { text: value }));
}

function renderResult(data) {
  $("error").hidden = true;
  const stats = $("stats");
  stats.replaceChildren(el("span", {
    class: `access ${data.access}`,
    text: data.access === "IndexLookup" ? "Index lookup" : "Full scan",
  }));
  if (!data.explain) {
    stats.append(
      stat("rows", data.rowCount.toLocaleString()),
      stat("rows examined", data.rowsExamined.toLocaleString()),
      stat("time", `${data.elapsedMs.toFixed(3)} ms`));
  }
  $("plan").textContent = data.plan;

  const results = $("results");
  results.replaceChildren();
  if (data.explain) {
    results.hidden = true;
  } else {
    results.hidden = false;
    if (data.rows.length === 0) {
      results.append(el("div", { class: "empty", text: "No rows." }));
    } else {
      const head = el("tr");
      for (const c of data.columns) {
        head.append(el("th", {}, document.createTextNode(c.name), el("span", { class: "type", text: c.type })));
      }
      const body = el("tbody");
      for (const row of data.rows) {
        const tr = el("tr");
        row.forEach((v, i) => {
          tr.append(el("td", { class: data.columns[i].type === "INT64" ? "num" : "", text: String(v) }));
        });
        body.append(tr);
      }
      results.append(el("table", {}, el("thead", {}, head), body));
    }
  }
  const old = $("output").querySelector(".note");
  if (old) old.remove();
  if (data.truncated) {
    $("output").append(el("p", { class: "note",
      text: `Showing the first ${data.rows.length.toLocaleString()} of ${data.rowCount.toLocaleString()} rows.` }));
  }
  $("output").hidden = false;
}

let busy = false;
async function run(explainOnly) {
  if (busy) return;
  const sql = $("sql").value;
  busy = true;
  $("run").disabled = $("explain").disabled = true;
  try {
    renderResult(await api(explainOnly ? "/api/explain" : "/api/query", { sql }));
  } catch (e) {
    showError(e, sql);
  } finally {
    busy = false;
    $("run").disabled = $("explain").disabled = false;
  }
}

// ---- benchmark ----

const BENCH_ITERATIONS = 15;
const BENCH_QUERIES = [
  { label: "WHERE id = 50007", detail: "unique key, 1 row", where: "WHERE id = 50007" },
  { label: "WHERE city = 'Denver'", detail: "~10% of rows", where: "WHERE city = 'Denver'" },
  { label: "city = 'Denver' AND age >= 75", detail: "index + residual filter", where: "WHERE city = 'Denver' AND age >= 75" },
];

function median(values) {
  const v = [...values].sort((a, b) => a - b);
  const m = v.length >> 1;
  return v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
}

async function timeQuery(sql) {
  const samples = [];
  let last;
  for (let i = 0; i < BENCH_ITERATIONS; i++) {
    last = await api("/api/query", { sql });
    samples.push(last.elapsedMs);
  }
  return { ms: median(samples), result: last };
}

const SVG_NS = "http://www.w3.org/2000/svg";
function svg(tag, attrs, text) {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, String(v));
  if (text !== undefined) node.textContent = text;
  return node;
}

function fmtMs(ms) {
  return ms >= 1 ? `${ms.toFixed(2)} ms` : `${ms.toFixed(3)} ms`;
}

// Grouped horizontal bars on a shared linear scale, so the size difference
// between scan and index is shown honestly (index bars can be very short).
function renderBenchChart(rows) {
  const width = 720, labelW = 230, rightPad = 150, barH = 16, gap = 4, groupGap = 22;
  const plotW = width - labelW - rightPad;
  const height = rows.length * (2 * barH + gap + groupGap);
  const max = Math.max(...rows.map((r) => r.scan));
  const chart = svg("svg", { viewBox: `0 0 ${width} ${height}`, role: "img",
    "aria-label": "Median query time, full scan versus index lookup" });

  rows.forEach((r, i) => {
    const y = i * (2 * barH + gap + groupGap) + 4;
    chart.append(svg("text", { x: 0, y: y + barH - 3, class: "label" }, r.label));
    chart.append(svg("text", { x: 0, y: y + 2 * barH + gap - 3, class: "sub" }, r.detail));
    for (const [k, kind, cls] of [[0, "scan", "bar-scan"], [1, "index", "bar-index"]]) {
      const value = r[kind];
      const by = y + k * (barH + gap);
      const w = Math.max(2, (value / max) * plotW);
      chart.append(svg("rect", { x: labelW, y: by, width: w, height: barH, rx: 3, class: cls }));
      chart.append(svg("text", { x: labelW + w + 6, y: by + barH - 4 }, fmtMs(value)));
    }
    chart.append(svg("text", { x: width - 4, y: y + barH + 6, "text-anchor": "end", class: "speedup" },
      `${(r.scan / r.index).toFixed(1)}× faster`));
  });

  const legend = el("div", { class: "bench-legend" },
    el("span", { class: "scan", text: "Full scan (users_100k)" }),
    el("span", { class: "index", text: "Index lookup (users_100k_indexed)" }));
  $("bench-chart").replaceChildren(chart, legend);
}

async function runBenchmark() {
  const button = $("bench-run");
  const status = $("bench-status");
  button.disabled = true;
  const rows = [];
  try {
    for (const [i, q] of BENCH_QUERIES.entries()) {
      status.textContent = `Running query ${i + 1} of ${BENCH_QUERIES.length}…`;
      const scan = await timeQuery(`SELECT * FROM users_100k ${q.where}`);
      const index = await timeQuery(`SELECT * FROM users_100k_indexed ${q.where}`);
      if (index.result.access !== "IndexLookup" || scan.result.access !== "FullScan") {
        throw { message: "unexpected access path; were the sample tables reset?" };
      }
      // Both tables hold identical rows, so the answers must match. The API
      // returns at most 500 rows, so this compares counts plus those rows.
      const same = scan.result.rowCount === index.result.rowCount &&
        JSON.stringify(scan.result.rows) === JSON.stringify(index.result.rows);
      if (!same) throw { message: `results differ for ${q.label}` };
      rows.push({ ...q, scan: scan.ms, index: index.ms, count: scan.result.rowCount });
      renderBenchChart(rows);
    }
    status.textContent = `Done. Both access paths returned the same row counts ` +
      `(${rows.map((r) => r.count.toLocaleString()).join(", ")}), and every row the API ` +
      `returned (up to 500 per query) matched exactly.`;
  } catch (e) {
    status.textContent = `Benchmark failed: ${e.message || e}`;
  } finally {
    button.disabled = false;
  }
}

// ---- wiring ----

function init() {
  const examples = $("examples");
  for (const [label, sql] of EXAMPLES) {
    const chip = el("button", { type: "button", class: "chip", text: label });
    chip.addEventListener("click", () => { $("sql").value = sql; run(false); });
    examples.append(chip);
  }
  $("run").addEventListener("click", () => run(false));
  $("explain").addEventListener("click", () => run(true));
  $("reload").addEventListener("click", () => loadTables(true));
  $("bench-run").addEventListener("click", runBenchmark);
  $("bench-iterations").textContent = String(BENCH_ITERATIONS);
  $("sql").addEventListener("keydown", (e) => {
    if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      run(false);
    }
  });
  loadTables();
  run(false);
}

init();
