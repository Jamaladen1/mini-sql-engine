#include <string>

#include "minisql/json.hpp"
#include "test_framework.hpp"

using namespace minisql;

TEST(parse_object_array_scalars) {
    Json j = parseJson(R"({"sql": "SELECT 1", "n": -12.5e3, "ok": true, "x": null, "a": [1, "two", []]})");
    CHECK(j.kind == Json::Kind::Object);
    CHECK_EQ(j.get("sql")->str, std::string("SELECT 1"));
    CHECK(j.get("n")->kind == Json::Kind::Number);
    CHECK_EQ(j.get("n")->str, std::string("-12.5e3"));
    CHECK(j.get("ok")->kind == Json::Kind::Bool);
    CHECK(j.get("x")->kind == Json::Kind::Null);
    CHECK_EQ(j.get("a")->arr.size(), std::size_t{3});
    CHECK(j.get("missing") == nullptr);
}

TEST(simple_escapes) {
    CHECK_EQ(parseJson(R"("a\"b\\c\/d\n\t\r\b\f")").str, std::string("a\"b\\c/d\n\t\r\b\f"));
}

TEST(unicode_escapes_become_utf8) {
    CHECK_EQ(parseJson(R"("\u0041")").str, std::string("A"));
    CHECK_EQ(parseJson(R"("\u00e9")").str, std::string("\xc3\xa9"));          // é
    CHECK_EQ(parseJson(R"("\u20AC")").str, std::string("\xe2\x82\xac"));      // €
    CHECK_EQ(parseJson(R"("\ud83d\ude00")").str, std::string("\xf0\x9f\x98\x80"));  // 😀
    CHECK_EQ(parseJson(R"("\u0001")").str, std::string("\x01"));
    CHECK_EQ(parseJson("\"caf\xc3\xa9\"").str, std::string("caf\xc3\xa9"));  // raw UTF-8 kept
}

TEST(bad_unicode_escapes_rejected) {
    CHECK_THROWS_WITH(parseJson(R"("\u12")"), "truncated \\u escape");
    CHECK_THROWS_WITH(parseJson(R"("\u12G4")"), "invalid hex digit");
    CHECK_THROWS_WITH(parseJson(R"("\ud83d")"), "unpaired high surrogate");
    CHECK_THROWS_WITH(parseJson(R"("\ud83d\u0041")"), "unpaired high surrogate");
    CHECK_THROWS_WITH(parseJson(R"("\ude00")"), "unpaired low surrogate");
}

TEST(errors_use_requested_kind_and_offset) {
    try {
        parseJson(R"({"sql": })", ErrorKind::Syntax);
        CHECK(false);
    } catch (const Error& e) {
        CHECK(e.kind() == ErrorKind::Syntax);
        const std::size_t pos = e.position().value_or(999);
        CHECK_EQ(pos, std::size_t{8});
    }
    CHECK_THROWS_WITH(parseJson("{} x"), "trailing characters");
    CHECK_THROWS_WITH(parseJson(std::string(40, '[')), "nesting too deep");
}

TEST(quote_escapes_specials) {
    CHECK_EQ(jsonQuote("plain"), std::string("\"plain\""));
    CHECK_EQ(jsonQuote("a\"b\\c"), std::string("\"a\\\"b\\\\c\""));
    CHECK_EQ(jsonQuote("\n\r\t\b\f"), std::string("\"\\n\\r\\t\\b\\f\""));
    CHECK_EQ(jsonQuote(std::string("\x01\x1f\x7f", 3)), std::string("\"\\u0001\\u001f\\u007f\""));
    CHECK_EQ(jsonQuote("</script>&"), std::string("\"\\u003c/script\\u003e\\u0026\""));
    CHECK_EQ(jsonQuote("caf\xc3\xa9"), std::string("\"caf\xc3\xa9\""));
}

TEST(quote_round_trips_through_parse) {
    const std::string samples[] = {"", "O'Brien", "say \"hi\"", "tab\there", "line\nbreak",
                                   std::string("nul\0byte", 8), "<b>&amp;</b>", "\xf0\x9f\x98\x80"};
    for (const auto& s : samples) CHECK(parseJson(jsonQuote(s)).str == s);
    for (int c = 0; c < 256; ++c) {
        const std::string s(1, static_cast<char>(c));
        if (c >= 0x80) continue;  // lone high bytes are not valid UTF-8 text
        CHECK(parseJson(jsonQuote(s)).str == s);
    }
}

MINISQL_TEST_MAIN()
