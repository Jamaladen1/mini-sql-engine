#include "minisql/validator.hpp"

#include <string>
#include <unordered_set>

#include "minisql/error.hpp"

namespace minisql {

namespace {

std::size_t resolveColumn(const Table& table, const Identifier& id) {
    auto idx = table.columnIndex(id.name);
    if (!idx) {
        throw Error(ErrorKind::Semantic,
                    "unknown column '" + id.name + "' in table '" + table.name() + "'",
                    id.position);
    }
    return *idx;
}

}  // namespace

BoundQuery validate(const SelectStmt& stmt, const Catalog& catalog) {
    BoundQuery q;
    q.table = catalog.find(stmt.table.name);
    if (!q.table) {
        throw Error(ErrorKind::Semantic, "unknown table '" + stmt.table.name + "'",
                    stmt.table.position);
    }
    const Table& table = *q.table;

    if (stmt.selectStar) {
        for (std::size_t i = 0; i < table.columns().size(); ++i) q.projection.push_back(i);
    } else {
        std::unordered_set<std::size_t> seen;
        for (const auto& id : stmt.projection) {
            std::size_t col = resolveColumn(table, id);
            if (!seen.insert(col).second) {
                throw Error(ErrorKind::Semantic,
                            "column '" + id.name + "' appears more than once in SELECT list",
                            id.position);
            }
            q.projection.push_back(col);
        }
    }

    for (const auto& p : stmt.where) {
        std::size_t col = resolveColumn(table, p.column);
        const ColumnType colType = table.columns()[col].type;
        const ColumnType litType = typeOf(p.literal);
        if (colType != litType) {
            throw Error(ErrorKind::Semantic,
                        "type mismatch: column '" + p.column.name + "' is " +
                            std::string(typeName(colType)) + " but literal is " +
                            std::string(typeName(litType)) + " (no implicit coercion)",
                        p.literalPosition);
        }
        q.predicates.push_back({col, p.op, p.literal});
    }

    if (stmt.orderBy) {
        q.orderColumn = resolveColumn(table, stmt.orderBy->column);
        q.descending = stmt.orderBy->descending;
    }
    q.limit = stmt.limit;
    return q;
}

}  // namespace minisql
