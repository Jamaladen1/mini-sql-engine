#include "minisql/engine.hpp"

#include <chrono>

#include "minisql/csv.hpp"
#include "minisql/error.hpp"
#include "minisql/parser.hpp"
#include "minisql/validator.hpp"

namespace minisql {

void Engine::loadCsv(const std::string& table, const std::string& csvPath,
                     const std::vector<Column>& schema) {
    if (catalog_.find(table)) {
        throw Error(ErrorKind::Catalog, "table '" + table + "' already exists");
    }
    catalog_.addTable(loadCsvFile(table, csvPath, schema));
}

void Engine::addTable(Table table) { catalog_.addTable(std::move(table)); }

void Engine::createIndex(const std::string& table, const std::string& column) {
    Table* t = catalog_.find(table);
    if (!t) throw Error(ErrorKind::Catalog, "unknown table '" + table + "'");
    t->createIndex(column);
}

QueryOutcome Engine::run(std::string_view sql) const {
    const auto start = std::chrono::steady_clock::now();
    const Statement stmt = parse(sql);
    PhysicalPlan p = plan(validate(stmt.select, catalog_));

    QueryOutcome out;
    out.explain = stmt.explain;
    out.planText = explainPlan(p);
    if (!stmt.explain) out.result = execute(p);
    const auto end = std::chrono::steady_clock::now();
    out.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    return out;
}

ResultSet Engine::query(std::string_view sql) const {
    const Statement stmt = parse(sql);
    if (stmt.explain) {
        throw Error(ErrorKind::Semantic, "query() does not accept EXPLAIN; use explain()");
    }
    return execute(plan(validate(stmt.select, catalog_)));
}

std::string Engine::explain(std::string_view sql) const { return explainPlan(planFor(sql)); }

PhysicalPlan Engine::planFor(std::string_view sql) const {
    const Statement stmt = parse(sql);
    return plan(validate(stmt.select, catalog_));
}

}  // namespace minisql
