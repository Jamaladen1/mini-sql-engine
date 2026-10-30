#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minisql {

enum class TokenType {
    // Keywords (matched case-insensitively)
    Select, From, Where, And, Order, By, Asc, Desc, Limit, Explain,
    // SQL words this engine recognises but deliberately does not support
    // (JOIN, GROUP, OR, ...). Kept as a distinct token so the parser can
    // reject them with a clear "unsupported" message.
    Unsupported,
    Identifier,
    Integer,  // signed base-10 INT64 literal
    String,   // single-quoted, '' escapes a quote; `text` holds the decoded value
    Comma, Star, Semicolon,
    Eq, Ne, Lt, Le, Gt, Ge,
    End,
};

struct Token {
    TokenType type;
    std::string text;          // raw spelling (decoded value for String)
    std::size_t position;      // zero-based byte offset in the query
    std::int64_t intValue = 0; // valid when type == Integer
};

std::string_view tokenTypeName(TokenType type) noexcept;

// Throws minisql::Error (Syntax) with the offending offset on bad input:
// unknown characters, unterminated strings, out-of-range integers.
std::vector<Token> tokenize(std::string_view sql);

}  // namespace minisql
