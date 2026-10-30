#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "minisql/validator.hpp"

namespace minisql {

enum class AccessKind { FullScan, IndexLookup };

// Physical plan, executed bottom-up:
//   access (scan | index lookup) -> filter(residual) -> sort -> limit -> project
struct PhysicalPlan {
    const Table* table = nullptr;  // non-owning, points into the Catalog
    AccessKind access = AccessKind::FullScan;
    std::size_t indexColumn = 0;   // valid for IndexLookup
    Value indexKey;                // valid for IndexLookup
    std::vector<BoundPredicate> residual;
    std::optional<std::size_t> orderColumn;
    bool descending = false;
    std::optional<std::uint64_t> limit;
    std::vector<std::size_t> projection;
};

// Rule-based (not cost-based) planning: if any `=` predicate targets an
// indexed column, the first such predicate in WHERE order drives an index
// lookup and every other predicate becomes a residual filter. Otherwise the
// plan is a full scan with all predicates as residual filters.
PhysicalPlan plan(BoundQuery query);

// Human-readable plan, one operator per line, top (output) to bottom (access).
std::string explainPlan(const PhysicalPlan& plan);

}  // namespace minisql
