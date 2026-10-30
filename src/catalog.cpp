#include "minisql/catalog.hpp"

#include <algorithm>
#include <unordered_set>

#include "minisql/error.hpp"

namespace minisql {

HashIndex::HashIndex(std::size_t column, const std::vector<Row>& rows) : column_(column) {
    for (std::size_t id = 0; id < rows.size(); ++id) {
        map_[rows[id].at(column)].push_back(id);
    }
}

const std::vector<std::size_t>& HashIndex::lookup(const Value& key) const {
    static const std::vector<std::size_t> empty;
    auto it = map_.find(key);
    return it == map_.end() ? empty : it->second;
}

Table::Table(std::string name, std::vector<Column> columns, std::vector<Row> rows)
    : name_(std::move(name)), columns_(std::move(columns)), rows_(std::move(rows)) {
    if (columns_.empty()) {
        throw Error(ErrorKind::Schema, "table '" + name_ + "' has no columns");
    }
    std::unordered_set<std::string> seen;
    for (const auto& c : columns_) {
        if (!seen.insert(c.name).second) {
            throw Error(ErrorKind::Schema, "duplicate column name '" + c.name + "'");
        }
    }
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (rows_[r].size() != columns_.size()) {
            throw Error(ErrorKind::Schema, "row " + std::to_string(r) + " has " +
                                               std::to_string(rows_[r].size()) +
                                               " values, expected " +
                                               std::to_string(columns_.size()));
        }
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            if (typeOf(rows_[r][c]) != columns_[c].type) {
                throw Error(ErrorKind::Schema, "row " + std::to_string(r) + " column '" +
                                                   columns_[c].name + "' has wrong type");
            }
        }
    }
}

std::optional<std::size_t> Table::columnIndex(const std::string& name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == name) return i;
    }
    return std::nullopt;
}

void Table::createIndex(const std::string& column) {
    auto idx = columnIndex(column);
    if (!idx) {
        throw Error(ErrorKind::Catalog,
                    "unknown column '" + column + "' in table '" + name_ + "'");
    }
    indexes_.erase(*idx);
    indexes_.emplace(*idx, HashIndex(*idx, rows_));
}

const HashIndex* Table::indexOn(std::size_t column) const {
    auto it = indexes_.find(column);
    return it == indexes_.end() ? nullptr : &it->second;
}

std::vector<std::string> Table::indexedColumns() const {
    std::vector<std::size_t> ids;
    for (const auto& [col, _] : indexes_) ids.push_back(col);
    std::sort(ids.begin(), ids.end());
    std::vector<std::string> out;
    for (auto id : ids) out.push_back(columns_[id].name);
    return out;
}

Table& Catalog::addTable(Table table) {
    const std::string name = table.name();
    if (tables_.count(name)) {
        throw Error(ErrorKind::Catalog, "table '" + name + "' already exists");
    }
    return tables_.emplace(name, std::move(table)).first->second;
}

const Table* Catalog::find(const std::string& name) const {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : &it->second;
}

Table* Catalog::find(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::tableNames() const {
    std::vector<std::string> out;
    for (const auto& [name, _] : tables_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace minisql
