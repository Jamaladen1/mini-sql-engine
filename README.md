# Mini SQL Query Engine (C++20)

A small in-memory relational query engine written from scratch in modern C++.
It loads typed CSV tables, parses a documented SQL subset, builds an
execution plan, runs it, and can use single-column hash indexes for
equality lookups. `EXPLAIN` shows which access path was chosen.

No SQLite or other database is used. Everything from the CSV reader to the
sort operator is in this repository.

```
SQL ─► Lexer ─► Parser ─► AST ─► Validator ─► Planner ─► Executor ─► ResultSet
                                                │
                                   FullScan  or  IndexLookup (hash)
                                                │
                              Filter ─► Sort ─► Limit ─► Project
```

## Features

- `SELECT * | col, ...  FROM table`
- `WHERE` with `=`, `!=`, `<`, `<=`, `>`, `>=`, joined by `AND`
- `ORDER BY col [ASC|DESC]` (deterministic: ties broken by original row order)
- `LIMIT n`
- `EXPLAIN SELECT ...`
- Two column types: `INT64` and `TEXT`, declared up front. No guessing.
- Single-column hash indexes. The planner uses one for an `=` predicate on an indexed column.
- Clear errors with byte offsets (SQL) or line numbers (CSV). Bad input never crashes the process.

Not supported on purpose: JOIN, GROUP BY, aggregates, OR/NOT, NULL,
INSERT/UPDATE/DELETE, transactions, persistence. These words are rejected
with an `unsupported SQL feature` error instead of being silently
misread. See [DESIGN.md](DESIGN.md) for the exact grammar and semantics.

## Build and test

Requires CMake ≥ 3.16 and a C++20 compiler (GCC 11+, Clang 14+, Apple clang 14+).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
ctest --test-dir build --output-on-failure
```

Tests: `parser_tests`, `csv_tests`, `execution_tests`, `index_tests` (unit and
integration, using a tiny built-in test harness) plus `cli_smoke`, an
end-to-end shell test of the CLI binary.

Sanitizers (ASan + UBSan) on Linux:

```bash
scripts/run_sanitizers.sh
```

On macOS, Apple clang's AddressSanitizer can crash during its own startup,
so run the same script inside Docker:

```bash
scripts/run_sanitizers_docker.sh
```

## Using the CLI

`minisql` is a REPL. All commands in one session share one in-memory
catalog. Separate `minisql` processes do **not** share state, so load and
query in the same session.

```bash
./build/minisql
```

```
minisql> load users data/users.csv --schema data/users.schema.json
Loaded 20 rows into 'users'
minisql> SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10;
+----+------------+
| id | name       |
+----+------------+
|  3 | Carol, PhD |
|  1 | Alice      |
| 10 | Judy       |
| 19 | Yara       |
|  5 | Eve        |
| 13 | Peggy      |
+----+------------+
(6 rows)
Time: 0.011 ms
minisql> index users id
Created hash index on users(id)
minisql> EXPLAIN SELECT * FROM users WHERE id = 42
Project [id, name, age, city]
  Limit none
    Sort none (row id order)
      Filter none
        IndexLookup table=users index=hash(id) key=42
```

| Command | What it does |
|---|---|
| `load <table> <file.csv> --schema <schema.json>` | Load a CSV using a JSON schema |
| `load <table> <file.csv> --columns id:INT64,name:TEXT` | Load a CSV with an inline schema |
| `index <table> <column>` (alias `create-index`) | Build a hash index |
| `query <SELECT ...>` or a bare `SELECT ...` line | Run a query |
| `explain <SELECT ...>` or `EXPLAIN SELECT ...` | Show the plan without running it |
| `tables` | List tables and row counts |
| `schema <table>` | Show columns, types and indexes |
| `help`, `quit` / `exit` | |

Each statement is one line. Lines starting with `#` or `--` are comments.

Non-interactive use, all in one process:

