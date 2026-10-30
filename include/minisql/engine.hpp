#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "minisql/catalog.hpp"
#include "minisql/executor.hpp"
#include "minisql/planner.hpp"

namespace minisql {

struct QueryOutcome {
    bool explain = false;
    std::string planText;   // filled for EXPLAIN (and always available)
    ResultSet result;       // empty for EXPLAIN
    double elapsedMs = 0.0; // parse + validate + plan + execute
};

// Facade tying the pipeline together:
//   SQL -> lexer -> parser -> AST -> validator -> planner -> executor
// The Engine owns the Catalog; every stage below it borrows.
class Engine {
public:
    Catalog& catalog() noexcept { return catalog_; }
    const Catalog& catalog() const noexcept { return catalog_; }

    void loadCsv(const std::string& table, const std::string& csvPath,
                 const std::vector<Column>& schema);
    void addTable(Table table);
    void createIndex(const std::string& table, const std::string& column);

    // Runs a SELECT or EXPLAIN SELECT statement.
    QueryOutcome run(std::string_view sql) const;
    // Convenience: run and return rows (EXPLAIN is rejected).
    ResultSet query(std::string_view sql) const;
    // Convenience: plan text for a SELECT (with or without EXPLAIN prefix).
    std::string explain(std::string_view sql) const;
    PhysicalPlan planFor(std::string_view sql) const;

private:
    Catalog catalog_;
};

}  // namespace minisql
