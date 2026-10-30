#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minisql {

enum class ColumnType { Int64, Text };

using Value = std::variant<std::int64_t, std::string>;
using Row = std::vector<Value>;

struct Column {
    std::string name;
    ColumnType type;
};

std::string_view typeName(ColumnType type) noexcept;
std::optional<ColumnType> parseTypeName(std::string_view name) noexcept;
ColumnType typeOf(const Value& value) noexcept;

// Three-way comparison of two values of the same type.
// Precondition: typeOf(a) == typeOf(b). Mixed types are rejected by the
// validator before execution ever reaches this point.
int compareValues(const Value& a, const Value& b) noexcept;

std::string valueToString(const Value& value);

// Strict base-10 INT64 parsing: optional leading '-', digits only, no
// whitespace, overflow rejected. Returns nullopt on any failure.
std::optional<std::int64_t> parseInt64(std::string_view text) noexcept;

}  // namespace minisql
