#include "http.hpp"

#include <algorithm>
#include <cctype>

namespace minisql::http {

namespace {

ParseResult invalid(int status, std::string message) {
    ParseResult r;
    r.status = ParseStatus::Invalid;
    r.errorStatus = status;
    r.errorMessage = std::move(message);
    return r;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimSpaces(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// RFC 9110 token characters, used for methods and header names.
bool isTokenChar(char c) {
    if (std::isalnum(static_cast<unsigned char>(c))) return true;
    return std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

bool isToken(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), isTokenChar);
}

}  // namespace

const std::string* Request::header(const std::string& lowerName) const {
    auto it = headers.find(lowerName);
    return it == headers.end() ? nullptr : &it->second;
}

ParseResult parseRequest(std::string_view received, const Limits& limits) {
    const std::size_t headEnd = received.find("\r\n\r\n");
    if (headEnd == std::string_view::npos) {
        if (received.size() > limits.maxHeaderBytes) {
            return invalid(431, "request headers too large");
        }
        return {};  // Incomplete
    }
    if (headEnd + 4 > limits.maxHeaderBytes) return invalid(431, "request headers too large");

    const std::string_view head = received.substr(0, headEnd);
    std::size_t lineEnd = head.find("\r\n");
    const std::string_view requestLine = head.substr(0, lineEnd);

    // request-line = method SP request-target SP HTTP-version
    const std::size_t sp1 = requestLine.find(' ');
    const std::size_t sp2 =
        sp1 == std::string_view::npos ? std::string_view::npos : requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos ||
        requestLine.find(' ', sp2 + 1) != std::string_view::npos) {
        return invalid(400, "malformed request line");
    }
    const std::string_view method = requestLine.substr(0, sp1);
    const std::string_view target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = requestLine.substr(sp2 + 1);

    if (!isToken(method)) return invalid(400, "malformed method");
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        if (version.rfind("HTTP/", 0) == 0) return invalid(505, "HTTP version not supported");
        return invalid(400, "malformed HTTP version");
    }
    if (target.empty() || target.front() != '/') {
        return invalid(400, "request target must be an absolute path");
    }
    for (char c : target) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7F) return invalid(400, "invalid character in request target");
    }

    ParseResult result;
    Request& req = result.request;
    req.method = std::string(method);
    const std::size_t q = target.find('?');
    req.path = std::string(target.substr(0, q));
    if (q != std::string_view::npos) req.query = std::string(target.substr(q + 1));

    // header-field = field-name ":" OWS field-value OWS
    while (lineEnd != std::string_view::npos) {
        const std::size_t start = lineEnd + 2;
        lineEnd = head.find("\r\n", start);
        const std::string_view line = head.substr(start, lineEnd == std::string_view::npos
                                                             ? std::string_view::npos
                                                             : lineEnd - start);
        if (line.empty()) continue;
        if (line.front() == ' ' || line.front() == '\t') {
            return invalid(400, "obsolete header line folding is not supported");
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || !isToken(line.substr(0, colon))) {
            return invalid(400, "malformed header line");
        }
        const std::string_view value = trimSpaces(line.substr(colon + 1));
        for (char c : value) {
            const auto u = static_cast<unsigned char>(c);
            if ((u < 0x20 && c != '\t') || u == 0x7F) return invalid(400, "invalid header value");
        }
        const std::string name = toLower(line.substr(0, colon));
        auto [it, inserted] = req.headers.emplace(name, std::string(value));
        if (!inserted) {
            // Repeated Content-Length with different values is a classic
            // request-smuggling vector; reject it. Other repeats are merged.
            if (name == "content-length" && it->second != value) {
                return invalid(400, "conflicting Content-Length headers");
            }
            if (name != "content-length") it->second += ", " + std::string(value);
        }
    }

    if (req.header("transfer-encoding")) {
        return invalid(501, "Transfer-Encoding is not supported; send Content-Length");
    }

    std::size_t bodyLength = 0;
    if (const std::string* cl = req.header("content-length")) {
        if (cl->empty() || cl->size() > 9 ||
            !std::all_of(cl->begin(), cl->end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return invalid(400, "invalid Content-Length");
        }
        bodyLength = static_cast<std::size_t>(std::stoul(*cl));
        if (bodyLength > limits.maxBodyBytes) return invalid(413, "request body too large");
    }

    const std::size_t bodyStart = headEnd + 4;
    if (received.size() - bodyStart < bodyLength) return {};  // Incomplete
    if (received.size() - bodyStart > bodyLength) {
        // One request per connection: bytes after the body are not allowed.
        return invalid(400, "unexpected data after request body");
    }
    req.body = std::string(received.substr(bodyStart, bodyLength));
    result.status = ParseStatus::Complete;
    return result;
}

std::string_view statusText(int status) noexcept {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Content Too Large";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

std::string serializeResponse(const Response& r) {
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " +
                      std::string(statusText(r.status)) + "\r\n";
    out += "Content-Type: " + r.contentType + "\r\n";
    out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Referrer-Policy: no-referrer\r\n";
    for (const auto& [name, value] : r.headers) out += name + ": " + value + "\r\n";
    out += "\r\n";
    out += r.body;
    return out;
}

}  // namespace minisql::http
