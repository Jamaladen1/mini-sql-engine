// Golden-result tests over data/users.csv (20 rows). Expected row IDs were
// worked out by hand from the CSV; see the table in the file for reference.

#include <string>
#include <vector>

#include "minisql/csv.hpp"
#include "minisql/engine.hpp"
#include "test_framework.hpp"

using namespace minisql;

namespace {

Engine makeEngine() {
    Engine e;
    const std::string dir = MINISQL_SOURCE_DIR "/data/";
    e.loadCsv("users", dir + "users.csv", loadSchemaFile(dir + "users.schema.json"));
    return e;
}

// First output column rendered as "a,b,c" for easy golden comparisons.
std::string col0(const ResultSet& r) {
    std::string out;
    for (std::size_t i = 0; i < r.rows.size(); ++i) {
        if (i) out += ",";
        out += valueToString(r.rows[i][0]);
    }
    return out;
}

std::string names(const ResultSet& r) {
    std::string out;
    for (const auto& c : r.columns) {
        if (!out.empty()) out += ",";
        out += c.name;
    }
    return out;
}

}  // namespace

TEST(select_star_returns_all_rows_in_load_order) {
    Engine e = makeEngine();
    ResultSet r = e.query("SELECT * FROM users");
    CHECK_EQ(r.rows.size(), std::size_t{20});
    CHECK_EQ(names(r), std::string("id,name,age,city"));
    CHECK_EQ(col0(r), std::string("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20"));
}

TEST(projection_order_follows_select_list) {
    Engine e = makeEngine();
    ResultSet r = e.query("SELECT city, id FROM users LIMIT 2");
    CHECK_EQ(names(r), std::string("city,id"));
    CHECK_EQ(col0(r), std::string("Columbus,Austin"));
    CHECK(std::get<std::int64_t>(r.rows[1][1]) == 2);
}

TEST(int_comparison_operators) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE age = 34")), std::string("1,7,10,19"));
    CHECK_EQ(e.query("SELECT id FROM users WHERE age != 34").rows.size(), std::size_t{16});
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE age < 34")),
             std::string("2,4,5,8,11,13,16,18"));
    CHECK_EQ(e.query("SELECT id FROM users WHERE age <= 34").rows.size(), std::size_t{12});
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE age > 34")),
             std::string("3,6,9,12,14,15,17,20"));
    CHECK_EQ(e.query("SELECT id FROM users WHERE age >= 34").rows.size(), std::size_t{12});
}

TEST(text_comparison_operators) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE city < 'Boston'")), std::string("2,7,15"));
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE city >= 'Seattle'")), std::string("14,18"));
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE name = 'O''Brien'")), std::string("12"));
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE name = 'Zoe \"Z\" Lee'")), std::string("18"));
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE name = 'Carol, PhD'")), std::string("3"));
}

TEST(and_predicates) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE city = 'Columbus' AND age >= 21 AND age < 40")),
             std::string("1,5,10,13,19"));
    CHECK_EQ(e.query("SELECT id FROM users WHERE age > 100 AND city = 'Columbus'").rows.size(),
             std::size_t{0});
}

TEST(design_doc_example_query) {
    Engine e = makeEngine();
    ResultSet r = e.query(
        "SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' "
        "ORDER BY age DESC LIMIT 10;");
    CHECK_EQ(names(r), std::string("id,name"));
    CHECK_EQ(col0(r), std::string("3,1,10,19,5,13"));
}

TEST(order_by_asc_with_ties_broken_by_row_id) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT id FROM users ORDER BY age LIMIT 8")),
             std::string("16,8,2,4,13,11,5,18"));
    CHECK_EQ(col0(e.query("SELECT id FROM users ORDER BY age ASC LIMIT 8")),
             std::string("16,8,2,4,13,11,5,18"));
}

TEST(order_by_desc_with_ties_broken_by_row_id) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT id FROM users ORDER BY age DESC LIMIT 6")),
             std::string("17,9,20,6,3,15"));
    // All four 34-year-olds tie: row id order is kept even for DESC.
    CHECK_EQ(col0(e.query("SELECT id FROM users WHERE age = 34 ORDER BY age DESC")),
             std::string("1,7,10,19"));
}

