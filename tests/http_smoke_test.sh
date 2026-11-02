#!/usr/bin/env bash
# End-to-end test of minisql_server over real sockets.
# Usage: tests/http_smoke_test.sh <path-to-minisql_server>  (run from repo root)
#
# Responses are captured into variables before matching. Piping curl into
# `grep -q` under pipefail is racy: grep exits at the first match, curl can
# then fail writing to the closed pipe, and the check fails spuriously.
set -euo pipefail
BIN="${1:-build/minisql_server}"
PORT=$(( 20000 + RANDOM % 20000 ))
BASE="http://127.0.0.1:$PORT"
LOG=$(mktemp)
fail() { echo "HTTP SMOKE FAIL: $*"; echo "--- server log:"; cat "$LOG"; exit 1; }
# has <haystack> <needle>: literal substring match
has() { [[ "$1" == *"$2"* ]]; }

"$BIN" --port "$PORT" --data data --web web 2>"$LOG" &
PID=$!
cleanup() { kill "$PID" 2>/dev/null || true; rm -f "$LOG"; }
trap cleanup EXIT

for _ in $(seq 50); do
  curl -sf "$BASE/health" >/dev/null 2>&1 && break
  sleep 0.1
done
has "$(curl -s "$BASE/health")" '"status":"ok"' || fail "health check"

# Static page with security headers.
headers=$(curl -s -D - -o /dev/null "$BASE/")
grep -qi "^content-type: text/html" <<<"$headers"                  || fail "index content type"
grep -qi "^content-security-policy: default-src 'self'" <<<"$headers" || fail "CSP header"
grep -qi "^x-content-type-options: nosniff" <<<"$headers"          || fail "nosniff header"
has "$(curl -s "$BASE/app.js")" "use strict"                       || fail "app.js"

# API.
has "$(curl -s "$BASE/api/tables")" '"name":"users_100k_indexed"'  || fail "tables"
q() { curl -s -H 'Content-Type: application/json' -d "$2" "$BASE/api/$1"; }
has "$(q query '{"sql":"SELECT id, name FROM users WHERE age >= 21 AND city = '"'"'Columbus'"'"' ORDER BY age DESC LIMIT 10"}')" \
  '"rows":[[3,"Carol, PhD"]' || fail "query result"
has "$(q explain '{"sql":"SELECT * FROM users_100k_indexed WHERE id = 7"}')" '"access":"IndexLookup"' || fail "explain"
has "$(q query '{"sql":"SELECT * FROM users WHERE age = '"'"'x'"'"'"}')" '"kind":"semantic"' || fail "semantic error"
has "$(q load-sample '{}')" '"name":"users"' || fail "load-sample"

code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
[ "$(code -H 'Content-Type: application/json' -d 'not json' "$BASE/api/query")" = 400 ] || fail "bad JSON -> 400"
[ "$(code -d '{"sql":"x"}' "$BASE/api/query")" = 415 ]                  || fail "form body -> 415"
[ "$(code -H 'Content-Type: text/plain' -d '' "$BASE/api/load-sample")" = 415 ] || fail "text/plain load-sample -> 415"
[ "$(code "$BASE/api/query")" = 405 ]                                    || fail "GET query -> 405"
[ "$(code "$BASE/nope")" = 404 ]                                         || fail "unknown path -> 404"
[ "$(code --path-as-is "$BASE/../data/users.csv")" = 404 ]               || fail "path traversal not rejected"
big=$(head -c 20000 /dev/zero | tr '\0' 'a')
[ "$(code -H 'Content-Type: application/json' -d "{\"sql\":\"$big\"}" "$BASE/api/query")" = 413 ] || fail "large body -> 413"

# Host / Origin checks (DNS rebinding and cross-site requests).
[ "$(code -H 'Host: evil.example' "$BASE/health")" = 403 ]               || fail "foreign Host -> 403"
[ "$(code -H "Host: localhost:$PORT" "$BASE/health")" = 200 ]            || fail "localhost Host -> 200"
[ "$(code -H 'Origin: http://evil.example' -H 'Content-Type: application/json' -d '{}' "$BASE/api/load-sample")" = 403 ] \
  || fail "foreign Origin -> 403"
[ "$(code -H "Origin: http://127.0.0.1:$PORT" -H 'Content-Type: application/json' -d '{}' "$BASE/api/load-sample")" = 200 ] \
  || fail "same Origin -> 200"

# Raw malformed requests, then check the server still answers.
raw() { exec 3<>"/dev/tcp/127.0.0.1/$PORT"; printf '%b' "$1" >&3; cat <&3; exec 3<&-; }
has "$(raw 'GARBAGE\r\n\r\n')" "HTTP/1.1 400"                                         || fail "garbage request -> 400"
has "$(raw "POST /api/query HTTP/1.1\r\nHost: 127.0.0.1:$PORT\r\nTransfer-Encoding: chunked\r\n\r\n")" "HTTP/1.1 501" \
  || fail "chunked -> 501"
has "$(curl -s --max-time 10 "$BASE/health")" ok || fail "server stopped answering after bad requests"

# Slow client: one byte per second, never finishing the request. The
# server allows each connection 5 s in total, so another client must get
# an answer within about that, not after the slow client gives up.
(
  exec 3<>"/dev/tcp/127.0.0.1/$PORT"
  for c in G E T ' ' / ' ' H T T P / 1 . 1; do printf '%s' "$c" >&3 2>/dev/null || break; sleep 1; done
  exec 3<&-
) &
TRICKLE=$!
sleep 0.5   # let the slow client connect first
start=$(date +%s)
rc=0
health=$(curl -s --max-time 20 -w ' HTTP=%{http_code}' "$BASE/health") || rc=$?
waited=$(( $(date +%s) - start ))
[[ "$health" == *'"status":"ok"'*' HTTP=200' ]] \
  || fail "health check behind a slow client (curl exit $rc, got: $health)"
kill "$TRICKLE" 2>/dev/null || true; wait "$TRICKLE" 2>/dev/null || true
[ "$waited" -le 7 ] || fail "slow client held the server for ${waited}s (limit is 5s per connection)"

# Clean shutdown on SIGTERM.
kill -TERM "$PID"
for _ in $(seq 50); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$PID" 2>/dev/null && fail "server did not exit on SIGTERM"
wait "$PID" || fail "server exited with an error"
grep -q "shutting down" "$LOG" || fail "no shutdown message"

echo "HTTP smoke test passed"
