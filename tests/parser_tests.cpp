#include <cstdint>
#include <limits>

#include "minisql/lexer.hpp"
#include "minisql/parser.hpp"
#include "test_framework.hpp"

using namespace minisql;

// ------------------------------------------------------------- Lexer ----

TEST(lexer_whitespace_and_offsets) {
    auto toks = tokenize("  SELECT\t*\n FROM   t ;");
    CHECK_EQ(toks.size(), std::size_t{6});
    CHECK(toks[0].type == TokenType::Select);
    CHECK_EQ(toks[0].position, std::size_t{2});
    CHECK(toks[1].type == TokenType::Star);
    CHECK_EQ(toks[1].position, std::size_t{9});
    CHECK(toks[2].type == TokenType::From);
    CHECK(toks[3].type == TokenType::Identifier);
    CHECK_EQ(toks[3].text, std::string("t"));
    CHECK(toks[4].type == TokenType::Semicolon);
    CHECK(toks[5].type == TokenType::End);
}

TEST(lexer_keywords_case_insensitive_identifiers_preserved) {
    auto toks = tokenize("sElEcT MyCol fRoM Users");
    CHECK(toks[0].type == TokenType::Select);
    CHECK(toks[1].type == TokenType::Identifier);
    CHECK_EQ(toks[1].text, std::string("MyCol"));
    CHECK(toks[2].type == TokenType::From);
    CHECK_EQ(toks[3].text, std::string("Users"));
}

TEST(lexer_escaped_quotes) {
    auto toks = tokenize("'it''s' '' 'a,b'");
    CHECK(toks[0].type == TokenType::String);
    CHECK_EQ(toks[0].text, std::string("it's"));
    CHECK_EQ(toks[1].text, std::string(""));
    CHECK_EQ(toks[2].text, std::string("a,b"));
}

