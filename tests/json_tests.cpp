#include <cstdint>
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
    for (int c = 0; c < 0x80; ++c) {
        const std::string s(1, static_cast<char>(c));
        CHECK(parseJson(jsonQuote(s)).str == s);
    }
}

namespace {

// Independent strict UTF-8 check (decodes code points), used to judge
// jsonQuote's output without reusing its own logic.
bool isValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        std::uint32_t cp;
        std::size_t n;
        if (b < 0x80) { cp = b; n = 1; }
        else if ((b >> 5) == 0x6) { cp = b & 0x1F; n = 2; }
        else if ((b >> 4) == 0xE) { cp = b & 0x0F; n = 3; }
        else if ((b >> 3) == 0x1E) { cp = b & 0x07; n = 4; }
        else return false;
        if (i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k) {
            const auto c = static_cast<unsigned char>(s[i + k]);
            if ((c >> 6) != 0x2) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        const std::uint32_t minFor[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < minFor[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += n;
    }
    return true;
}

}  // namespace

TEST(quote_output_is_always_valid_utf8) {
    for (int c = 0; c < 256; ++c) {
        CHECK(isValidUtf8(jsonQuote(std::string(1, static_cast<char>(c)))));
    }
    // splitmix64-driven random byte strings
    std::uint64_t state = 7;
    auto next = [&state] {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    for (int n = 0; n < 5000; ++n) {
        std::string s(next() % 12, '\0');
        for (char& ch : s) ch = static_cast<char>(next() & 0xFF);
        const std::string q = jsonQuote(s);
        if (!isValidUtf8(q)) throw testfw::Failure("invalid UTF-8 from jsonQuote");
        (void)parseJson(q);  // and it parses back
    }
}

TEST(quote_replaces_invalid_utf8) {
    const std::string fffd = "\xEF\xBF\xBD";
    CHECK_EQ(jsonQuote("\xC3"), "\"" + fffd + "\"");                    // lone lead byte
    CHECK_EQ(jsonQuote("a\xC3" "b"), "\"a" + fffd + "b\"");             // lead + ASCII
    CHECK_EQ(jsonQuote("\xC0\x80"), "\"" + fffd + fffd + "\"");         // overlong NUL
    CHECK_EQ(jsonQuote("\xED\xA0\x80"), "\"" + fffd + fffd + fffd + "\"");  // surrogate
    CHECK_EQ(jsonQuote("\xE2\x82"), "\"" + fffd + fffd + "\"");         // truncated
    CHECK_EQ(jsonQuote("\xF5\x80\x80\x80"), "\"" + fffd + fffd + fffd + fffd + "\"");  // > U+10FFFF
    CHECK_EQ(jsonQuote("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"),
             std::string("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\""));  // valid passes through
}

TEST(strict_number_grammar) {
    for (const char* ok : {"0", "-0", "12", "-12.5", "1e3", "1E+3", "2.5e-10"}) {
        CHECK_EQ(parseJson(ok).str, std::string(ok));
    }
    for (const char* bad : {"--1", "-", "+1", "01", "1.", ".5", "1e", "1e+", "--+e", "1.2.3", "0x10"}) {
        bool threw = false;
        try { (void)parseJson(bad); } catch (const Error&) { threw = true; }
        if (!threw) throw testfw::Failure(std::string("accepted invalid number: ") + bad);
    }
}

TEST(rejects_raw_control_characters_in_strings) {
    CHECK_THROWS_WITH(parseJson("\"a\tb\""), "unescaped control character");
    CHECK_THROWS_WITH(parseJson("\"a\nb\""), "unescaped control character");
    CHECK_THROWS_WITH(parseJson(std::string("\"a\0b\"", 5)), "unescaped control character");
    CHECK_EQ(parseJson("\"a\\tb\"").str, std::string("a\tb"));
}

TEST(rejects_duplicate_keys) {
    CHECK_THROWS_WITH(parseJson(R"({"sql":"a","sql":"b"})"), "duplicate key \"sql\"");
    CHECK_EQ(parseJson(R"({"a":{"x":1},"b":{"x":2}})").get("b")->get("x")->str, std::string("2"));
}

MINISQL_TEST_MAIN()
