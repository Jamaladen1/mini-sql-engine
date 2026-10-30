#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "minisql/catalog.hpp"

namespace minisql {

// Deterministic synthetic "users" table (id INT64, name TEXT, age INT64,
// city TEXT). Uses splitmix64 so scripts/generate_data.py produces
// byte-identical CSV for the same (rows, seed).
Table generateUsers(const std::string& tableName, std::size_t rows, std::uint64_t seed = 42);

}  // namespace minisql
