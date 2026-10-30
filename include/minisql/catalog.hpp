#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "minisql/value.hpp"

namespace minisql {

// Single-column hash index: key value -> ascending list of row IDs.
// Row IDs are positions into Table::rows(); rows are immutable after load,
// so the IDs can never dangle.
class HashIndex {
public:
    HashIndex(std::size_t column, const std::vector<Row>& rows);

    std::size_t column() const noexcept { return column_; }
    // Returns the matching row IDs (ascending) or an empty list.
    const std::vector<std::size_t>& lookup(const Value& key) const;
    std::size_t distinctKeys() const noexcept { return map_.size(); }

private:
    std::size_t column_;
    std::unordered_map<Value, std::vector<std::size_t>> map_;
};

class Table {
public:
    Table(std::string name, std::vector<Column> columns, std::vector<Row> rows);

    const std::string& name() const noexcept { return name_; }
    const std::vector<Column>& columns() const noexcept { return columns_; }
    const std::vector<Row>& rows() const noexcept { return rows_; }
    std::size_t rowCount() const noexcept { return rows_.size(); }

    std::optional<std::size_t> columnIndex(const std::string& name) const;

    // Builds (or rebuilds) a hash index on `column`.
    void createIndex(const std::string& column);
    const HashIndex* indexOn(std::size_t column) const;
    std::vector<std::string> indexedColumns() const;

private:
    std::string name_;
    std::vector<Column> columns_;
    std::vector<Row> rows_;
    std::unordered_map<std::size_t, HashIndex> indexes_;
};

class Catalog {
public:
    // Throws on duplicate table name.
    Table& addTable(Table table);
    const Table* find(const std::string& name) const;
    Table* find(const std::string& name);
    // Sorted alphabetically for deterministic output.
    std::vector<std::string> tableNames() const;

private:
    std::unordered_map<std::string, Table> tables_;
};

}  // namespace minisql
