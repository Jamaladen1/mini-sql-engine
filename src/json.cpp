#include "minisql/json.hpp"

#include <cstdint>
#include <utility>

namespace minisql {

namespace {

class JsonParser {
public:
    JsonParser(std::string_view s, ErrorKind kind) : s_(s), kind_(kind) {}

    Json parseDocument() {
        Json v = parseValue(0);
        skipWs();
        if (i_ != s_.size()) fail("trailing characters after JSON document");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& msg) const {
        throw Error(kind_, "invalid JSON: " + msg, i_);
    }

    void skipWs() {
        while (i_ < s_.size() &&
               (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }

    void expect(char c) {
        skipWs();
        if (i_ >= s_.size() || s_[i_] != c) fail(std::string("expected '") + c + "'");
        ++i_;
    }

    Json parseValue(int depth) {
        if (depth > 32) fail("nesting too deep");
        skipWs();
        if (i_ >= s_.size()) fail("unexpected end of input");
        const char c = s_[i_];
        Json v;
        if (c == '{') {
            v.kind = Json::Kind::Object;
            ++i_;
            skipWs();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return v; }
            while (true) {
                skipWs();
                std::string key = parseString();
                expect(':');
                Json member = parseValue(depth + 1);
                v.keys.push_back(std::move(key));
                v.values.push_back(std::move(member));
                skipWs();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.kind = Json::Kind::Array;
            ++i_;
            skipWs();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return v; }
            while (true) {
                v.arr.push_back(parseValue(depth + 1));
                skipWs();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.kind = Json::Kind::String;
            v.str = parseString();
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            v.kind = Json::Kind::Number;
            while (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' ||
                                      s_[i_] == 'e' || s_[i_] == 'E' ||
                                      (s_[i_] >= '0' && s_[i_] <= '9'))) {
                v.str.push_back(s_[i_++]);
            }
            return v;
        }
        for (auto [word, kind] : {std::pair{"true", Json::Kind::Bool},
                                  std::pair{"false", Json::Kind::Bool},
                                  std::pair{"null", Json::Kind::Null}}) {
            std::string_view w(word);
            if (s_.substr(i_, w.size()) == w) {
                i_ += w.size();
                v.kind = kind;
                v.str = std::string(w);
                return v;
            }
        }
        fail("unexpected character");
    }

    std::string parseString() {
        if (i_ >= s_.size() || s_[i_] != '"') fail("expected string");
        ++i_;
        std::string out;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i_ >= s_.size()) break;
                char e = s_[i_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': appendUtf8(out, parseUnicodeEscape()); break;
                    default: fail("unsupported escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        fail("unterminated string");
    }

    // Reads the 4 hex digits after "\\u", plus a second \\uXXXX when the
    // first is a UTF-16 high surrogate. Returns the code point.
    std::uint32_t parseUnicodeEscape() {
        const std::uint32_t first = parseHex4();
        if (first >= 0xDC00 && first <= 0xDFFF) fail("unpaired low surrogate in \\u escape");
        if (first < 0xD800 || first > 0xDBFF) return first;
        if (s_.substr(i_, 2) != "\\u") fail("unpaired high surrogate in \\u escape");
        i_ += 2;
        const std::uint32_t second = parseHex4();
        if (second < 0xDC00 || second > 0xDFFF) fail("unpaired high surrogate in \\u escape");
        return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
    }

    std::uint32_t parseHex4() {
        if (s_.size() - i_ < 4) fail("truncated \\u escape");
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= static_cast<std::uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<std::uint32_t>(h - 'A' + 10);
            else fail("invalid hex digit in \\u escape");
        }
        return v;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        auto put = [&out](std::uint32_t b) { out.push_back(static_cast<char>(b)); };
        if (cp < 0x80) {
            put(cp);
        } else if (cp < 0x800) {
            put(0xC0 | (cp >> 6));
            put(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            put(0xE0 | (cp >> 12));
            put(0x80 | ((cp >> 6) & 0x3F));
            put(0x80 | (cp & 0x3F));
        } else {
            put(0xF0 | (cp >> 18));
            put(0x80 | ((cp >> 12) & 0x3F));
            put(0x80 | ((cp >> 6) & 0x3F));
            put(0x80 | (cp & 0x3F));
        }
    }

    std::string_view s_;
    ErrorKind kind_;
    std::size_t i_ = 0;
};

}  // namespace

Json parseJson(std::string_view text, ErrorKind errorKind) {
    return JsonParser(text, errorKind).parseDocument();
}

std::string jsonQuote(std::string_view text) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            // '<', '>' and '&' are escaped so the output is also safe to
            // embed inside an HTML <script> block.
            case '<': out += "\\u003c"; break;
            case '>': out += "\\u003e"; break;
            case '&': out += "\\u0026"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace minisql
