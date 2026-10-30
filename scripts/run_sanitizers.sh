#!/usr/bin/env bash
# Debug build with AddressSanitizer + UndefinedBehaviorSanitizer, then run
# the full test suite (unit tests + CLI smoke test).
#
# Note: Apple clang 14 on recent macOS crashes inside ASan's own startup
# ("asan_init_is_running" CHECK) before any project code runs. On macOS use
# scripts/run_sanitizers_docker.sh instead.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMINISQL_SANITIZE=ON -DMINISQL_WERROR=ON
cmake --build build-asan -j"$(getconf _NPROCESSORS_ONLN)"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
  ctest --test-dir build-asan --output-on-failure
