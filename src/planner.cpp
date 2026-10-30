#include "minisql/planner.hpp"

#include <sstream>

namespace minisql {

namespace {

std::string literalToSql(const Value& v) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    std::string out = "'";
    for (char c : std::get<std::string>(v)) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    return out + "'";
}

}  // namespace

PhysicalPlan plan(BoundQuery query) {
    PhysicalPlan p;
    p.table = query.table;
    p.orderColumn = query.orderColumn;
    p.descending = query.descending;
    p.limit = query.limit;
    p.projection = std::move(query.projection);

    std::optional<std::size_t> chosen;
    for (std::size_t i = 0; i < query.predicates.size(); ++i) {
        const auto& pred = query.predicates[i];
        if (pred.op == CompareOp::Eq && p.table->indexOn(pred.column) != nullptr) {
            chosen = i;
            break;
        }
    }
    if (chosen) {
        p.access = AccessKind::IndexLookup;
        p.indexColumn = query.predicates[*chosen].column;
        p.indexKey = query.predicates[*chosen].literal;
    }
    for (std::size_t i = 0; i < query.predicates.size(); ++i) {
        if (chosen && i == *chosen) continue;
        p.residual.push_back(std::move(query.predicates[i]));
    }
    return p;
}

std::string explainPlan(const PhysicalPlan& p) {
    const auto& cols = p.table->columns();
    std::ostringstream out;

    out << "Project [";
    for (std::size_t i = 0; i < p.projection.size(); ++i) {
        if (i) out << ", ";
        out << cols[p.projection[i]].name;
    }
    out << "]\n";

    out << "  Limit " << (p.limit ? std::to_string(*p.limit) : std::string("none"));
    if (p.limit && !p.orderColumn) out << " (stops scan early)";
    out << "\n";

    if (p.orderColumn) {
        out << "    Sort " << cols[*p.orderColumn].name << (p.descending ? " DESC" : " ASC")
            << " (tie-break: row id ASC)\n";
    } else {
        out << "    Sort none (row id order)\n";
    }

    out << "      Filter ";
    if (p.residual.empty()) {
        out << "none";
    } else {
        for (std::size_t i = 0; i < p.residual.size(); ++i) {
            if (i) out << " AND ";
            const auto& r = p.residual[i];
            out << cols[r.column].name << ' ' << compareOpSymbol(r.op) << ' '
                << literalToSql(r.literal);
        }
    }
    out << "\n";

    if (p.access == AccessKind::IndexLookup) {
        out << "        IndexLookup table=" << p.table->name()
            << " index=hash(" << cols[p.indexColumn].name << ") key=" << literalToSql(p.indexKey)
            << "\n";
    } else {
        out << "        FullScan table=" << p.table->name() << " rows=" << p.table->rowCount()
            << "\n";
    }
    return out.str();
}

}  // namespace minisql
