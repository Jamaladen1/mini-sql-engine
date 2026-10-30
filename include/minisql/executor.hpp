#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "minisql/planner.hpp"

namespace minisql {

struct ResultSet {
    std::vector<Column> columns;
    std::vector<Row> rows;
    // Rows the filter actually looked at. Smaller than the table when an
    // index narrows the candidates or LIMIT without ORDER BY stops early.
    std::size_t rowsExamined = 0;
};

// Executes a physical plan: produce candidate row IDs (scan or index),
// apply residual predicates, sort (stable by row id), apply LIMIT, then
// project the requested columns into a ResultSet.
ResultSet execute(const PhysicalPlan& plan);

// Renders a ResultSet as an ASCII table with a trailing "(N rows)" line.
std::string renderResult(const ResultSet& result);

}  // namespace minisql
