#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "minisql/value.hpp"

namespace minisql {

enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

std::string_view compareOpSymbol(CompareOp op) noexcept;

struct Identifier {
    std::string name;
    std::size_t position = 0;
};

struct Predicate {
    Identifier column;
    CompareOp op = CompareOp::Eq;
    Value literal;
    std::size_t literalPosition = 0;
};

struct OrderBy {
    Identifier column;
    bool descending = false;
};

struct SelectStmt {
    bool selectStar = false;
    std::vector<Identifier> projection;  // empty when selectStar
    Identifier table;
    std::vector<Predicate> where;        // conjunction (AND)
    std::optional<OrderBy> orderBy;
    std::optional<std::uint64_t> limit;
};

struct Statement {
    bool explain = false;
    SelectStmt select;
};

}  // namespace minisql
