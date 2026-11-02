#!/usr/bin/env bash
# End-to-end test of minisql_server over real sockets.
# Usage: tests/http_smoke_test.sh <path-to-minisql_server>  (run from repo root)
set -euo pipefail
BIN="${1:-build/minisql_server}"
PORT=$(( 20000 + RANDOM % 20000 ))
BASE="http://127.0.0.1:$PORT"
LOG=$(mktemp)
fail() { echo "HTTP SMOKE FAIL: $*"; echo "--- server log:"; cat "$LOG"; exit 1; }

"$BIN" --port "$PORT" --data data --web web 2>"$LOG" &
PID=$!
cleanup() { kill "$PID" 2>/dev/null || true; rm -f "$LOG"; }
trap cleanup EXIT

for _ in $(seq 50); do
  curl -sf "$BASE/health" >/dev/null 2>&1 && break
  sleep 0.1
done
curl -sf "$BASE/health" | grep -q '"status":"ok"' || fail "health check"

# Static page with security headers.
headers=$(curl -s -D - -o /dev/null "$BASE/")
grep -qi "^content-type: text/html" <<<"$headers"                  || fail "index content type"
grep -qi "^content-security-policy: default-src 'self'" <<<"$headers" || fail "CSP header"
grep -qi "^x-content-type-options: nosniff" <<<"$headers"          || fail "nosniff header"
curl -sf "$BASE/app.js" | grep -q "use strict"                     || fail "app.js"

# API.
curl -sf "$BASE/api/tables" | grep -q '"name":"users_100k_indexed"' || fail "tables"
q() { curl -s -H 'Content-Type: application/json' -d "$2" "$BASE/api/$1"; }
q query '{"sql":"SELECT id, name FROM users WHERE age >= 21 AND city = '"'"'Columbus'"'"' ORDER BY age DESC LIMIT 10"}' \
  | grep -q '"rows":\[\[3,"Carol, PhD"\]' || fail "query result"
q explain '{"sql":"SELECT * FROM users_100k_indexed WHERE id = 7"}' | grep -q '"access":"IndexLookup"' || fail "explain"
q query '{"sql":"SELECT * FROM users WHERE age = '"'"'x'"'"'"}' | grep -q '"kind":"semantic"' || fail "semantic error"

code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
[ "$(code -H 'Content-Type: application/json' -d 'not json' "$BASE/api/query")" = 400 ] || fail "bad JSON -> 400"
[ "$(code -d '{"sql":"x"}' "$BASE/api/query")" = 415 ]                  || fail "form body -> 415"
[ "$(code "$BASE/api/query")" = 405 ]                                    || fail "GET query -> 405"
[ "$(code "$BASE/nope")" = 404 ]                                         || fail "unknown path -> 404"
[ "$(code --path-as-is "$BASE/../data/users.csv")" = 400 ] || [ "$(code --path-as-is "$BASE/../data/users.csv")" = 404 ] \
  || fail "path traversal not rejected"
big=$(head -c 20000 /dev/zero | tr '\0' 'a')
[ "$(code -H 'Content-Type: application/json' -d "{\"sql\":\"$big\"}" "$BASE/api/query")" = 413 ] || fail "large body -> 413"

# Raw malformed requests, then check the server still answers.
raw() { exec 3<>"/dev/tcp/127.0.0.1/$PORT"; printf '%b' "$1" >&3; cat <&3; exec 3<&-; }
raw 'GARBAGE\r\n\r\n' | head -1 | grep -q "400"                                   || fail "garbage request -> 400"
raw 'POST /api/query HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n' | head -1 | grep -q "501" || fail "chunked -> 501"
raw 'GET / HTTP/1.1\r\n' >/dev/null &   # incomplete request: server must time out, not hang
curl -sf --max-time 10 "$BASE/health" | grep -q ok || fail "server stopped answering after bad requests"

# Clean shutdown on SIGTERM.
kill -TERM "$PID"
for _ in $(seq 50); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$PID" 2>/dev/null && fail "server did not exit on SIGTERM"
wait "$PID" || fail "server exited with an error"
grep -q "shutting down" "$LOG" || fail "no shutdown message"

echo "HTTP smoke test passed"
