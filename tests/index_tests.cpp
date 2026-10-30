// Index correctness: indexed execution must return exactly the same rows
// in exactly the same order as a full scan.

#include <string>
#include <vector>

#include "minisql/csv.hpp"
#include "minisql/datagen.hpp"
#include "minisql/engine.hpp"
#include "test_framework.hpp"

using namespace minisql;

namespace {

Engine sampleEngine() {
    Engine e;
    const std::string dir = MINISQL_SOURCE_DIR "/data/";
    e.loadCsv("users", dir + "users.csv", loadSchemaFile(dir + "users.schema.json"));
    return e;
}

// Runs every query without the index, then with it, and asserts parity
// and that the planner actually switched to an index lookup when expected.
void checkParity(Engine& e, const std::string& column, const std::vector<std::string>& queries) {
    std::vector<ResultSet> before;
    for (const auto& q : queries) {
        CHECK(e.planFor(q).access == AccessKind::FullScan);
        before.push_back(e.query(q));
    }
    e.createIndex("users", column);
    for (std::size_t i = 0; i < queries.size(); ++i) {
        ResultSet after = e.query(queries[i]);
        CHECK_EQ(after.rows.size(), before[i].rows.size());
        CHECK(after.rows == before[i].rows);
        CHECK(after.columns.size() == before[i].columns.size());
    }
}

}  // namespace

TEST(hash_index_lookup_returns_ascending_row_ids) {
    Engine e = sampleEngine();
    e.createIndex("users", "city");
    const Table* t = e.catalog().find("users");
    const HashIndex* idx = t->indexOn(*t->columnIndex("city"));
    CHECK(idx != nullptr);
    const auto& ids = idx->lookup(Value{std::string("Columbus")});
    CHECK_EQ(ids.size(), std::size_t{8});
    for (std::size_t i = 1; i < ids.size(); ++i) CHECK(ids[i - 1] < ids[i]);
    CHECK(idx->lookup(Value{std::string("Nowhere")}).empty());
    CHECK_EQ(idx->distinctKeys(), std::size_t{6});
}

TEST(parity_unique_key) {
    Engine e = sampleEngine();
    checkParity(e, "id", {"SELECT * FROM users WHERE id = 12",
                          "SELECT name FROM users WHERE id = 1",
                          "SELECT * FROM users WHERE id = 20 AND age = 55",
                          "SELECT * FROM users WHERE id = 20 AND age = 54"});
}

TEST(parity_duplicate_keys_with_sort_and_limit) {
    Engine e = sampleEngine();
    checkParity(e, "city", {"SELECT * FROM users WHERE city = 'Columbus'",
                            "SELECT id FROM users WHERE city = 'Columbus' ORDER BY age DESC",
                            "SELECT id FROM users WHERE city = 'Columbus' ORDER BY age LIMIT 3",
                            "SELECT id FROM users WHERE city = 'Austin' LIMIT 0",
                            "SELECT name, age FROM users WHERE city = 'Boston' ORDER BY name"});
}

TEST(parity_no_match_key) {
    Engine e = sampleEngine();
    checkParity(e, "city", {"SELECT * FROM users WHERE city = 'Nowhere'",
                            "SELECT * FROM users WHERE city = ''"});
    CHECK_EQ(e.query("SELECT * FROM users WHERE city = 'Nowhere'").rows.size(), std::size_t{0});
}

TEST(parity_multiple_predicates) {
    Engine e = sampleEngine();
    checkParity(e, "age", {"SELECT id FROM users WHERE city = 'Columbus' AND age = 34",
                           "SELECT id FROM users WHERE age = 34 AND city != 'Columbus'",
                           "SELECT id FROM users WHERE age = 21 AND id > 2 AND id < 13"});
    CHECK_EQ(e.query("SELECT id FROM users WHERE city = 'Columbus' AND age = 34").rows.size(),
             std::size_t{3});
}

