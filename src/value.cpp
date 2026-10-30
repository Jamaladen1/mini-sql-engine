#include "minisql/value.hpp"

#include <charconv>

namespace minisql {

std::string_view typeName(ColumnType type) noexcept {
    return type == ColumnType::Int64 ? "INT64" : "TEXT";
}

std::optional<ColumnType> parseTypeName(std::string_view name) noexcept {
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) {
        upper.push_back(static_cast<char>((c >= 'a' && c <= 'z') ? c - 32 : c));
    }
    if (upper == "INT64") return ColumnType::Int64;
    if (upper == "TEXT") return ColumnType::Text;
    return std::nullopt;
}

ColumnType typeOf(const Value& value) noexcept {
    return std::holds_alternative<std::int64_t>(value) ? ColumnType::Int64
                                                       : ColumnType::Text;
}

int compareValues(const Value& a, const Value& b) noexcept {
    if (const auto* ai = std::get_if<std::int64_t>(&a)) {
        const auto* bi = std::get_if<std::int64_t>(&b);
        if (!bi) return 0;  // unreachable when precondition holds
        return (*ai < *bi) ? -1 : (*ai > *bi ? 1 : 0);
    }
    const auto* as = std::get_if<std::string>(&a);
    const auto* bs = std::get_if<std::string>(&b);
    if (!as || !bs) return 0;
    const int c = as->compare(*bs);
    return (c < 0) ? -1 : (c > 0 ? 1 : 0);
}

std::string valueToString(const Value& value) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) return std::to_string(*i);
    return std::get<std::string>(value);
}

std::optional<std::int64_t> parseInt64(std::string_view text) noexcept {
    if (text.empty()) return std::nullopt;
    std::size_t digitsStart = (text[0] == '-') ? 1 : 0;
    if (digitsStart == text.size()) return std::nullopt;
    for (std::size_t i = digitsStart; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return std::nullopt;
    }
    std::int64_t out = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return out;
}

}  // namespace minisql
