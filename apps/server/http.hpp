#pragma once

// Minimal HTTP/1.1 for the demo server. Deliberately small: one request
// per connection (Connection: close), Content-Length bodies only, strict
// size limits. Not a general-purpose HTTP implementation.

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace minisql::http {

struct Limits {
    std::size_t maxHeaderBytes = 8 * 1024;  // request line + headers
    std::size_t maxBodyBytes = 16 * 1024;
};

struct Request {
    std::string method;   // e.g. "GET", as sent (methods are case-sensitive)
    std::string path;     // target without the query string, e.g. "/api/query"
    std::string query;    // text after '?', not decoded
    std::map<std::string, std::string> headers;  // names lower-cased
    std::string body;

    const std::string* header(const std::string& lowerName) const;
};

struct Response {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;  // extra headers
};

enum class ParseStatus { Incomplete, Complete, Invalid };

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    Request request;          // valid when Complete
    int errorStatus = 0;      // HTTP status to reply with when Invalid
    std::string errorMessage; // short reason when Invalid
};

// Parses the bytes received so far. Returns Incomplete if more input is
// needed, Complete with the request, or Invalid with a status code
// (400, 413, 431, 501, 505) to send back.
ParseResult parseRequest(std::string_view received, const Limits& limits);

// Serializes a response, adding Content-Length, Connection: close and
// baseline security headers.
std::string serializeResponse(const Response& response);

std::string_view statusText(int status) noexcept;

using Handler = std::function<Response(const Request&)>;

struct ServerOptions {
    std::string host = "127.0.0.1";  // IPv4 address to bind
    int port = 8080;
    // Time allowed to receive a complete request. After it, the response
    // gets up to 1 s more to send and the close up to 0.5 s to drain, so a
    // slow client holds the server for at most ~6.5 s. The query itself
    // isn't covered by this limit; it's bounded by the dataset size.
    int requestTimeoutSeconds = 5;
    Limits limits;
};

// Accepts connections one at a time and calls `handler` for each request
// until SIGINT/SIGTERM. Returns a process exit code. Logs one line per
// request to stderr.
int serve(const ServerOptions& options, const Handler& handler);

}  // namespace minisql::http
