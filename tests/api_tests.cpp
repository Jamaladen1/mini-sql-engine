// DemoApi tests: requests go straight to DemoApi::handle (no sockets) and
// responses are parsed back with the engine's own JSON reader.

#include <string>

#include "api.hpp"
#include "minisql/json.hpp"
#include "test_framework.hpp"

using namespace minisql;
using namespace minisql::demo;

namespace {

DemoApi makeApi() {
    std::map<std::string, StaticFile> files{
        {"/index.html", {"text/html; charset=utf-8", "<!doctype html><title>t</title>"}},
        {"/app.js", {"text/javascript; charset=utf-8", "console.log(1)"}},
    };
    return DemoApi(MINISQL_SOURCE_DIR "/data", std::move(files));
}

http::Request get(const std::string& path) {
    http::Request r;
    r.method = "GET";
    r.path = path;
    return r;
}

http::Request post(const std::string& path, const std::string& body,
                   const std::string& contentType = "application/json") {
    http::Request r;
    r.method = "POST";
    r.path = path;
    r.body = body;
    if (!contentType.empty()) r.headers["content-type"] = contentType;
    return r;
}

http::Request sqlPost(const std::string& path, const std::string& sql) {
    return post(path, "{\"sql\":" + jsonQuote(sql) + "}");
}

Json body(const http::Response& r) {
    CHECK_EQ(r.contentType, std::string("application/json"));
    return parseJson(r.body);
}

std::string errorKind(const http::Response& r) {
    const Json j = body(r);
    const Json* e = j.get("error");
    CHECK(e != nullptr);
    return e->get("kind")->str;
}

}  // namespace

TEST(health) {
    DemoApi api = makeApi();
    http::Response r = api.handle(get("/health"));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(body(r).get("status")->str, std::string("ok"));
}

TEST(wrong_method_is_405_with_allow) {
    DemoApi api = makeApi();
    http::Response r = api.handle(post("/health", ""));
    CHECK_EQ(r.status, 405);
    CHECK(r.headers.size() == 1 && r.headers[0].first == "Allow" && r.headers[0].second == "GET");
    CHECK_EQ(api.handle(get("/api/query")).status, 405);
    CHECK_EQ(api.handle(get("/api/load-sample")).status, 405);
}

TEST(tables_lists_sample_datasets_with_indexes) {
    DemoApi api = makeApi();
    const Json j = body(api.handle(get("/api/tables")));
    const auto& tables = j.get("tables")->arr;
    CHECK_EQ(tables.size(), std::size_t{3});
    CHECK_EQ(tables[0].get("name")->str, std::string("users"));
    CHECK_EQ(tables[0].get("rows")->str, std::string("20"));
    CHECK_EQ(tables[1].get("name")->str, std::string("users_100k"));
    CHECK_EQ(tables[1].get("rows")->str, std::string("100000"));
    CHECK_EQ(tables[2].get("name")->str, std::string("users_100k_indexed"));
    const auto& cols = tables[2].get("columns")->arr;
    CHECK_EQ(cols.size(), std::size_t{4});
    CHECK_EQ(cols[0].get("name")->str, std::string("id"));
    CHECK_EQ(cols[0].get("type")->str, std::string("INT64"));
    CHECK_EQ(cols[0].get("indexed")->str, std::string("true"));
    CHECK_EQ(cols[2].get("indexed")->str, std::string("false"));  // age
    CHECK_EQ(cols[3].get("indexed")->str, std::string("true"));   // city
    CHECK_EQ(tables[1].get("columns")->arr[0].get("indexed")->str, std::string("false"));
}

TEST(query_returns_columns_rows_plan_and_timing) {
    DemoApi api = makeApi();
    http::Response r = api.handle(sqlPost(
        "/api/query",
        "SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10"));
    CHECK_EQ(r.status, 200);
    const Json j = body(r);
    CHECK_EQ(j.get("explain")->str, std::string("false"));
    CHECK_EQ(j.get("access")->str, std::string("FullScan"));
    CHECK(j.get("plan")->str.find("Sort age DESC") != std::string::npos);
    CHECK(j.get("elapsedMs")->kind == Json::Kind::Number);
    CHECK_EQ(j.get("rowCount")->str, std::string("6"));
    CHECK_EQ(j.get("rowsExamined")->str, std::string("20"));
    CHECK_EQ(j.get("truncated")->str, std::string("false"));
    const auto& cols = j.get("columns")->arr;
    CHECK_EQ(cols[0].get("name")->str, std::string("id"));
    CHECK_EQ(cols[1].get("type")->str, std::string("TEXT"));
    const auto& rows = j.get("rows")->arr;
    CHECK_EQ(rows.size(), std::size_t{6});
    CHECK_EQ(rows[0].arr[0].str, std::string("3"));
    CHECK(rows[0].arr[0].kind == Json::Kind::Number);
    CHECK_EQ(rows[0].arr[1].str, std::string("Carol, PhD"));
}

TEST(text_values_are_escaped) {
    DemoApi api = makeApi();
    const Json j = body(api.handle(sqlPost("/api/query", "SELECT name FROM users WHERE id = 18")));
    CHECK_EQ(j.get("rows")->arr[0].arr[0].str, std::string("Zoe \"Z\" Lee"));
}

