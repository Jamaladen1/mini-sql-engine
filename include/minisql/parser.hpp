#pragma once

#include <string_view>

#include "minisql/ast.hpp"

namespace minisql {

// Recursive-descent parser for the documented SQL subset:
//
//   statement  := [EXPLAIN] query
//   query      := SELECT projection FROM identifier
//                 [WHERE predicate (AND predicate)*]
//                 [ORDER BY identifier [ASC|DESC]]
//                 [LIMIT integer] [;]
//   projection := * | identifier (, identifier)*
//   predicate  := identifier (=|!=|<|<=|>|>=) literal
//   literal    := signed_integer | 'single-quoted string'
//
// The parser only builds an AST; it never touches the catalog or executes
// anything. Throws minisql::Error (Syntax) with a byte offset on failure.
Statement parse(std::string_view sql);

}  // namespace minisql