```bash
./build/minisql -e "load users data/users.csv --schema data/users.schema.json" -e "index users id" -e "SELECT * FROM users WHERE id = 7"
```

```bash
./build/minisql -f data/demo.sql
```

`data/demo.sql` ends with two intentional errors to show error reporting,
so that command exits with status 1. With `-e`, `-f` or piped input, the
exit status is 1 if any command failed.

### Schema files

```json
{
  "table": "users",
  "columns": [
    {"name": "id",   "type": "INT64"},
    {"name": "name", "type": "TEXT"}
  ]
}
```

The CSV header must list exactly these column names, in this order.
`data/schema.json` and `data/users.schema.json` are the same file under the
two names used in the design document.

### CSV rules

Comma-separated. Fields may be double-quoted, `""` inside quotes is a
literal quote, and quoted fields may contain commas and newlines. LF and
CRLF line endings both work. Empty TEXT cells are empty strings. An empty
INT64 cell is an error, because there is no NULL.

## Benchmark

```bash
./build/minisql_bench
```

Options: `--rows 10000,100000` `--iterations 25` `--warmup 3` `--seed 42`.

For each table size and query, the benchmark runs the query with no index
(full scan), builds the index, and runs it again (index lookup). It times
the whole `Engine::run` call (parse, validate, plan, execute) with
`std::chrono::steady_clock` and reports the median. It also checks that
both paths return identical rows, and exits nonzero if they ever differ.

Results from one run on an Apple M5 laptop (Release build, Apple clang 14).
Your numbers will differ. Rerun the command to get yours.

| rows | query | rows returned | full scan median (ms) | index median (ms) | speedup | parity |
|---:|---|---:|---:|---:|---:|---|
| 10,000 | `SELECT * FROM users WHERE id = 5007` | 1 | 0.0540 | 0.0019 | 28.8x | identical |
| 10,000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 963 | 0.1358 | 0.0390 | 3.5x | identical |
| 10,000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 263 | 0.1056 | 0.0116 | 9.1x | identical |
| 100,000 | `SELECT * FROM users WHERE id = 50007` | 1 | 0.4985 | 0.0017 | 291.9x | identical |
| 100,000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 10,052 | 2.3288 | 0.2862 | 8.1x | identical |
| 100,000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 2,560 | 1.3037 | 0.1323 | 9.9x | identical |

Unique-key lookups gain the most. Low-selectivity keys (10% of rows match
`city = 'Columbus'`) gain less, because most of the time goes into copying
the matching rows out.

### Generating larger CSVs

`scripts/generate_data.py` writes the same data the benchmark generates in
memory (same generator, same seed), so you can query it in the REPL:

```bash
python3 scripts/generate_data.py --rows 100000 --out data/users_100k.csv
```

```bash
./build/minisql -e "load users data/users_100k.csv --schema data/users.schema.json" -e "SELECT * FROM users WHERE id = 50007" -e "index users id" -e "SELECT * FROM users WHERE id = 50007"
```

## Docker

The image build compiles the project and runs the full test suite.

```bash
docker build -t minisql .
```

```bash
docker run --rm -it minisql
```

```bash
docker run --rm minisql minisql_bench
```

The sample data is in `/app/data` inside the container, so
`load users data/users.csv --schema data/users.schema.json` works as is.

## Project layout

```
include/minisql/   public headers (value, catalog, csv, lexer, ast, parser,
                   validator, planner, executor, engine, datagen, error)
src/               engine implementation (the minisql_core library)
apps/              minisql_cli.cpp (REPL), minisql_bench.cpp
tests/             unit/integration tests + CLI smoke test
data/              sample CSV, schema, demo script
scripts/           data generator, sanitizer scripts
```

More detail: [DESIGN.md](DESIGN.md) (architecture and semantics),
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) (requirement → file → test
mapping), [TEST_REPORT.md](TEST_REPORT.md) (commands run and their results).
