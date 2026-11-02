# Implementation plan

Maps each requirement in the design document to the code that implements
it and the tests that cover it. Milestones M1–M6 were done in order, and
the build and tests were run after each.

## Requirement → files → tests

| Requirement (spec §) | Implementation | Tests |
|---|---|---|
| Typed CSV loading, no inference (§2, §4) | `src/csv.cpp` (`tableFromCsv`, `loadCsvFile`) | `csv_tests`: `table_load_*`, `sample_data_files_load` |
| CSV quoting, `""`, CRLF, empty strings (§4) | `src/csv.cpp` (`parseCsv`) | `csv_tests`: `csv_*` |
| Reject duplicate headers, bad field counts, bad/overflowing ints, empty INT64 (§4) | `src/csv.cpp`, `src/value.cpp` (`parseInt64`) | `csv_tests`: `table_load_bad_integer`, `_out_of_range`, `_empty_int_is_error`, `_duplicate_headers`, `_header_mismatch_and_field_count` |
| Schema file / CLI column spec (§2, §7) | `src/csv.cpp` (`parseSchemaJson`, `parseColumnSpec`) | `csv_tests`: `schema_json_*`, `column_spec_parsing` |
| Value / Row / Table / Catalog ownership (§4) | `include/minisql/value.hpp`, `catalog.hpp`, `src/catalog.cpp` | `execution_tests`: `duplicate_table_load_rejected`; `index_tests` |
| Lexer with offsets, case-insensitive keywords, escapes, signed ints (§5, §6) | `src/lexer.cpp` | `parser_tests`: `lexer_*` |
| Recursive-descent parser, typed AST, syntax errors with positions (§6) | `src/parser.cpp`, `include/minisql/ast.hpp` | `parser_tests`: `parse_*` |
| Explicit rejection of unsupported syntax (§5) | `src/lexer.cpp` (`kUnsupported`), `src/parser.cpp` | `parser_tests`: `parse_unsupported_syntax_rejected_explicitly` |
| Semantic validation: tables, columns, types, duplicate projection, ORDER BY (§6) | `src/validator.cpp` | `execution_tests`: `semantic_errors`, `type_mismatch_is_rejected` |
| Scan / filter / sort / limit / project (§2, §6) | `src/executor.cpp` | `execution_tests`: all golden tests; `fuzz_tests`: random queries vs a reference implementation |
| Deterministic sort, row-ID tie-break; LIMIT 0 (§5) | `src/executor.cpp` | `execution_tests`: `order_by_*`, `full_sort_matches_partial_sort_prefix`, `limit_zero_and_large_limit` |
| Hash index on one column (§2, §4) | `src/catalog.cpp` (`HashIndex`) | `index_tests`: `hash_index_lookup_returns_ascending_row_ids`, `rebuilding_index_is_idempotent` |
| Planner: index for indexed `=`, residual filters (§6) | `src/planner.cpp` (`plan`) | `index_tests`: `planner_*`, `parity_*` |
| EXPLAIN (§2, §6) | `src/planner.cpp` (`explainPlan`) | `execution_tests`: `explain_does_not_execute_and_shows_plan`; `index_tests`: `explain_shows_index_lookup_and_residual`; `cli_smoke` |
| CLI: load, create-index, query, explain, tables, schema, quit (§2, §7) | `apps/minisql_cli.cpp` | `tests/cli_smoke_test.sh` |
| Errors never crash the process (§2, §9) | `apps/minisql_cli.cpp` (catch per command) | `cli_smoke` (error section) |
| Malformed SQL/CSV give controlled errors, no UB (§9) | every stage throws `minisql::Error` | `fuzz_tests`: 85k random/mutated inputs, run under ASan + UBSan in CI |
| Timing with `steady_clock` (§6) | `src/engine.cpp`, `apps/minisql_bench.cpp` | manual run (see TEST_REPORT.md) |
| Benchmark: 10k/100k, warm-up, ≥20 iterations, median, parity (§2, §10) | `apps/minisql_bench.cpp`, `src/datagen.cpp`, `scripts/generate_data.py` | `minisql_bench` exits nonzero on mismatch; run in CI |
| CMake, warnings `-Wall -Wextra -Wpedantic` (§2, §9) | `CMakeLists.txt` (`MINISQL_WERROR` turns warnings into errors) | every build |
| ASan / UBSan (§9) | `MINISQL_SANITIZE` option (`-O1`), `scripts/run_sanitizers*.sh` | CI `sanitizers` job; UBSan in CI `macos` job |
| Dockerfile (§2) | `Dockerfile` (build stage runs ctest) | `docker build` |
| README, DESIGN, sample data, example queries (§2, §7) | `README.md`, `DESIGN.md`, `data/` | README commands run verbatim (TEST_REPORT.md) |
| Optional HTTP demo (§8) | **Not implemented yet** (stretch goal; next) | n/a |

## Interpretations of ambiguous points

- **Schema file name**: spec §7 lists `data/schema.json`, and the CLI
  example uses `data/users.schema.json`. Both exist with identical content.
- **CLI subcommands vs REPL**: spec §7 prefers a single REPL. `minisql` is
  a REPL. `-e`/`-f` run several commands in one process, so they share the
  catalog. Separate processes don't share state (documented in the README).
- **`index` vs `create-index`**: both spellings work.
- **Extra headers** beyond the §7 list: `error.hpp`, `validator.hpp`,
  `engine.hpp`, `datagen.hpp`. The validator is its own stage in the §3
  diagram, so it has its own file.
- **Residual filters**: the index predicate is not re-checked after the
  lookup, because hash equality is exact. All other predicates are.
  Parity tests confirm the results match a full scan.
- **Test framework**: a small header-only harness (`tests/test_framework.hpp`)
  avoids any network dependency at build time.

## Work after the MVP

Each item was its own pull request, reviewed and merged into `main`.

| PR | Change | Files | Tests |
|---|---|---|---|
| [#1](https://github.com/Jamaladen1/mini-sql-engine/pull/1) | Stream candidates through the filter. Stop early on `LIMIT` without `ORDER BY`. `EXPLAIN` shows it | `src/executor.cpp`, `src/planner.cpp` | `execution_tests`: `limit_*`, `early_exit_*`; `index_tests`: rows examined |
| [#2](https://github.com/Jamaladen1/mini-sql-engine/pull/2) | REPL: multi-line SQL ending at `;`, `timing on/off`, `quit` always works | `apps/minisql_cli.cpp` | `cli_smoke` |
| [#3](https://github.com/Jamaladen1/mini-sql-engine/pull/3) | CI: macOS job, benchmark job with summary and artifact, no duplicate runs, timeouts | `.github/workflows/ci.yml` | the CI run itself |
| [#4](https://github.com/Jamaladen1/mini-sql-engine/pull/4) | Randomized robustness and reference-comparison tests. Test filter and per-test timing | `tests/fuzz_tests.cpp`, `tests/test_framework.hpp` | `fuzz_tests` |
