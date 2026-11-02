// Randomized tests with fixed seeds, so every run checks the same inputs.
//
// 1. Robustness: random and mutated SQL / CSV / schema text must either
//    succeed or throw minisql::Error. Any other exception, a crash, or a
//    sanitizer report is a bug.
// 2. Differential: random valid queries are run through the engine and
//    through a deliberately naive reference implementation in this file;
//    results must match exactly (rows and order), with and without indexes.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "minisql/csv.hpp"
#include "minisql/datagen.hpp"
#include "minisql/engine.hpp"
#include "minisql/lexer.hpp"
#include "minisql/parser.hpp"
#include "test_framework.hpp"

using namespace minisql;

namespace {

// Iteration counts are multiplied by MINISQL_FUZZ_SCALE percent (default
// 100). Sanitizer builds run 10-50x slower, so scripts/run_sanitizers.sh
// lowers it; the inputs checked are a prefix of the full run's inputs.
int scaled(int n) {
    static const int percent = [] {
        const char* env = std::getenv("MINISQL_FUZZ_SCALE");
        const int v = env ? std::atoi(env) : 100;
        return v > 0 ? v : 100;
    }();
    return std::max(1, static_cast<int>(static_cast<long long>(n) * percent / 100));
}

// splitmix64: same output on every platform and standard library.
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
    bool chance(std::size_t oneIn) { return below(oneIn) == 0; }
    template <class T>
    const T& pick(const std::vector<T>& v) { return v[below(v.size())]; }
};

// Runs `fn`; only minisql::Error is an acceptable failure.
template <class Fn>
void mustSucceedOrThrowError(Fn&& fn, const std::string& input) {
    try {
        fn();
    } catch (const Error&) {
        // expected for invalid input
    } catch (const std::exception& e) {
        throw testfw::Failure("non-minisql exception '" + std::string(e.what()) +
                              "' for input: " + input);
    }
}

Engine sampleEngine() {
    Engine e;
    const std::string dir = MINISQL_SOURCE_DIR "/data/";
    e.loadCsv("users", dir + "users.csv", loadSchemaFile(dir + "users.schema.json"));
    return e;
}

const std::vector<std::string> kSqlPieces{
    "SELECT", "select", "FROM", "WHERE", "AND", "ORDER", "BY", "ASC", "DESC", "LIMIT",
    "EXPLAIN", "users", "id", "name", "age", "city", "nope", "*", ",", ";", "=", "!=", "<",
    "<=", ">", ">=", "<>", "0", "-1", "42", "9223372036854775807", "9223372036854775808",
    "-9223372036854775809", "'Columbus'", "''", "'it''s'", "'open", "\"dq\"", "JOIN", "OR",
    "NULL", "(", ")", "@", "-", "!", "\t", "\n", " ", "1abc", "_x", "\xff",
};

std::string randomSql(Rng& rng) {
    std::string out;
    const std::size_t n = 1 + rng.below(14);
    for (std::size_t i = 0; i < n; ++i) {
        out += rng.pick(kSqlPieces);
        if (!rng.chance(5)) out += ' ';
    }
    return out;
}

// Byte-level mutation: delete, duplicate, or replace a random span.
std::string mutate(Rng& rng, std::string s) {
    const std::size_t edits = 1 + rng.below(3);
    for (std::size_t e = 0; e < edits; ++e) {
        if (s.empty()) {
            s.push_back(static_cast<char>(rng.below(256)));
            continue;
        }
        const std::size_t pos = rng.below(s.size());
        switch (rng.below(4)) {
            case 0: s.erase(pos, 1 + rng.below(4)); break;
            case 1: s.insert(pos, s.substr(pos, 1 + rng.below(6))); break;
            case 2: s[pos] = static_cast<char>(rng.below(256)); break;
            default: s.insert(pos, 1, "',\"\r\n;=-*"[rng.below(10)]); break;
        }
    }
    return s;
}

}  // namespace

// ---------------------------------------------------------- Robustness ----

TEST(random_token_soup_never_crashes) {
    Rng rng{1};
    Engine e = sampleEngine();
    e.createIndex("users", "city");
    for (int i = 0; i < scaled(20000); ++i) {
        const std::string sql = randomSql(rng);
        mustSucceedOrThrowError([&] { (void)e.run(sql); }, sql);
    }
}