TEST(index_lookup_vs_full_scan) {
    DemoApi api = makeApi();
    const Json a = body(api.handle(sqlPost("/api/query", "SELECT * FROM users_100k WHERE id = 50007")));
    const Json b =
        body(api.handle(sqlPost("/api/query", "SELECT * FROM users_100k_indexed WHERE id = 50007")));
    CHECK_EQ(a.get("access")->str, std::string("FullScan"));
    CHECK_EQ(a.get("rowsExamined")->str, std::string("100000"));
    CHECK_EQ(b.get("access")->str, std::string("IndexLookup"));
    CHECK_EQ(b.get("rowsExamined")->str, std::string("1"));
    CHECK(a.get("rows")->arr[0].arr[3].str == b.get("rows")->arr[0].arr[3].str);
}

TEST(large_results_are_truncated_but_counted) {
    DemoApi api = makeApi();
    const Json j = body(api.handle(sqlPost("/api/query", "SELECT id FROM users_100k")));
    CHECK_EQ(j.get("rowCount")->str, std::string("100000"));
    CHECK_EQ(j.get("truncated")->str, std::string("true"));
    CHECK_EQ(j.get("rows")->arr.size(), std::size_t{500});
}

TEST(explain_endpoint_with_and_without_prefix) {
    DemoApi api = makeApi();
    for (const char* sql : {"SELECT * FROM users_100k_indexed WHERE city = 'Austin' AND age > 40",
                            "EXPLAIN SELECT * FROM users_100k_indexed WHERE city = 'Austin' AND age > 40"}) {
        const Json j = body(api.handle(sqlPost("/api/explain", sql)));
        CHECK_EQ(j.get("explain")->str, std::string("true"));
        CHECK_EQ(j.get("access")->str, std::string("IndexLookup"));
        CHECK(j.get("plan")->str.find("Filter age > 40") != std::string::npos);
        CHECK(j.get("rows") == nullptr);
        CHECK(j.get("elapsedMs") == nullptr);
    }
    // EXPLAIN through /api/query returns the plan and no rows.
    const Json q = body(api.handle(sqlPost("/api/query", "EXPLAIN SELECT * FROM users")));
    CHECK_EQ(q.get("explain")->str, std::string("true"));
    CHECK(q.get("rows") == nullptr);
}

TEST(sql_errors_have_kind_message_and_position) {
    DemoApi api = makeApi();
    http::Response r = api.handle(sqlPost("/api/query", "SELECT * FROM users WHERE age = 'x'"));
    CHECK_EQ(r.status, 400);
    const Json j = body(r);
    CHECK_EQ(j.get("error")->get("kind")->str, std::string("semantic"));
    CHECK_EQ(j.get("error")->get("position")->str, std::string("32"));
    CHECK(j.get("error")->get("message")->str.find("type mismatch") != std::string::npos);
    CHECK_EQ(errorKind(api.handle(sqlPost("/api/query", "SELECT FROM"))), std::string("syntax"));
    CHECK_EQ(errorKind(api.handle(sqlPost("/api/query", "SELECT * FROM nope"))), std::string("semantic"));
}

TEST(request_body_validation) {
    DemoApi api = makeApi();
    CHECK_EQ(api.handle(post("/api/query", "{\"sql\":\"SELECT * FROM users\"}", "")).status, 415);
    CHECK_EQ(api.handle(post("/api/query", "{\"sql\":\"SELECT * FROM users\"}", "text/plain")).status, 415);
    CHECK_EQ(api.handle(post("/api/query", "{\"sql\":\"SELECT * FROM users\"}",
                             "application/json; charset=utf-8"))
                 .status,
             200);
    CHECK_EQ(api.handle(post("/api/query", "not json")).status, 400);
    CHECK_EQ(api.handle(post("/api/query", "{\"query\":\"x\"}")).status, 400);
    CHECK_EQ(api.handle(post("/api/query", "{\"sql\":5}")).status, 400);
    CHECK_EQ(api.handle(post("/api/query", "[\"SELECT\"]")).status, 400);
    const std::string longSql = "SELECT * FROM users WHERE name = '" + std::string(5000, 'x') + "'";
    http::Response r = api.handle(sqlPost("/api/query", longSql));
    CHECK_EQ(r.status, 400);
    CHECK(body(r).get("error")->get("message")->str.find("longer than 4096") != std::string::npos);
}

TEST(load_sample_resets_and_returns_tables) {
    DemoApi api = makeApi();
    http::Response r = api.handle(post("/api/load-sample", "", ""));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(body(r).get("tables")->arr.size(), std::size_t{3});
    CHECK_EQ(api.engine().catalog().find("users")->rowCount(), std::size_t{20});
}

TEST(static_files_exact_match_only) {
    DemoApi api = makeApi();
    http::Response root = api.handle(get("/"));
    CHECK_EQ(root.status, 200);
    CHECK_EQ(root.contentType, std::string("text/html; charset=utf-8"));
    bool hasCsp = false;
    for (const auto& [k, v] : root.headers) hasCsp = hasCsp || (k == "Content-Security-Policy" && v.find("default-src 'self'") != std::string::npos);
    CHECK(hasCsp);
    CHECK_EQ(api.handle(get("/app.js")).status, 200);
    CHECK_EQ(api.handle(get("/../data/users.csv")).status, 404);
    CHECK_EQ(api.handle(get("/data/users.csv")).status, 404);
    CHECK_EQ(api.handle(get("/index.html/")).status, 404);
    CHECK_EQ(api.handle(post("/app.js", "")).status, 405);
    CHECK_EQ(errorKind(api.handle(get("/nope"))), std::string("http"));
}

MINISQL_TEST_MAIN()
