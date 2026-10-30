#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "minisql/catalog.hpp"
#include "minisql/value.hpp"

namespace minisql {

struct CsvRecord {
    std::vector<std::string> fields;
    std::size_t line;  // 1-based line where the record starts
};

// RFC 4180-style CSV: comma separator, double-quoted fields, "" escapes a
// quote, LF or CRLF line endings, quoted fields may contain commas and
// newlines. A trailing newline at end of input does not create a record.
std::vector<CsvRecord> parseCsv(std::string_view text);

// Schema file format (JSON):
//   {"columns": [{"name": "id", "type": "INT64"}, {"name": "city", "type": "TEXT"}]}
std::vector<Column> parseSchemaJson(std::string_view json);
std::vector<Column> loadSchemaFile(const std::string& path);

// Inline schema: "id:INT64,name:TEXT".
std::vector<Column> parseColumnSpec(std::string_view spec);

// The first CSV record is the header and must list exactly the schema's
// column names in the same order. Every data cell is converted to the
// declared type; empty INT64 cells are errors (no NULL support).
Table tableFromCsv(const std::string& tableName, std::string_view csvText,
                   const std::vector<Column>& schema);
Table loadCsvFile(const std::string& tableName, const std::string& path,
                  const std::vector<Column>& schema);

std::string readFile(const std::string& path);

}  // namespace minisql
