#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "minisql/ast.hpp"
#include "minisql/catalog.hpp"

namespace minisql {

struct BoundPredicate {
    std::size_t column;
    CompareOp op;
    Value literal;  // guaranteed to have the column's type
};

// A query whose names have been resolved against the catalog and whose
// literal types have been checked. `table` is non-owning and points into
// the Catalog, which must outlive the BoundQuery.
struct BoundQuery {
    const Table* table = nullptr;
    std::vector<std::size_t> projection;  // output column order
    std::vector<BoundPredicate> predicates;
    std::optional<std::size_t> orderColumn;
    bool descending = false;
    std::optional<std::uint64_t> limit;
};

// Semantic checks: table and columns exist, no duplicate projection
// columns, predicate literals have exactly the column's type (no implicit
// coercion), ORDER BY column exists. Throws minisql::Error (Semantic).
BoundQuery validate(const SelectStmt& stmt, const Catalog& catalog);

}  // namespace minisql
