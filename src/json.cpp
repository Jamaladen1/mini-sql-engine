#include "minisql/json.hpp"

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
                    default: fail("unsupported escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        fail("unterminated string");
    }

    std::string_view s_;
    ErrorKind kind_;
    std::size_t i_ = 0;
};

}  // namespace

Json parseJson(std::string_view text, ErrorKind errorKind) {
    return JsonParser(text, errorKind).parseDocument();
}

}  // namespace minisql
