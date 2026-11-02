#include "minisql/csv.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>

#include "minisql/error.hpp"
#include "minisql/json.hpp"

namespace minisql {

// ---------------------------------------------------------------- CSV ----

std::vector<CsvRecord> parseCsv(std::string_view text) {
    std::vector<CsvRecord> records;
    std::size_t i = 0;
    std::size_t line = 1;
    const std::size_t n = text.size();

    while (i < n) {
        CsvRecord rec;
        rec.line = line;
        bool endOfRecord = false;
        while (!endOfRecord) {
            std::string field;
            if (i < n && text[i] == '"') {
                const std::size_t quoteLine = line;
                ++i;
                bool closed = false;
                while (i < n) {
                    char c = text[i];
                    if (c == '"') {
                        if (i + 1 < n && text[i + 1] == '"') {
                            field.push_back('"');
                            i += 2;
                        } else {
                            ++i;
                            closed = true;
                            break;
                        }
                    } else {
                        if (c == '\n') ++line;
                        field.push_back(c);
                        ++i;
                    }
                }
                if (!closed) {
                    throw Error(ErrorKind::Csv, "unterminated quoted field", std::nullopt,
                                quoteLine);
                }
                // After a closing quote only a separator or line end is legal.
                if (i < n && text[i] != ',' && text[i] != '\n' && text[i] != '\r') {
                    throw Error(ErrorKind::Csv, "unexpected character after closing quote",
                                std::nullopt, line);
                }
            } else {
                while (i < n && text[i] != ',' && text[i] != '\n' && text[i] != '\r') {
                    if (text[i] == '"') {
                        throw Error(ErrorKind::Csv, "quote inside unquoted field",
                                    std::nullopt, line);
                    }
                    field.push_back(text[i]);
                    ++i;
                }
            }
            rec.fields.push_back(std::move(field));

            if (i >= n) {
                endOfRecord = true;
            } else if (text[i] == ',') {
                ++i;
            } else if (text[i] == '\r') {
                if (i + 1 < n && text[i + 1] == '\n') {
                    i += 2;
                } else {
                    throw Error(ErrorKind::Csv, "bare carriage return", std::nullopt, line);
                }
                ++line;
                endOfRecord = true;
            } else {  // '\n'
                ++i;
                ++line;
                endOfRecord = true;
            }
        }
        records.push_back(std::move(rec));
    }
    return records;
}

// ------------------------------------------------------------- Schema ----

namespace {

bool isIdentifier(std::string_view s) {
    if (s.empty()) return false;
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!alpha(s[0])) return false;
    for (char c : s) {
        if (!alpha(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

void validateColumns(const std::vector<Column>& cols) {
    if (cols.empty()) throw Error(ErrorKind::Schema, "schema declares no columns");
    std::unordered_set<std::string> seen;
    for (const auto& c : cols) {
        if (!isIdentifier(c.name)) {
            throw Error(ErrorKind::Schema, "invalid column name '" + c.name + "'");
        }
        if (!seen.insert(c.name).second) {
            throw Error(ErrorKind::Schema, "duplicate column name '" + c.name + "'");
        }
    }
}

}  // namespace

std::vector<Column> parseSchemaJson(std::string_view json) {
    const Json doc = parseJson(json);
    if (doc.kind != Json::Kind::Object) {
        throw Error(ErrorKind::Schema, "schema must be a JSON object");
    }
    const Json* cols = doc.get("columns");
    if (!cols || cols->kind != Json::Kind::Array) {
        throw Error(ErrorKind::Schema, "schema must contain a \"columns\" array");
    }
    std::vector<Column> out;
    for (const auto& c : cols->arr) {
        const Json* name = c.get("name");
        const Json* type = c.get("type");
        if (c.kind != Json::Kind::Object || !name || !type ||
            name->kind != Json::Kind::String || type->kind != Json::Kind::String) {
            throw Error(ErrorKind::Schema,
                        "each column needs string \"name\" and \"type\" fields");
        }
        auto t = parseTypeName(type->str);
        if (!t) {
            throw Error(ErrorKind::Schema, "unknown type '" + type->str + "' for column '" +
                                               name->str + "' (expected INT64 or TEXT)");
        }
        out.push_back({name->str, *t});
    }
    validateColumns(out);
    return out;
}

std::vector<Column> loadSchemaFile(const std::string& path) {
    return parseSchemaJson(readFile(path));
}

std::vector<Column> parseColumnSpec(std::string_view spec) {
    std::vector<Column> out;
    std::size_t start = 0;
    while (start <= spec.size()) {
        std::size_t end = spec.find(',', start);
        if (end == std::string_view::npos) end = spec.size();
        std::string_view item = spec.substr(start, end - start);
        std::size_t colon = item.find(':');
        if (colon == std::string_view::npos) {
            throw Error(ErrorKind::Schema,
                        "column spec '" + std::string(item) + "' must be name:TYPE");
        }
        auto t = parseTypeName(item.substr(colon + 1));
        if (!t) {
            throw Error(ErrorKind::Schema, "unknown type '" +
                                               std::string(item.substr(colon + 1)) +
                                               "' (expected INT64 or TEXT)");
        }
        out.push_back({std::string(item.substr(0, colon)), *t});
        start = end + 1;
    }
    validateColumns(out);
    return out;
}

// --------------------------------------------------------------- Load ----

Table tableFromCsv(const std::string& tableName, std::string_view csvText,
                   const std::vector<Column>& schema) {
    if (!isIdentifier(tableName)) {
        throw Error(ErrorKind::Catalog, "invalid table name '" + tableName + "'");
    }
    validateColumns(schema);
    std::vector<CsvRecord> records = parseCsv(csvText);
    if (records.empty()) throw Error(ErrorKind::Csv, "file is empty (header row required)");

    const CsvRecord& header = records.front();
    {
        std::unordered_set<std::string> seen;
        for (const auto& h : header.fields) {
            if (!seen.insert(h).second) {
                throw Error(ErrorKind::Csv, "duplicate header column '" + h + "'",
                            std::nullopt, header.line);
            }
        }
    }
    if (header.fields.size() != schema.size()) {
        throw Error(ErrorKind::Csv,
                    "header has " + std::to_string(header.fields.size()) +
                        " columns but schema declares " + std::to_string(schema.size()),
                    std::nullopt, header.line);
    }
    for (std::size_t c = 0; c < schema.size(); ++c) {
        if (header.fields[c] != schema[c].name) {
            throw Error(ErrorKind::Csv,
                        "header column " + std::to_string(c + 1) + " is '" +
                            header.fields[c] + "' but schema expects '" + schema[c].name + "'",
                        std::nullopt, header.line);
        }
    }

    std::vector<Row> rows;
    rows.reserve(records.size() - 1);
    for (std::size_t r = 1; r < records.size(); ++r) {
        const CsvRecord& rec = records[r];
        if (rec.fields.size() != schema.size()) {
            throw Error(ErrorKind::Csv,
                        "record has " + std::to_string(rec.fields.size()) +
                            " fields, expected " + std::to_string(schema.size()),
                        std::nullopt, rec.line);
        }
        Row row;
        row.reserve(schema.size());
        for (std::size_t c = 0; c < schema.size(); ++c) {
            const std::string& cell = rec.fields[c];
            if (schema[c].type == ColumnType::Int64) {
                if (cell.empty()) {
                    throw Error(ErrorKind::Csv,
                                "empty INT64 value in column '" + schema[c].name +
                                    "' (NULL is not supported)",
                                std::nullopt, rec.line);
                }
                auto v = parseInt64(cell);
                if (!v) {
                    throw Error(ErrorKind::Csv,
                                "invalid or out-of-range INT64 '" + cell + "' in column '" +
                                    schema[c].name + "'",
                                std::nullopt, rec.line);
                }
                row.emplace_back(*v);
            } else {
                row.emplace_back(cell);
            }
        }
        rows.push_back(std::move(row));
    }
    return Table(tableName, schema, std::move(rows));
}

Table loadCsvFile(const std::string& tableName, const std::string& path,
                  const std::vector<Column>& schema) {
    return tableFromCsv(tableName, readFile(path), schema);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error(ErrorKind::Io, "cannot open file '" + path + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) throw Error(ErrorKind::Io, "failed reading file '" + path + "'");
    return ss.str();
}

}  // namespace minisql