TEST(mutated_valid_queries_never_crash) {
    const std::vector<std::string> seeds{
        "SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10;",
        "EXPLAIN SELECT * FROM users WHERE id = 42",
        "SELECT name FROM users WHERE name = 'O''Brien'",
        "SELECT * FROM users WHERE age != -5 ORDER BY city LIMIT 0",
    };
    Rng rng{2};
    Engine e = sampleEngine();
    e.createIndex("users", "id");
    for (int i = 0; i < scaled(20000); ++i) {
        const std::string sql = mutate(rng, rng.pick(seeds));
        mustSucceedOrThrowError([&] { (void)e.run(sql); }, sql);
    }
}

TEST(error_positions_stay_inside_the_query) {
    Rng rng{3};
    Engine e = sampleEngine();
    for (int i = 0; i < scaled(5000); ++i) {
        const std::string sql = randomSql(rng);
        try {
            (void)e.run(sql);
        } catch (const Error& err) {
            if (err.position() && *err.position() > sql.size()) {
                throw testfw::Failure("error position past end of input for: " + sql);
            }
        }
    }
}

TEST(mutated_csv_never_crashes) {
    const std::string base =
        "id,name,age,city\n1,Alice,34,Columbus\n2,\"Bob, Jr\",21,Austin\r\n"
        "3,\"say \"\"hi\"\"\",45,\"multi\nline\"\n4,,29,\n";
    const std::vector<Column> schema = parseColumnSpec("id:INT64,name:TEXT,age:INT64,city:TEXT");
    Rng rng{4};
    for (int i = 0; i < scaled(20000); ++i) {
        const std::string csv = mutate(rng, base);
        mustSucceedOrThrowError(
            [&] {
                Table t = tableFromCsv("t", csv, schema);
                // Anything that loads must have fully typed rows.
                for (const auto& row : t.rows()) {
                    CHECK_EQ(row.size(), schema.size());
                    for (std::size_t c = 0; c < row.size(); ++c) CHECK(typeOf(row[c]) == schema[c].type);
                }
            },
            csv);
    }
}

TEST(mutated_schema_json_never_crashes) {
    const std::string base =
        R"({"table": "users", "columns": [{"name": "id", "type": "INT64"}, {"name": "city", "type": "TEXT"}]})";
    Rng rng{5};
    for (int i = 0; i < scaled(20000); ++i) {
        const std::string json = mutate(rng, base);
        mustSucceedOrThrowError([&] { (void)parseSchemaJson(json); }, json);
    }
}

TEST(deeply_nested_json_is_rejected_not_overflowed) {
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    CHECK_THROWS_WITH(parseSchemaJson(deep), "nesting too deep");
}

TEST(very_long_query_is_handled) {
    Engine e = sampleEngine();
    std::string sql = "SELECT id FROM users WHERE age >= 0";
    for (int i = 0; i < 2000; ++i) sql += " AND age < 1000";
    CHECK_EQ(e.query(sql).rows.size(), std::size_t{20});
    std::string longString = "SELECT id FROM users WHERE name = '" + std::string(1 << 20, 'x') + "'";
    CHECK_EQ(e.query(longString).rows.size(), std::size_t{0});
}

// -------------------------------------------------------- Differential ----

namespace {

struct RefPredicate {
    std::size_t column;
    CompareOp op;
    Value literal;
};

bool refMatches(const Value& v, CompareOp op, const Value& lit) {
    // Written independently of compareValues(): compare via std::variant's
    // own operators, which order same-type alternatives naturally.
    switch (op) {
        case CompareOp::Eq: return v == lit;
        case CompareOp::Ne: return v != lit;
        case CompareOp::Lt: return v < lit;
        case CompareOp::Le: return v <= lit;
        case CompareOp::Gt: return v > lit;
        case CompareOp::Ge: return v >= lit;
    }
    return false;
}

// Naive reference: filter in row order, stable sort, truncate, project.
std::vector<Row> reference(const Table& t, const std::vector<std::size_t>& proj,
                           const std::vector<RefPredicate>& preds,
                           std::optional<std::size_t> orderCol, bool desc,
                           std::optional<std::uint64_t> limit) {
    std::vector<const Row*> kept;
    for (const auto& row : t.rows()) {
        bool ok = true;
        for (const auto& p : preds) ok = ok && refMatches(row[p.column], p.op, p.literal);
        if (ok) kept.push_back(&row);
    }
    if (orderCol) {
        const std::size_t c = *orderCol;
        std::stable_sort(kept.begin(), kept.end(), [&](const Row* a, const Row* b) {
            return desc ? (*b)[c] < (*a)[c] : (*a)[c] < (*b)[c];
        });
    }
    if (limit && *limit < kept.size()) kept.resize(static_cast<std::size_t>(*limit));
    std::vector<Row> out;
    for (const Row* r : kept) {
        Row o;
        for (std::size_t c : proj) o.push_back((*r)[c]);
        out.push_back(std::move(o));
    }
    return out;
}

std::string sqlLiteral(const Value& v) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    std::string s = "'";
    for (char c : std::get<std::string>(v)) s += (c == '\'') ? std::string("''") : std::string(1, c);
    return s + "'";
}

