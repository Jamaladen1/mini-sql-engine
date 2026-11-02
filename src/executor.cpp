#include "minisql/executor.hpp"

#include <algorithm>
#include <sstream>

namespace minisql {

namespace {

bool matches(const Row& row, const BoundPredicate& p) {
    const int c = compareValues(row[p.column], p.literal);
    switch (p.op) {
        case CompareOp::Eq: return c == 0;
        case CompareOp::Ne: return c != 0;
        case CompareOp::Lt: return c < 0;
        case CompareOp::Le: return c <= 0;
        case CompareOp::Gt: return c > 0;
        case CompareOp::Ge: return c >= 0;
    }
    return false;
}

}  // namespace

ResultSet execute(const PhysicalPlan& plan) {
    const Table& table = *plan.table;
    const auto& rows = table.rows();

    // Without ORDER BY, output order is row-id order, so once `limit`
    // rows have passed the filter no later row can appear in the result.
    const bool earlyExit = plan.limit.has_value() && !plan.orderColumn;
    const std::size_t stopAfter =
        earlyExit ? static_cast<std::size_t>(*plan.limit) : rows.size();

    ResultSet result;
    std::vector<std::size_t> ids;

    auto passes = [&plan](const Row& row) {
        for (const auto& p : plan.residual) {
            if (!matches(row, p)) return false;
        }
        return true;
    };

    // 1. Access path (candidate row IDs in ascending order) + 2. filter.
    // Candidates are streamed, never copied into a separate list. Loops
    // keep their counters in locals so the hot path stays tight.
    const HashIndex* index =
        plan.access == AccessKind::IndexLookup ? table.indexOn(plan.indexColumn) : nullptr;
    if (stopAfter == 0) {
        // LIMIT 0 without ORDER BY: nothing to examine.
    } else if (index) {
        const std::vector<std::size_t>& candidates = index->lookup(plan.indexKey);
        ids.reserve(std::min(candidates.size(), stopAfter));
        std::size_t i = 0;
        while (i < candidates.size()) {
            const std::size_t id = candidates[i++];
            if (passes(rows[id])) {
                ids.push_back(id);
                if (ids.size() == stopAfter) break;
            }
        }
        result.rowsExamined = i;
    } else {
        // Full scan. If the plan asked for an index that no longer exists,
        // re-apply the index predicate here so the result is unchanged.
        const bool recheckKey = plan.access == AccessKind::IndexLookup;
        const BoundPredicate eq{plan.indexColumn, CompareOp::Eq, plan.indexKey};
        ids.reserve(std::min(rows.size(), stopAfter));
        std::size_t id = 0;
        while (id < rows.size()) {
            const Row& row = rows[id];
            const std::size_t current = id++;
            if (recheckKey && !matches(row, eq)) continue;
            if (passes(row)) {
                ids.push_back(current);
                if (ids.size() == stopAfter) break;
            }
        }
        result.rowsExamined = id;
    }

    // 3. Sort (total order: key, then row id ascending) and 4. LIMIT.
    std::size_t outCount = ids.size();
    if (plan.limit && *plan.limit < outCount) outCount = static_cast<std::size_t>(*plan.limit);

    if (plan.orderColumn) {
        const std::size_t col = *plan.orderColumn;
        const bool desc = plan.descending;
        auto less = [&](std::size_t a, std::size_t b) {
            const int c = compareValues(rows[a][col], rows[b][col]);
            if (c != 0) return desc ? c > 0 : c < 0;
            return a < b;
        };
        if (outCount < ids.size()) {
            std::partial_sort(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(outCount),
                              ids.end(), less);
        } else {
            std::sort(ids.begin(), ids.end(), less);
        }
    }
    ids.resize(outCount);

    // 5. Projection.
    for (std::size_t c : plan.projection) result.columns.push_back(table.columns()[c]);
    result.rows.reserve(ids.size());
    for (std::size_t id : ids) {
        Row out;
        out.reserve(plan.projection.size());
        for (std::size_t c : plan.projection) out.push_back(rows[id][c]);
        result.rows.push_back(std::move(out));
    }
    return result;
}

std::string renderResult(const ResultSet& result) {
    const std::size_t ncol = result.columns.size();
    std::vector<std::size_t> width(ncol);
    for (std::size_t c = 0; c < ncol; ++c) width[c] = result.columns[c].name.size();
    std::vector<std::vector<std::string>> cells;
    cells.reserve(result.rows.size());
    for (const auto& row : result.rows) {
        std::vector<std::string> line;
        for (std::size_t c = 0; c < ncol; ++c) {
            line.push_back(valueToString(row[c]));
            width[c] = std::max(width[c], line.back().size());
        }
        cells.push_back(std::move(line));
    }

    std::ostringstream out;
    auto sep = [&] {
        out << '+';
        for (std::size_t c = 0; c < ncol; ++c) out << std::string(width[c] + 2, '-') << '+';
        out << '\n';
    };
    auto line = [&](const std::vector<std::string>& vals, bool rightAlignInts) {
        out << '|';
        for (std::size_t c = 0; c < ncol; ++c) {
            const std::string& v = vals[c];
            const std::size_t pad = width[c] - v.size();
            const bool right = rightAlignInts && result.columns[c].type == ColumnType::Int64;
            out << ' ' << (right ? std::string(pad, ' ') + v : v + std::string(pad, ' ')) << " |";
        }
        out << '\n';
    };

    std::vector<std::string> header;
    for (const auto& col : result.columns) header.push_back(col.name);
    sep();
    line(header, false);
    sep();
    for (const auto& row : cells) line(row, true);
    if (!cells.empty()) sep();
    out << '(' << result.rows.size() << (result.rows.size() == 1 ? " row)" : " rows)") << '\n';
    return out.str();
}

}  // namespace minisql
