#include "minisql/datagen.hpp"

#include <array>
#include <string_view>

namespace minisql {

namespace {

struct SplitMix64 {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

constexpr std::array<std::string_view, 10> kCities{
    "Columbus", "Austin", "Boston", "Chicago", "Denver",
    "Seattle", "Portland", "Atlanta", "Phoenix", "Miami",
};

}  // namespace

Table generateUsers(const std::string& tableName, std::size_t rows, std::uint64_t seed) {
    SplitMix64 rng{seed};
    std::vector<Row> data;
    data.reserve(rows);
    for (std::size_t i = 0; i < rows; ++i) {
        const auto age = static_cast<std::int64_t>(18 + rng.next() % 63);  // 18..80
        const auto city = kCities[rng.next() % kCities.size()];
        data.push_back(Row{static_cast<std::int64_t>(i), "user_" + std::to_string(i), age,
                           std::string(city)});
    }
    return Table(tableName,
                 {{"id", ColumnType::Int64},
                  {"name", ColumnType::Text},
                  {"age", ColumnType::Int64},
                  {"city", ColumnType::Text}},
                 std::move(data));
}

}  // namespace minisql
