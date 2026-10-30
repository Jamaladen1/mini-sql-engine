#include "minisql/lexer.hpp"

#include <array>
#include <utility>

#include "minisql/error.hpp"
#include "minisql/value.hpp"

namespace minisql {

namespace {

bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return out;
}

constexpr std::array<std::pair<std::string_view, TokenType>, 10> kKeywords{{
    {"SELECT", TokenType::Select}, {"FROM", TokenType::From},
    {"WHERE", TokenType::Where},   {"AND", TokenType::And},
    {"ORDER", TokenType::Order},   {"BY", TokenType::By},
    {"ASC", TokenType::Asc},       {"DESC", TokenType::Desc},
    {"LIMIT", TokenType::Limit},   {"EXPLAIN", TokenType::Explain},
}};

constexpr std::array<std::string_view, 30> kUnsupported{
    "JOIN",   "INNER",  "LEFT",   "RIGHT",    "OUTER",  "ON",     "GROUP",  "HAVING",
    "OR",     "NOT",    "NULL",   "IS",       "IN",     "LIKE",   "BETWEEN", "AS",
    "DISTINCT", "OFFSET", "UNION", "INSERT",  "UPDATE", "DELETE", "CREATE", "DROP",
    "ALTER",  "COUNT",  "SUM",    "AVG",      "MIN",    "MAX",
};

}  // namespace

std::string_view tokenTypeName(TokenType type) noexcept {
    switch (type) {
        case TokenType::Select: return "SELECT";
        case TokenType::From: return "FROM";
        case TokenType::Where: return "WHERE";
        case TokenType::And: return "AND";
        case TokenType::Order: return "ORDER";
        case TokenType::By: return "BY";
        case TokenType::Asc: return "ASC";
        case TokenType::Desc: return "DESC";
        case TokenType::Limit: return "LIMIT";
        case TokenType::Explain: return "EXPLAIN";
        case TokenType::Unsupported: return "unsupported keyword";
        case TokenType::Identifier: return "identifier";
        case TokenType::Integer: return "integer";
        case TokenType::String: return "string";
        case TokenType::Comma: return "','";
        case TokenType::Star: return "'*'";
        case TokenType::Semicolon: return "';'";
        case TokenType::Eq: return "'='";
        case TokenType::Ne: return "'!='";
        case TokenType::Lt: return "'<'";
        case TokenType::Le: return "'<='";
        case TokenType::Gt: return "'>'";
        case TokenType::Ge: return "'>='";
        case TokenType::End: return "end of input";
    }
    return "?";
}

std::vector<Token> tokenize(std::string_view sql) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = sql.size();

    auto push = [&](TokenType t, std::size_t pos, std::size_t len) {
        tokens.push_back({t, std::string(sql.substr(pos, len)), pos});
    };

    while (i < n) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        const std::size_t start = i;

        if (isIdentStart(c)) {
            while (i < n && isIdentChar(sql[i])) ++i;
            std::string_view word = sql.substr(start, i - start);
            const std::string upper = toUpper(word);
            TokenType type = TokenType::Identifier;
            for (const auto& [kw, t] : kKeywords) {
                if (upper == kw) type = t;
            }
            if (type == TokenType::Identifier) {
                for (auto kw : kUnsupported) {
                    if (upper == kw) type = TokenType::Unsupported;
                }
            }
            push(type, start, i - start);
            continue;
        }

        if (isDigit(c) || (c == '-' && i + 1 < n && isDigit(sql[i + 1]))) {
            ++i;
            while (i < n && isDigit(sql[i])) ++i;
            if (i < n && isIdentChar(sql[i])) {
                throw Error(ErrorKind::Syntax, "malformed number", start);
            }
            std::string_view digits = sql.substr(start, i - start);
            auto v = parseInt64(digits);
            if (!v) {
                throw Error(ErrorKind::Syntax,
                            "integer literal '" + std::string(digits) + "' is out of INT64 range",
                            start);
            }
            Token t{TokenType::Integer, std::string(digits), start, *v};
            tokens.push_back(std::move(t));
            continue;
        }

        if (c == '\'') {
            ++i;
            std::string value;
            bool closed = false;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') {
                        value.push_back('\'');
                        i += 2;
                        continue;
                    }
                    ++i;
                    closed = true;
                    break;
                }
                value.push_back(sql[i++]);
            }
            if (!closed) throw Error(ErrorKind::Syntax, "unterminated string literal", start);
            tokens.push_back({TokenType::String, std::move(value), start});
            continue;
        }

        switch (c) {
            case ',': push(TokenType::Comma, start, 1); ++i; continue;
            case '*': push(TokenType::Star, start, 1); ++i; continue;
            case ';': push(TokenType::Semicolon, start, 1); ++i; continue;
            case '=': push(TokenType::Eq, start, 1); ++i; continue;
            case '!':
                if (i + 1 < n && sql[i + 1] == '=') {
                    push(TokenType::Ne, start, 2);
                    i += 2;
                    continue;
                }
                break;
            case '<':
                if (i + 1 < n && sql[i + 1] == '=') {
                    push(TokenType::Le, start, 2);
                    i += 2;
                } else if (i + 1 < n && sql[i + 1] == '>') {
                    throw Error(ErrorKind::Syntax, "'<>' is not supported; use '!='", start);
                } else {
                    push(TokenType::Lt, start, 1);
                    ++i;
                }
                continue;
            case '>':
                if (i + 1 < n && sql[i + 1] == '=') {
                    push(TokenType::Ge, start, 2);
                    i += 2;
                } else {
                    push(TokenType::Gt, start, 1);
                    ++i;
                }
                continue;
            case '"':
                throw Error(ErrorKind::Syntax,
                            "double-quoted identifiers/strings are not supported; "
                            "use single quotes for strings",
                            start);
            default: break;
        }
        throw Error(ErrorKind::Syntax, std::string("unexpected character '") + c + "'", start);
    }
    tokens.push_back({TokenType::End, "", n});
    return tokens;
}

}  // namespace minisql
