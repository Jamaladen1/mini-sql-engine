#include "minisql/csv.hpp"
#include "test_framework.hpp"

using namespace minisql;

namespace {
const std::vector<Column> kSchema{{"id", ColumnType::Int64}, {"name", ColumnType::Text}};
}

TEST(csv_basic_records) {
    auto recs = parseCsv("a,b\n1,2\n3,4\n");
    CHECK_EQ(recs.size(), std::size_t{3});
    CHECK_EQ(recs[1].fields[0], std::string("1"));
    CHECK_EQ(recs[2].fields[1], std::string("4"));
    CHECK_EQ(recs[2].line, std::size_t{3});
}

TEST(csv_quoted_commas_and_escaped_quotes) {
    auto recs = parseCsv("a,b\n\"x, y\",\"say \"\"hi\"\"\"\n");
    CHECK_EQ(recs[1].fields[0], std::string("x, y"));
    CHECK_EQ(recs[1].fields[1], std::string("say \"hi\""));
}

TEST(csv_crlf_and_no_trailing_newline) {
    auto recs = parseCsv("a,b\r\n1,2\r\n3,4");
    CHECK_EQ(recs.size(), std::size_t{3});
    CHECK_EQ(recs[1].fields[1], std::string("2"));
    CHECK_EQ(recs[2].fields[1], std::string("4"));
}

TEST(csv_empty_strings) {
    auto recs = parseCsv("a,b,c\n,\"\",\n");
    CHECK_EQ(recs[1].fields.size(), std::size_t{3});
    CHECK_EQ(recs[1].fields[0], std::string(""));
    CHECK_EQ(recs[1].fields[1], std::string(""));
    CHECK_EQ(recs[1].fields[2], std::string(""));
}

TEST(csv_quoted_newline_tracks_lines) {
    auto recs = parseCsv("a\n\"multi\nline\"\nnext\n");
    CHECK_EQ(recs.size(), std::size_t{3});
    CHECK_EQ(recs[1].fields[0], std::string("multi\nline"));
    CHECK_EQ(recs[2].line, std::size_t{4});
}

TEST(csv_malformed_quotes) {
    CHECK_THROWS_WITH(parseCsv("a\n\"open\n"), "unterminated quoted field");
    CHECK_THROWS_WITH(parseCsv("a\n\"x\"y\n"), "unexpected character after closing quote");
    CHECK_THROWS_WITH(parseCsv("a\nab\"c\n"), "quote inside unquoted field");
    CHECK_THROWS_WITH(parseCsv("a\rb"), "bare carriage return");
}

TEST(table_load_typed_values) {
    Table t = tableFromCsv("t", "id,name\n1,alice\n-2,\"bob, jr\"\n3,\n", kSchema);
    CHECK_EQ(t.rowCount(), std::size_t{3});
    CHECK(std::get<std::int64_t>(t.rows()[1][0]) == -2);
    CHECK(std::get<std::string>(t.rows()[1][1]) == "bob, jr");
    CHECK(std::get<std::string>(t.rows()[2][1]).empty());
}

TEST(table_load_bad_integer) {
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\nabc,x\n", kSchema),
                      "(line 2): invalid or out-of-range INT64 'abc'");
}

TEST(table_load_integer_out_of_range) {
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\n99999999999999999999,x\n", kSchema),
                      "out-of-range INT64");
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\n 5,x\n", kSchema), "invalid or out-of-range");
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\n1.5,x\n", kSchema), "invalid or out-of-range");
}

TEST(table_load_empty_int_is_error) {
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\n,x\n", kSchema), "NULL is not supported");
}

TEST(table_load_duplicate_headers) {
    CHECK_THROWS_WITH(tableFromCsv("t", "id,id\n1,2\n", kSchema), "duplicate header column 'id'");
}

TEST(table_load_header_mismatch_and_field_count) {
    CHECK_THROWS_WITH(tableFromCsv("t", "id,nom\n1,x\n", kSchema), "schema expects 'name'");
    CHECK_THROWS_WITH(tableFromCsv("t", "id\n1\n", kSchema), "header has 1 columns");
    CHECK_THROWS_WITH(tableFromCsv("t", "id,name\n1,x,extra\n", kSchema),
                      "(line 2): record has 3 fields, expected 2");
    CHECK_THROWS_WITH(tableFromCsv("t", "", kSchema), "file is empty");
}

TEST(schema_json_parsing) {
    auto cols = parseSchemaJson(
        R"({"table": "users", "columns": [ {"name": "id", "type": "INT64"},
            {"name": "city", "type": "text"} ]})");
    CHECK_EQ(cols.size(), std::size_t{2});
    CHECK(cols[0].type == ColumnType::Int64);
    CHECK(cols[1].type == ColumnType::Text);
    CHECK_EQ(cols[1].name, std::string("city"));
}

TEST(schema_json_errors) {
    CHECK_THROWS_WITH(parseSchemaJson("[]"), "must be a JSON object");
    CHECK_THROWS_WITH(parseSchemaJson("{}"), "\"columns\" array");
    CHECK_THROWS_WITH(parseSchemaJson(R"({"columns":[{"name":"a","type":"FLOAT"}]})"),
                      "unknown type 'FLOAT'");
    CHECK_THROWS_WITH(parseSchemaJson(
                          R"({"columns":[{"name":"a","type":"TEXT"},{"name":"a","type":"TEXT"}]})"),
                      "duplicate column name 'a'");
    CHECK_THROWS_WITH(parseSchemaJson(R"({"columns":[{"name":"a",)"), "invalid JSON");
    CHECK_THROWS_WITH(parseSchemaJson(R"({"columns":[]})"), "no columns");
}

TEST(column_spec_parsing) {
    auto cols = parseColumnSpec("id:INT64,name:TEXT");
    CHECK_EQ(cols.size(), std::size_t{2});
    CHECK(cols[0].type == ColumnType::Int64);
    CHECK_THROWS_WITH(parseColumnSpec("id"), "must be name:TYPE");
    CHECK_THROWS_WITH(parseColumnSpec("id:BIGINT"), "unknown type");
    CHECK_THROWS_WITH(parseColumnSpec("1bad:TEXT"), "invalid column name");
}

TEST(sample_data_files_load) {
    const std::string dir = MINISQL_SOURCE_DIR "/data/";
    auto cols = loadSchemaFile(dir + "users.schema.json");
    Table t = loadCsvFile("users", dir + "users.csv", cols);
    CHECK(t.rowCount() > 0);
}

TEST(missing_file_is_io_error) {
    CHECK_THROWS_WITH(readFile("/nonexistent/file.csv"), "cannot open file");
}

MINISQL_TEST_MAIN()
