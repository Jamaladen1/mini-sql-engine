#include <string>

#include "http.hpp"
#include "test_framework.hpp"

using namespace minisql::http;

namespace {
const Limits kLimits{};

ParseResult p(const std::string& raw) { return parseRequest(raw, kLimits); }
}  // namespace

TEST(parses_get_with_query_and_headers) {
    ParseResult r = p("GET /api/tables?x=1 HTTP/1.1\r\nHost: localhost\r\nX-Thing:  a b \r\n\r\n");
    CHECK(r.status == ParseStatus::Complete);
    CHECK_EQ(r.request.method, std::string("GET"));
    CHECK_EQ(r.request.path, std::string("/api/tables"));
    CHECK_EQ(r.request.query, std::string("x=1"));
    CHECK_EQ(*r.request.header("host"), std::string("localhost"));
    CHECK_EQ(*r.request.header("x-thing"), std::string("a b"));
    CHECK(r.request.body.empty());
}

TEST(parses_post_body_by_content_length) {
    const std::string body = R"({"sql":"SELECT 1"})";
    ParseResult r = p("POST /api/query HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) +
                      "\r\nContent-Type: application/json\r\n\r\n" + body);
    CHECK(r.status == ParseStatus::Complete);
    CHECK_EQ(r.request.body, body);
}

TEST(incomplete_until_head_and_body_arrive) {
    CHECK(p("").status == ParseStatus::Incomplete);
    CHECK(p("GET / HTTP/1.1\r\nHost: x\r\n").status == ParseStatus::Incomplete);
    CHECK(p("POST /a HTTP/1.1\r\nContent-Length: 5\r\n\r\nabc").status == ParseStatus::Incomplete);
    CHECK(p("POST /a HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde").status == ParseStatus::Complete);
}

TEST(header_size_limit) {
    const std::string big = "GET / HTTP/1.1\r\nX: " + std::string(9000, 'a');
    ParseResult r = p(big);
    CHECK(r.status == ParseStatus::Invalid);
    CHECK_EQ(r.errorStatus, 431);
    CHECK_EQ(p(big + "\r\n\r\n").errorStatus, 431);
}

TEST(body_size_limit) {
    ParseResult r = p("POST /a HTTP/1.1\r\nContent-Length: 16385\r\n\r\n");
    CHECK(r.status == ParseStatus::Invalid);
    CHECK_EQ(r.errorStatus, 413);
}

TEST(rejects_malformed_request_lines) {
    CHECK_EQ(p("GET\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET /\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / HTTP/1.1 extra\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET  / HTTP/1.1\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("G(T / HTTP/1.1\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET http://x/ HTTP/1.1\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / FOO/1.1\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / HTTP/2.0\r\n\r\n").errorStatus, 505);
    CHECK_EQ(p("GET /a\x01 HTTP/1.1\r\n\r\n").errorStatus, 400);
}

TEST(rejects_malformed_headers) {
    CHECK_EQ(p("GET / HTTP/1.1\r\nNoColon\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / HTTP/1.1\r\nBad Name: x\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / HTTP/1.1\r\nX: a\r\n  folded\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("GET / HTTP/1.1\r\nX: a\x01" "b\r\n\r\n").errorStatus, 400);
}

TEST(content_length_rules) {
    CHECK_EQ(p("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("POST / HTTP/1.1\r\nContent-Length: 1e3\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("POST / HTTP/1.1\r\nContent-Length: 99999999999\r\n\r\n").errorStatus, 400);
    CHECK_EQ(p("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx").errorStatus, 400);
    CHECK(p("POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx").status ==
          ParseStatus::Complete);
    CHECK_EQ(p("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").errorStatus, 501);
    CHECK_EQ(p("POST / HTTP/1.1\r\nContent-Length: 1\r\n\r\nxy").errorStatus, 400);
}

TEST(repeated_headers_are_merged) {
    ParseResult r = p("GET / HTTP/1.1\r\nAccept: a\r\naccept: b\r\n\r\n");
    CHECK(r.status == ParseStatus::Complete);
    CHECK_EQ(*r.request.header("accept"), std::string("a, b"));
}

TEST(serialize_response) {
    Response r;
    r.status = 404;
    r.contentType = "application/json";
    r.body = "{}";
    r.headers.push_back({"X-Extra", "1"});
    const std::string s = serializeResponse(r);
    CHECK(s.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
    CHECK(s.find("Content-Type: application/json\r\n") != std::string::npos);
    CHECK(s.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(s.find("Connection: close\r\n") != std::string::npos);
    CHECK(s.find("X-Content-Type-Options: nosniff\r\n") != std::string::npos);
    CHECK(s.find("X-Extra: 1\r\n") != std::string::npos);
    CHECK(s.size() >= 6 && s.substr(s.size() - 6) == "\r\n\r\n{}");
}

MINISQL_TEST_MAIN()
