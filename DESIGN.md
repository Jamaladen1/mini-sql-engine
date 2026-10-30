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

## Not implemented

- The optional HTTP/browser demo (spec section 8, a stretch goal). The core
  library is separate from the CLI, so a thin HTTP wrapper could be added
  later without touching the engine.
- Everything in the spec's non-goals list.
