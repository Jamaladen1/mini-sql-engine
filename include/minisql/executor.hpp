#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "minisql/planner.hpp"

namespace minisql {

struct ResultSet {
    std::vector<Column> columns;
    std::vector<Row> rows;
    std::size_t rowsExamined = 0;  // candidate rows produced by the access path
};

// Executes a physical plan: produce candidate row IDs (scan or index),
// apply residual predicates, sort (stable by row id), apply LIMIT, then
// project the requested columns into a ResultSet.
ResultSet execute(const PhysicalPlan& plan);

// Renders a ResultSet as an ASCII table with a trailing "(N rows)" line.
std::string renderResult(const ResultSet& result);

}  // namespace minisql
