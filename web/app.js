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
