# Design

This implements the "Mini SQL Query Engine, C++20, MVP specification v1.0"
design document. This file records how each part works and the choices made
where the spec left room.

## Architecture

```
apps/minisql_cli.cpp  (REPL; owns one Engine)
        │
Engine (include/minisql/engine.hpp) ── owns ──► Catalog ─► Table ─► rows, HashIndex
        │
  tokenize() ─► parse() ─► Statement (AST) ─► validate() ─► BoundQuery
                                                  │
                                               plan() ─► PhysicalPlan ─► explainPlan()
                                                  │
                                              execute() ─► ResultSet ─► renderResult()
```

- `minisql_core` is a static library with no terminal I/O. The CLI and the
  benchmark are separate executables that link it.
- The parser only produces an AST. It never looks at the catalog.
- The validator resolves names against the catalog and checks types. Its
  output (`BoundQuery`) uses column indices, not names.
- The planner and executor work only on validated, bound data.

## Data model and ownership

| Type | Definition |
|---|---|
| `Value` | `std::variant<int64_t, std::string>` |
| `ColumnType` | `enum class { Int64, Text }` |
| `Column` | `{ name, type }` |
| `Row` | `std::vector<Value>` |
| `Table` | owns columns, rows, and `unordered_map<column, HashIndex>` |
| `Catalog` | owns tables in `unordered_map<string, Table>` |
| `HashIndex` | `unordered_map<Value, vector<size_t>>`: key → ascending row IDs |

- A row ID is the zero-based position in `Table::rows()`. Rows never change
  after loading, so row IDs can't go stale.
- An index holds only row IDs, never pointers, so it can't dangle. Creating
  an index on a column that already has one rebuilds it.
- `BoundQuery` and `PhysicalPlan` hold a non-owning `const Table*` into the
  catalog. They exist only for the duration of one query. `unordered_map`
  keeps element addresses stable when it rehashes.
- No raw owning pointers. All ownership is by value or standard containers.
- Errors are a single type, `minisql::Error`, with a kind (syntax,
  semantic, csv, schema, catalog, io) and an optional offset or line number.

## SQL subset

```
statement  := [EXPLAIN] query
query      := SELECT projection FROM identifier
              [WHERE predicate (AND predicate)*]
              [ORDER BY identifier [ASC|DESC]]
              [LIMIT integer] [;]
projection := * | identifier (, identifier)*
predicate  := identifier (=|!=|<|<=|>|>=) literal
literal    := signed_integer | 'single-quoted string'
identifier := [A-Za-z_][A-Za-z0-9_]*
```

Choices:

- **Keywords are case-insensitive. Identifiers (table and column names) are
  case-sensitive.** `select ID from users` fails if the column is `id`.
- **String literals** use single quotes. `''` inside a string is one quote.
  Double quotes are rejected with a clear message.
- **Integer literals** are signed 64-bit. A leading `-` directly before a
  digit is part of the literal (`age > -5`). Out-of-range values are
  rejected. `LIMIT` must be ≥ 0.
- **No implicit coercion.** `age = '34'` is an error, as is `city = 5`.
- **Reserved unsupported words**: JOIN, INNER, LEFT, RIGHT, OUTER, ON,
  GROUP, HAVING, OR, NOT, NULL, IS, IN, LIKE, BETWEEN, AS, DISTINCT,
  OFFSET, UNION, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, COUNT, SUM,
  AVG, MIN, MAX. These produce `unsupported SQL feature '<word>'`. So these
  words can't be used as table or column names.
- `<>` is rejected with a hint to use `!=`.
- A duplicate column in the SELECT list is an error.
- `ORDER BY` can use any column of the table, even one not selected.

## Evaluation order

Logically: **WHERE → ORDER BY → LIMIT → projection**.

1. **Access path** produces candidate row IDs in ascending order: either all
   rows (full scan) or the index's ID list for the key.
2. **Filter** applies the remaining ("residual") predicates.
3. **Sort**, if there is an ORDER BY. The order is total: by the key
   (ascending or descending), then by row ID **ascending**, in both
   directions. So results are always deterministic, and ties keep load
   order even for `DESC`. When `LIMIT k` is smaller than the number of rows,
   `std::partial_sort` is used. Because the order is total, this gives the
   same first k rows as a full sort (this is tested).
4. **Limit** truncates. `LIMIT 0` returns no rows but still returns the
   column headers.
5. **Project** copies the selected columns into the result.

Without ORDER BY, rows come out in row-ID order for both access paths. This
is what makes indexed and full-scan results identical.

Candidates are streamed through the filter, so no list of row IDs is built
for a full scan. With `LIMIT k` and no ORDER BY, execution stops as soon as
k rows have passed the filter, because no later row can be in the result.
`EXPLAIN` marks this as `Limit k (stops scan early)`.

## Planner

Rule-based. There is no cost model and none is claimed.

- If any `=` predicate is on a column with an index, the **first such
  predicate in WHERE order** becomes an `IndexLookup`. All other predicates
  become residual filters.
