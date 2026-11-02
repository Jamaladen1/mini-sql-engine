# Test report

Commands that were actually run, and their results. Most recent run:
2026-11-02, on `main` at `fe64e13` (after PRs #1–#4).

- **Local**: Apple M5, macOS, Apple clang 14.0.3, CMake 4.4.5.
- **CI**: GitHub Actions run
  [41397767117](https://github.com/Jamaladen1/mini-sql-engine/actions/runs/41397767117)
  on the same commit, using `ubuntu-24.04` (GCC and Clang) and `macos-latest` runners.

## 1. Build and tests (local, macOS)

```
cmake -S . -B build -DMINISQL_WERROR=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Clean build with zero warnings (`-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion -Werror`). **6/6 CTest targets pass.**

| Suite | Cases | What it covers |
|---|---|---|
| parser_tests | 15/15 | lexer offsets, keywords, escapes, integer bounds, syntax errors, unsupported SQL |
| csv_tests | 17/17 | quoting, CRLF, empty strings, bad/overflowing integers, headers, schema JSON |
| execution_tests | 21/21 | golden results for every operator, AND, ORDER BY ties, LIMIT, LIMIT early exit, errors, EXPLAIN |
| index_tests | 12/12 | index vs full-scan parity, duplicate/missing keys, planner choice, rows examined |
| fuzz_tests | 10/10 | 85k random/mutated SQL, CSV and JSON inputs; random queries vs a reference implementation |
| json_tests | 7/7 | JSON parsing, `\u` escapes and surrogate pairs, `jsonQuote`, round trips (added with the browser demo) |
| http_tests | 10/10 | HTTP request parser limits and rejections, response writer (browser demo) |
| api_tests | 12/12 | every API endpoint, errors, limits, truncation, static files (browser demo) |
| http_smoke | passed | real server over sockets: curl + raw malformed requests, stalled client, SIGTERM shutdown (browser demo) |
| cli_smoke | passed | end-to-end CLI: load, index, explain, errors, piped REPL, multi-line SQL, `timing`, `quit` |

## 2. CI (GitHub Actions, same commit)

| Job | Result | What it runs |
|---|---|---|
| build-test (g++) | pass | Release build with `-Werror`, all tests |
| build-test (clang++) | pass | Release build with `-Werror`, all tests |
| macos | pass | Apple clang build and all tests, plus a UBSan build and all tests |
| sanitizers | pass | `scripts/run_sanitizers.sh`: GCC ASan + UBSan, leak detection on, `-fno-sanitize-recover=all`, all 6 tests |
| benchmark | pass | `minisql_bench` at 10k and 100k rows. Fails if index and scan results differ |
| docker | pass | `docker build` (runs the test suite inside), then a container smoke test |

In the sanitizer job, randomized tests run at 20% of their iterations
(`MINISQL_FUZZ_SCALE=20`), so the job takes about 20 s. A separate run at
100% under GCC ASan + UBSan also passed (10/10), during review of PR #4.

## 3. Sanitizers on macOS

Apple clang 14's AddressSanitizer crashes during its own startup on this
macOS version (`CHECK failed: sanitizer_malloc_mac.inc:189
"((!asan_init_is_running)) != (0)"`, empty stack), before any project
code runs. This is a toolchain problem, so ASan runs on Linux: in CI, or
locally via `scripts/run_sanitizers_docker.sh`. UBSan alone works on macOS
and runs in the CI `macos` job.

## 4. Benchmark (local)

```
./build/minisql_bench --rows 10000,100000 --iterations 25 --warmup 3
```

Release build, Apple M5. Medians of 25 runs after 3 warm-ups, end-to-end
`Engine::run`, in ms. Exit code 0: every indexed result matched the full
scan.

| rows | query | rows returned | full scan | index | speedup | parity |
|---:|---|---:|---:|---:|---:|---|
| 10000 | `SELECT * FROM users WHERE id = 5007` | 1 | 0.0423 | 0.0018 | 23.1x | identical |
| 10000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 963 | 0.1326 | 0.0313 | 4.2x | identical |
| 10000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 263 | 0.1112 | 0.0136 | 8.2x | identical |
| 100000 | `SELECT * FROM users WHERE id = 50007` | 1 | 0.3218 | 0.0017 | 193.1x | identical |
| 100000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 10052 | 1.3104 | 0.2738 | 4.8x | identical |
| 100000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 2560 | 1.0448 | 0.1373 | 7.6x | identical |

The CI benchmark job's numbers on a shared runner are in that run's job
summary. They are noisier but show the same pattern (for example
0.3384 → 0.0011 ms for the 100k unique-id lookup).

**LIMIT early exit (PR #1).** `SELECT id FROM users WHERE age >= 30
LIMIT 10` on 100k rows, median of 15 runs in one CLI session: **0.003 ms**
now, versus 0.833 ms before PR #1 (measured back to back against the
pre-PR build at the time).

## 5. Other checks

- **Generator parity.** `scripts/generate_data.py --rows 1000` and the C++
  generator produce byte-identical CSV (both SHA-1
  `05c22cdd5072d9c3b041e8dbffcb42403b06a16c`).
- **Randomized tests can fail.** Each of these engine bugs was injected on
  purpose and caught by `fuzz_tests`: `<=` evaluated as `<`, and a
  reversed sort tie-break.
- **Smoke checks can fail.** The `quit`-mid-statement check was verified
  to fail with that fix disabled.

## Problems found during development

- GCC `-Wdangling-reference` and clang `-Wdangling-gsl` flagged tests that
  kept references to temporaries. Fixed by making `CHECK_EQ` copy its
  operands.
- The JSON schema reader didn't compile with clang + libstdc++ 14
  (`std::pair` with an incomplete type). Fixed by storing object members
  as parallel arrays.
- The first streaming executor was about 50% slower on full scans. Fixed
  before merge, and confirmed with back-to-back benchmarks.
- The benchmark CI step hid failures behind a pipe into `tee`. Fixed with
  `shell: bash` (pipefail).
- Debug (`-O0`) ASan builds made the randomized tests exceed CTest's
  timeout. Sanitized builds now use `-O1`.

## Browser demo (PR #6)

Run on the `feature/http-demo` branch, 2026-11-02, on the same machine:

- `ctest`: 10/10 targets pass, with the four browser-demo rows above added.
  The build has zero warnings with `-Werror`.
- `tests/http_smoke_test.sh build/minisql_server`: passes in about 6 s,
  most of it waiting out the stalled-client timeout.
- Manually in a browser (desktop and a 375 px phone viewport, light and
  dark): the example queries, the error caret, table links, `Reset`, and
  the benchmark chart. The chart's run returned matching answers on both
  tables, with medians of 0.863 → 0.004 ms for `id = 50007`, 1.11 → 0.337
  ms for `city = 'Denver'`, and 0.876 → 0.105 ms for `city = 'Denver' AND
  age >= 75`.

Linux, sanitizer and Docker results for this change are in the PR's CI checks.

## Not covered

- Load testing the demo server. It handles one connection at a time by
  design (see DESIGN.md).