TEST(order_by_text_column) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT city FROM users ORDER BY city LIMIT 4")),
             std::string("Austin,Austin,Austin,Boston"));
}

TEST(order_by_column_not_in_projection) {
    Engine e = makeEngine();
    CHECK_EQ(col0(e.query("SELECT name FROM users ORDER BY age DESC LIMIT 2")),
             std::string("Walter,Ivan"));
}

TEST(full_sort_matches_partial_sort_prefix) {
    Engine e = makeEngine();
    ResultSet all = e.query("SELECT id FROM users ORDER BY city DESC");
    for (std::uint64_t k = 0; k <= 20; ++k) {
        ResultSet part = e.query("SELECT id FROM users ORDER BY city DESC LIMIT " + std::to_string(k));
        CHECK_EQ(part.rows.size(), static_cast<std::size_t>(k));
        for (std::size_t i = 0; i < part.rows.size(); ++i) CHECK(part.rows[i] == all.rows[i]);
    }
}

TEST(limit_zero_and_large_limit) {
    Engine e = makeEngine();
    ResultSet zero = e.query("SELECT id, name FROM users LIMIT 0");
    CHECK_EQ(zero.rows.size(), std::size_t{0});
    CHECK_EQ(names(zero), std::string("id,name"));
    CHECK_EQ(e.query("SELECT id FROM users LIMIT 1000").rows.size(), std::size_t{20});
    CHECK_EQ(col0(e.query("SELECT id FROM users LIMIT 3")), std::string("1,2,3"));
}

TEST(semantic_errors) {
    Engine e = makeEngine();
    CHECK_THROWS_WITH(e.query("SELECT * FROM nope"), "unknown table 'nope'");
    CHECK_THROWS_WITH(e.query("SELECT salary FROM users"), "unknown column 'salary'");
    CHECK_THROWS_WITH(e.query("SELECT ID FROM users"), "unknown column 'ID'");  // case-sensitive
    CHECK_THROWS_WITH(e.query("SELECT * FROM users WHERE x = 1"), "unknown column 'x'");
    CHECK_THROWS_WITH(e.query("SELECT * FROM users ORDER BY x"), "unknown column 'x'");
    CHECK_THROWS_WITH(e.query("SELECT id, id FROM users"), "appears more than once");
}

TEST(type_mismatch_is_rejected) {
    Engine e = makeEngine();
    CHECK_THROWS_WITH(e.query("SELECT * FROM users WHERE age = '34'"),
                      "column 'age' is INT64 but literal is TEXT");
    CHECK_THROWS_WITH(e.query("SELECT * FROM users WHERE city = 5"),
                      "column 'city' is TEXT but literal is INT64");
}

TEST(explain_does_not_execute_and_shows_plan) {
    Engine e = makeEngine();
    QueryOutcome out = e.run("EXPLAIN SELECT id FROM users WHERE age > 30 ORDER BY age DESC LIMIT 3");
    CHECK(out.explain);
    CHECK(out.result.rows.empty());
    CHECK(out.planText.find("FullScan table=users rows=20") != std::string::npos);
    CHECK(out.planText.find("Filter age > 30") != std::string::npos);
    CHECK(out.planText.find("Sort age DESC") != std::string::npos);
    CHECK(out.planText.find("Limit 3") != std::string::npos);
    CHECK(out.planText.find("Project [id]") != std::string::npos);
}

TEST(render_result_table) {
    Engine e = makeEngine();
    std::string s = renderResult(e.query("SELECT id, name FROM users WHERE id = 3"));
    CHECK(s.find("| id | name       |") != std::string::npos);
    CHECK(s.find("|  3 | Carol, PhD |") != std::string::npos);
    CHECK(s.find("(1 row)") != std::string::npos);
}

TEST(duplicate_table_load_rejected) {
    Engine e = makeEngine();
    const std::string dir = MINISQL_SOURCE_DIR "/data/";
    CHECK_THROWS_WITH(e.loadCsv("users", dir + "users.csv", loadSchemaFile(dir + "schema.json")),
                      "already exists");
}

MINISQL_TEST_MAIN()