const char* opText(CompareOp op) {
    switch (op) {
        case CompareOp::Eq: return "=";
        case CompareOp::Ne: return "!=";
        case CompareOp::Lt: return "<";
        case CompareOp::Le: return "<=";
        case CompareOp::Gt: return ">";
        case CompareOp::Ge: return ">=";
    }
    return "=";
}

void runDifferential(Engine& e, std::uint64_t seed, int queries) {
    const Table& t = *e.catalog().find("users");
    const auto& cols = t.columns();
    Rng rng{seed};
    const std::vector<CompareOp> ops{CompareOp::Eq, CompareOp::Ne, CompareOp::Lt,
                                     CompareOp::Le, CompareOp::Gt, CompareOp::Ge};
    for (int q = 0; q < queries; ++q) {
        // Projection: '*' or a random non-empty subset in random order.
        std::vector<std::size_t> proj;
        std::string sql = "SELECT ";
        if (rng.chance(4)) {
            for (std::size_t c = 0; c < cols.size(); ++c) proj.push_back(c);
            sql += "*";
        } else {
            std::vector<std::size_t> all{0, 1, 2, 3};
            for (std::size_t i = all.size(); i > 1; --i) std::swap(all[i - 1], all[rng.below(i)]);
            proj.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(1 + rng.below(4)));
            for (std::size_t i = 0; i < proj.size(); ++i) sql += (i ? ", " : "") + cols[proj[i]].name;
        }
        sql += " FROM users";

        // Predicates: literals drawn from real cell values (so equality hits)
        // or random values (so misses and range edges happen too).
        std::vector<RefPredicate> preds;
        const std::size_t npred = rng.below(4);
        for (std::size_t i = 0; i < npred; ++i) {
            const std::size_t c = rng.below(cols.size());
            Value lit;
            if (!rng.chance(4) && t.rowCount() > 0) {
                lit = t.rows()[rng.below(t.rowCount())][c];
            } else if (cols[c].type == ColumnType::Int64) {
                lit = static_cast<std::int64_t>(rng.below(200)) - 50;
            } else {
                lit = std::string(rng.chance(2) ? "M" : "user_5");
            }
            const CompareOp op = rng.chance(2) ? CompareOp::Eq : rng.pick(ops);
            preds.push_back({c, op, lit});
            sql += std::string(i ? " AND " : " WHERE ") + cols[c].name + " " + opText(op) + " " +
                   sqlLiteral(lit);
        }

        std::optional<std::size_t> orderCol;
        bool desc = false;
        if (rng.chance(2)) {
            orderCol = rng.below(cols.size());
            desc = rng.chance(2);
            sql += " ORDER BY " + cols[*orderCol].name + (desc ? " DESC" : (rng.chance(2) ? " ASC" : ""));
        }
        std::optional<std::uint64_t> limit;
        if (rng.chance(2)) {
            limit = rng.below(30);
            sql += " LIMIT " + std::to_string(*limit);
        }

        const std::vector<Row> expected = reference(t, proj, preds, orderCol, desc, limit);
        const ResultSet got = e.query(sql);
        if (got.rows != expected) {
            throw testfw::Failure("result mismatch (" + std::to_string(got.rows.size()) + " vs " +
                                  std::to_string(expected.size()) + " rows) for: " + sql);
        }
    }
}

}  // namespace

TEST(differential_sample_data_no_index) {
    Engine e = sampleEngine();
    runDifferential(e, 10, scaled(3000));
}

TEST(differential_sample_data_all_indexed) {
    Engine e = sampleEngine();
    for (const char* c : {"id", "name", "age", "city"}) e.createIndex("users", c);
    runDifferential(e, 10, scaled(3000));  // same queries as the unindexed run
}

TEST(differential_generated_data_with_indexes) {
    Engine e;
    e.addTable(generateUsers("users", 3000, 99));
    runDifferential(e, 11, scaled(300));
    e.createIndex("users", "age");
    e.createIndex("users", "city");
    runDifferential(e, 11, scaled(300));
}

MINISQL_TEST_MAIN()