- Otherwise the plan is a `FullScan`, and every predicate is a residual
  filter.
- Range and `!=` predicates never use the hash index.

`EXPLAIN` prints the operator tree from output down to access:

```
Project [id, name]
  Limit 10
    Sort age DESC (tie-break: row id ASC)
      Filter age >= 21
        IndexLookup table=users index=hash(city) key='Columbus'
```

## CSV and schemas

- The parser follows RFC 4180: double-quoted fields, `""` escapes, commas
  and newlines allowed inside quotes, LF or CRLF. A bare CR, a quote inside
  an unquoted field, characters after a closing quote, and unterminated
  quotes are all errors with line numbers.
- The first record is the header. It must match the schema's column names
  exactly, in order. Duplicate headers are rejected.
- Every record must have exactly as many fields as the schema has columns.
- INT64 parsing is strict: optional `-`, digits only, no spaces, no
  decimals, overflow rejected. Empty INT64 cells are errors (no NULL).
- Schemas are JSON (`{"columns":[{"name":..,"type":"INT64"|"TEXT"}]}`,
  type names case-insensitive), read by a small built-in JSON parser, or
  given inline as `name:TYPE,...`.
- Loading a table name that already exists is an error. Tables are
  never silently replaced.

## Benchmark method

`minisql_bench` builds deterministic data in memory (splitmix64, seed 42;
`scripts/generate_data.py` produces the identical CSV). For each size and
query it does 3 warm-up runs and 25 timed runs of `Engine::run` without an
index, then builds the index and repeats. It reports the median, checks that
both paths return identical rows, and computes the speedup from the measured
medians. No numbers are hard-coded.

## Browser demo server

Spec section 8. The server is a separate library (`minisql_http`, in
`apps/server/`) plus the `minisql_server` program. The engine core has no
networking code.

```
browser ── HTTP ──► http_socket.cpp ─► http.cpp ─► api.cpp ─► Engine
                    (accept loop)      (parse)     (routes, JSON)
```

- **HTTP.** A deliberately small HTTP/1.1 subset written for this project
  on POSIX sockets, instead of an outside library. One request per
  connection (`Connection: close`) and `Content-Length` bodies only.
  `Transfer-Encoding` is refused (501), and conflicting `Content-Length`
  headers, header line folding and bytes after the body are rejected
  (400). The parser is a pure function, so it's unit-tested without
  sockets.
- **Concurrency.** One connection at a time. Each connection gets one 5 s
  deadline for the whole exchange, enforced with `poll()` before every
  read and write. A slow or stalled client, even one sending a byte at a
  time, delays others by at most that. Connections close gracefully
  (`shutdown`, then a brief drain), so an early 413 or 400 isn't lost to a
  TCP reset. Fine for a demo; not built for heavy traffic.
- **Limits.** Headers 8 KiB, body 16 KiB, SQL 4 KiB, 500 result rows per
  response (the total count is still reported). Query time is bounded by
  the data: the largest table has 100,000 rows, and every query is at
  worst a scan plus a sort. There's no per-query cancellation.
- **No user paths.** Data comes only from the bundled `data/users.csv` and
  the deterministic generator. Static files are a fixed list read once at
  startup and served by exact path match, so path traversal has nothing to
  reach.
- **Binding and Host.** `127.0.0.1` by default. `0.0.0.0` must be
  requested explicitly (Docker needs it). On `127.0.0.1` only `Host:
  127.0.0.1:<port>` or `localhost:<port>` is answered (403 otherwise),
  which blocks DNS rebinding. `--allow-host` adds names for other setups.
- **Cross-site requests.** Every POST must be `application/json` (415
  otherwise), which a browser can't send cross-origin without a CORS
  preflight that this server never approves. A foreign `Origin` header
  gets 403 when a Host list is set.
- **Browser safety.** Responses send `Content-Security-Policy:
  default-src 'self'`, `nosniff` and `no-store`. The page uses no inline
  scripts or outside resources, and inserts every server value as text,
  never HTML. `jsonQuote` also escapes `<`, `>` and `&`.
- **Numbers.** INT64 values outside ±2^53 are sent as JSON strings, so
  JavaScript never shows a silently rounded number.
- **Valid UTF-8.** `jsonQuote` replaces any invalid UTF-8 byte with
  U+FFFD, so responses are always valid JSON even if CSV data contains
  invalid bytes. The lexer names unexpected non-ASCII bytes by value
  (`unexpected byte 0xC3`) rather than quoting half a character.
- **Errors.** Every `minisql::Error` becomes a 400 with `kind`, `message`
  and the byte `position`. The page turns that into a caret under the
  failing spot.

The benchmark chart runs each query 15 times on `users_100k` and on
`users_100k_indexed` (identical rows), and plots the median of the
engine's own `elapsedMs`, so network time isn't included. It checks that
both tables used the expected access path and returned the same answers
before drawing anything.

## Not implemented

- Everything in the spec's non-goals list.