TEST(lexer_negative_integers_and_bounds) {
    auto toks = tokenize("-42 0 9223372036854775807 -9223372036854775808");
    CHECK_EQ(toks[0].intValue, std::int64_t{-42});
    CHECK_EQ(toks[1].intValue, std::int64_t{0});
    CHECK_EQ(toks[2].intValue, std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(toks[3].intValue, std::numeric_limits<std::int64_t>::min());
}

TEST(lexer_integer_overflow_rejected) {
    CHECK_THROWS_WITH(tokenize("9223372036854775808"), "out of INT64 range");
    CHECK_THROWS_WITH(tokenize("-9223372036854775809"), "out of INT64 range");
}

TEST(lexer_operators) {
    auto toks = tokenize("= != < <= > >=");
    CHECK(toks[0].type == TokenType::Eq);
    CHECK(toks[1].type == TokenType::Ne);
    CHECK(toks[2].type == TokenType::Lt);
    CHECK(toks[3].type == TokenType::Le);
    CHECK(toks[4].type == TokenType::Gt);
    CHECK(toks[5].type == TokenType::Ge);
}

TEST(lexer_errors_have_positions) {
    CHECK_THROWS_WITH(tokenize("SELECT 'abc"), "at position 7: unterminated string");
    CHECK_THROWS_WITH(tokenize("SELECT @"), "at position 7: unexpected character '@'");
    CHECK_THROWS_WITH(tokenize("a <> b"), "'<>' is not supported");
    CHECK_THROWS_WITH(tokenize("LIMIT 10abc"), "malformed number");
    CHECK_THROWS_WITH(tokenize("x = \"str\""), "double-quoted");
}

// ------------------------------------------------------------ Parser ----

TEST(parse_select_star) {
    Statement s = parse("SELECT * FROM users");
    CHECK(!s.explain);
    CHECK(s.select.selectStar);
    CHECK_EQ(s.select.table.name, std::string("users"));
    CHECK(s.select.where.empty());
    CHECK(!s.select.orderBy);
    CHECK(!s.select.limit);
}

TEST(parse_full_query) {
    Statement s = parse(
        "SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' "
        "ORDER BY age DESC LIMIT 10;");
    const auto& q = s.select;
    CHECK(!q.selectStar);
    CHECK_EQ(q.projection.size(), std::size_t{2});
    CHECK_EQ(q.projection[0].name, std::string("id"));
    CHECK_EQ(q.projection[1].name, std::string("name"));
    CHECK_EQ(q.where.size(), std::size_t{2});
    CHECK(q.where[0].op == CompareOp::Ge);
    CHECK(std::get<std::int64_t>(q.where[0].literal) == 21);
    CHECK(q.where[1].op == CompareOp::Eq);
    CHECK(std::get<std::string>(q.where[1].literal) == "Columbus");
    CHECK(q.orderBy.has_value());
    CHECK_EQ(q.orderBy->column.name, std::string("age"));
    CHECK(q.orderBy->descending);
    CHECK(q.limit.has_value());
    CHECK_EQ(*q.limit, std::uint64_t{10});
}

TEST(parse_order_by_default_and_explicit_asc) {
    CHECK(!parse("SELECT * FROM t ORDER BY a").select.orderBy->descending);
    CHECK(!parse("SELECT * FROM t ORDER BY a asc").select.orderBy->descending);
}

TEST(parse_explain) {
    Statement s = parse("explain SELECT * FROM users WHERE id = 42");
    CHECK(s.explain);
    CHECK_EQ(s.select.where.size(), std::size_t{1});
}

TEST(parse_negative_literal_and_limit_zero) {
    Statement s = parse("SELECT * FROM t WHERE x > -5 LIMIT 0");
    CHECK(std::get<std::int64_t>(s.select.where[0].literal) == -5);
    CHECK_EQ(*s.select.limit, std::uint64_t{0});
}

TEST(parse_errors) {
    CHECK_THROWS_WITH(parse("SELECT id users"), "expected FROM but found identifier 'users'");
    CHECK_THROWS_WITH(parse("SELECT id"), "expected FROM but found end of input");
    CHECK_THROWS_WITH(parse("SELECT FROM t"), "expected column name or '*'");
    CHECK_THROWS_WITH(parse("SELECT * FROM"), "expected table name");
    CHECK_THROWS_WITH(parse("SELECT * FROM t WHERE"), "expected column name in WHERE");
    CHECK_THROWS_WITH(parse("SELECT * FROM t WHERE a"), "expected comparison operator");
    CHECK_THROWS_WITH(parse("SELECT * FROM t WHERE a = b"), "expected integer or 'string'");
    CHECK_THROWS_WITH(parse("SELECT * FROM t ORDER a"), "expected BY after ORDER");
    CHECK_THROWS_WITH(parse("SELECT * FROM t LIMIT -1"), "LIMIT must be a nonnegative");
    CHECK_THROWS_WITH(parse("SELECT * FROM t LIMIT 'x'"), "expected integer after LIMIT");
    CHECK_THROWS_WITH(parse("SELECT * FROM t; SELECT"), "expected end of query");
    CHECK_THROWS_WITH(parse("SELECT a, FROM t"), "expected column name but found FROM");
    CHECK_THROWS_WITH(parse(""), "expected SELECT but found end of input");
}

TEST(parse_unsupported_syntax_rejected_explicitly) {
    CHECK_THROWS_WITH(parse("SELECT * FROM a JOIN b"), "unsupported SQL feature 'JOIN'");
    CHECK_THROWS_WITH(parse("SELECT * FROM t WHERE a = 1 OR b = 2"),
                      "unsupported SQL feature 'OR'");
    CHECK_THROWS_WITH(parse("SELECT * FROM t GROUP BY a"), "unsupported SQL feature 'GROUP'");
    CHECK_THROWS_WITH(parse("SELECT COUNT FROM t"), "unsupported SQL feature 'COUNT'");
    CHECK_THROWS_WITH(parse("DELETE FROM t"), "unsupported SQL feature 'DELETE'");
    CHECK_THROWS_WITH(parse("SELECT * FROM t WHERE a = NULL"), "unsupported SQL feature 'NULL'");
}

TEST(parse_error_position_points_at_token) {
    try {
        parse("SELECT * FROM t WHERE a ~ 1");
        CHECK(false);
    } catch (const minisql::Error& e) {
        CHECK(e.position().has_value());
        const std::size_t pos = e.position().value_or(0);
        CHECK_EQ(pos, std::size_t{24});
    }
}

MINISQL_TEST_MAIN()
