#!/usr/bin/env bash
# End-to-end CLI smoke test. Usage: tests/cli_smoke_test.sh <path-to-minisql>
# Run from the repository root.
set -euo pipefail
BIN="${1:-build/minisql}"
fail() { echo "SMOKE FAIL: $*"; exit 1; }

out=$("$BIN" \
  -e "load users data/users.csv --schema data/users.schema.json" \
  -e "tables" \
  -e "schema users" \
  -e "explain SELECT * FROM users WHERE id = 7" \
  -e "index users id" \
  -e "EXPLAIN SELECT * FROM users WHERE id = 7" \
  -e "query SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10;" \
  -e "SELECT name FROM users WHERE id = 7")

grep -q "Loaded 20 rows into 'users'" <<<"$out" || fail "load"
grep -q "users (20 rows)" <<<"$out"                || fail "tables"
grep -q "id INT64  \[hash index\]" <<<"$out" && fail "index shown before creation"
grep -q "FullScan table=users rows=20" <<<"$out"   || fail "explain full scan"
grep -q "IndexLookup table=users index=hash(id) key=7" <<<"$out" || fail "explain index lookup"
grep -q "(6 rows)" <<<"$out"                       || fail "example query row count"
grep -q "| Grace |" <<<"$out"                      || fail "point query"

# Errors are reported without crashing, and set a nonzero exit status.
set +e
err=$("$BIN" -e "SELECT * FROM missing" -e "SELEC 1" -e "load x nofile.csv --columns a:TEXT" -e "tables")
code=$?
set -e
[ "$code" -eq 1 ] || fail "expected exit code 1, got $code"
grep -q "unknown table 'missing'" <<<"$err"        || fail "missing table error"
grep -q "unknown command 'selec'" <<<"$err"        || fail "unknown command error"
grep -q "cannot open file 'nofile.csv'" <<<"$err"  || fail "missing file error"
grep -q "(no tables)" <<<"$err"                    || fail "REPL continued after errors"

# Piped stdin REPL keeps state across lines.
piped=$(printf 'load users data/users.csv --columns id:INT64,name:TEXT,age:INT64,city:TEXT\nindex users city\nSELECT id FROM users WHERE city = '\''Denver'\''\nquit\nSELECT 1\n' | "$BIN")
grep -q "(2 rows)" <<<"$piped" || fail "piped REPL"

echo "CLI smoke test passed"
