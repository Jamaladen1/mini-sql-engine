# Test report

Commands that were actually run, and their results. Run date: 2026-10-29.

Host: Apple M5, macOS, Apple clang 14.0.3, CMake 4.x, Docker 29.5.2.
Linux runs used the `ubuntu:24.04` image with its packaged GCC and CMake.

## 1. Release build with warnings as errors (macOS)

```
cmake -S . -B build -DMINISQL_WERROR=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Result: builds with no warnings (`-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion -Werror`). **5/5 CTest targets pass.**

| Suite | Cases |
|---|---|
| parser_tests | 15/15 passed |
| csv_tests | 17/17 passed |
| execution_tests | 17/17 passed |
| index_tests | 11/11 passed |
| cli_smoke | passed |

Problems found and fixed during development:
- `-Wdangling-gsl` caught a test that kept a reference into a temporary
  `std::optional`. Fixed in the test.
- Two test expectations were wrong (a CSV error line number and a row
  count I had worked out by hand). The engine's output was correct, and the
  tests were fixed after checking against the CSV.

## 2. Sanitizers

**macOS ASan (Apple clang 14)**: `-DMINISQL_SANITIZE=ON` builds, but 3
test binaries abort with `AddressSanitizer: CHECK failed:
sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)"` and an
empty stack trace. The crash happens inside ASan's own startup, before
`main`. This is a toolchain problem with this compiler on this macOS
version, not a project bug. So sanitizers were run on Linux instead:

**macOS UBSan only**: `-fsanitize=undefined -fno-sanitize-recover=all`.
**5/5 pass.**

**Linux ASan + UBSan (GCC, ubuntu:24.04)**:

```
scripts/run_sanitizers_docker.sh
```

Result: **5/5 pass, no sanitizer reports** (leak detection on,
`-fno-sanitize-recover=all`, so any finding would fail the test).

## 3. Docker

```
docker build -t minisql .
docker run --rm minisql minisql -e "load users data/users.csv --schema data/users.schema.json" -e "index users id" -e "EXPLAIN SELECT * FROM users WHERE id = 42" -e "SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10;"
docker run --rm minisql minisql_bench --iterations 5
```

Result: the image builds (the build stage runs the full test suite with
`-Werror` on GCC and passes). The container prints the `IndexLookup` plan
and the expected 6 rows (ids 3, 1, 10, 19, 5, 13). The benchmark runs in
the container with parity "identical" on every row.

## 4. Benchmark

```
./build/minisql_bench --rows 10000,100000 --iterations 25 --warmup 3
```

Release build, macOS, Apple M5. Medians of 25 runs after 3 warm-ups,
end-to-end `Engine::run`, in ms:

| rows | query | rows returned | full scan | index | speedup | parity |
|---:|---|---:|---:|---:|---:|---|
| 10000 | `SELECT * FROM users WHERE id = 5007` | 1 | 0.0540 | 0.0019 | 28.8x | identical |
| 10000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 963 | 0.1358 | 0.0390 | 3.5x | identical |
| 10000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 263 | 0.1056 | 0.0116 | 9.1x | identical |
| 100000 | `SELECT * FROM users WHERE id = 50007` | 1 | 0.4985 | 0.0017 | 291.9x | identical |
| 100000 | `SELECT id, name FROM users WHERE city = 'Columbus'` | 10052 | 2.3288 | 0.2862 | 8.1x | identical |
| 100000 | `SELECT id FROM users WHERE city = 'Columbus' AND age >= 65` | 2560 | 1.3037 | 0.1323 | 9.9x | identical |

Exit code 0, meaning every indexed result matched the full-scan result.

## 5. Data generator matches the benchmark data

```
python3 scripts/generate_data.py --rows 1000 | shasum
(C++ generateUsers(1000, 42) written as CSV) | shasum
```

Both give `05c22cdd5072d9c3b041e8dbffcb42403b06a16c`, so the outputs are
byte-identical.

## 6. README commands run verbatim

- `./build/minisql -e "load users data/users.csv --schema data/users.schema.json" -e "index users id" -e "SELECT * FROM users WHERE id = 7"`: returns Grace, exit 0.
- `./build/minisql -f data/demo.sql`: all output as expected, with the two intentional errors at the end. Exit 1, as documented.
- `python3 scripts/generate_data.py --rows 100000 --out data/users_100k.csv`, then the 100k load/query/index/query command: loads 100000 rows. Same row with and without the index (1.458 ms → 0.024 ms in that run).

## Not covered

- The optional HTTP demo (spec §8) was not built, so it has no tests.
- The GitHub Actions workflow (`.github/workflows/ci.yml`) mirrors the
  commands above. Its status is shown on GitHub after each push.
