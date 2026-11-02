#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "minisql/error.hpp"

namespace minisql {

// Minimal JSON value: enough for schema files and small API request
// bodies. Numbers keep their source text in `str`.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    std::string str;
    std::vector<Json> arr;
    // Object members as parallel arrays: std::vector allows an incomplete
    // element type, std::pair does not.
    std::vector<std::string> keys;
    std::vector<Json> values;

    const Json* get(const std::string& key) const {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return &values[i];
        }
        return nullptr;
    }
};

// Parses one JSON document (objects, arrays, strings, numbers, true,
// false, null). Nesting deeper than 32 levels is rejected. Throws
// minisql::Error of `errorKind` with the byte offset on bad input.
Json parseJson(std::string_view text, ErrorKind errorKind = ErrorKind::Schema);

}  // namespace minisql