TEST(parity_on_generated_data) {
    Engine e;
    e.addTable(generateUsers("users", 5000, 7));
    checkParity(e, "city", {"SELECT * FROM users WHERE city = 'Denver'",
                            "SELECT id FROM users WHERE city = 'Miami' AND age >= 60 ORDER BY age DESC LIMIT 25"});
    checkParity(e, "id", {"SELECT * FROM users WHERE id = 4242",
                          "SELECT * FROM users WHERE id = 99999"});
}

TEST(planner_uses_index_only_for_equality) {
    Engine e = sampleEngine();
    e.createIndex("users", "age");
    CHECK(e.planFor("SELECT * FROM users WHERE age = 34").access == AccessKind::IndexLookup);
    CHECK(e.planFor("SELECT * FROM users WHERE age > 34").access == AccessKind::FullScan);
    CHECK(e.planFor("SELECT * FROM users WHERE age != 34").access == AccessKind::FullScan);
    CHECK(e.planFor("SELECT * FROM users WHERE city = 'Austin'").access == AccessKind::FullScan);
    CHECK(e.planFor("SELECT * FROM users").access == AccessKind::FullScan);
}

TEST(planner_picks_first_indexed_equality_in_where_order) {
    Engine e = sampleEngine();
    e.createIndex("users", "age");
    e.createIndex("users", "city");
    PhysicalPlan p1 = e.planFor("SELECT * FROM users WHERE city = 'Austin' AND age = 34");
    CHECK(p1.access == AccessKind::IndexLookup);
    CHECK_EQ(p1.table->columns()[p1.indexColumn].name, std::string("city"));
    CHECK_EQ(p1.residual.size(), std::size_t{1});
    PhysicalPlan p2 = e.planFor("SELECT * FROM users WHERE age > 1 AND age = 34 AND city = 'Austin'");
    CHECK_EQ(p2.table->columns()[p2.indexColumn].name, std::string("age"));
    CHECK_EQ(p2.residual.size(), std::size_t{2});
}

TEST(explain_shows_index_lookup_and_residual) {
    Engine e = sampleEngine();
    const std::string sql = "SELECT * FROM users WHERE id = 42";
    CHECK(e.explain(sql).find("FullScan table=users") != std::string::npos);
    e.createIndex("users", "id");
    const std::string plan = e.explain("EXPLAIN " + sql);
    CHECK(plan.find("IndexLookup table=users index=hash(id) key=42") != std::string::npos);
    CHECK(plan.find("Filter none") != std::string::npos);

    e.createIndex("users", "city");
    const std::string plan2 =
        e.explain("SELECT id FROM users WHERE age >= 21 AND city = 'O''Neil' ORDER BY age DESC LIMIT 10");
    CHECK(plan2.find("index=hash(city) key='O''Neil'") != std::string::npos);
    CHECK(plan2.find("Filter age >= 21") != std::string::npos);
    CHECK(plan2.find("Sort age DESC") != std::string::npos);
}

TEST(rebuilding_index_is_idempotent) {
    Engine e = sampleEngine();
    e.createIndex("users", "city");
    e.createIndex("users", "city");
    CHECK_EQ(e.query("SELECT id FROM users WHERE city = 'Columbus'").rows.size(), std::size_t{8});
    CHECK_EQ(e.catalog().find("users")->indexedColumns().size(), std::size_t{1});
}

TEST(index_lookup_examines_only_matching_rows) {
    Engine e = sampleEngine();
    e.createIndex("users", "city");
    CHECK_EQ(e.query("SELECT id FROM users WHERE city = 'Denver'").rowsExamined, std::size_t{2});
    CHECK_EQ(e.query("SELECT id FROM users WHERE city = 'Columbus' LIMIT 3").rowsExamined,
             std::size_t{3});
    CHECK_EQ(e.query("SELECT id FROM users WHERE city = 'Nowhere'").rowsExamined, std::size_t{0});
}

TEST(index_errors) {
    Engine e = sampleEngine();
    CHECK_THROWS_WITH(e.createIndex("nope", "id"), "unknown table 'nope'");
    CHECK_THROWS_WITH(e.createIndex("users", "salary"), "unknown column 'salary'");
}

MINISQL_TEST_MAIN()
