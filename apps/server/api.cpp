#include "api.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include "minisql/csv.hpp"
#include "minisql/datagen.hpp"
#include "minisql/error.hpp"
#include "minisql/json.hpp"

namespace minisql::demo {

namespace {

constexpr std::size_t kLargeRows = 100000;
constexpr std::uint64_t kSeed = 42;
// Largest integer a JavaScript number holds exactly (2^53).
constexpr std::int64_t kMaxSafeInt = 9007199254740992LL;

http::Response json(int status, std::string body) {
    http::Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = std::move(body);
    return r;
}

http::Response jsonError(int status, std::string_view kind, std::string_view message,
                         std::optional<std::size_t> position = std::nullopt) {
    std::string body = "{\"error\":{\"kind\":" + jsonQuote(kind) + ",\"message\":" +
                       jsonQuote(message);
    if (position) body += ",\"position\":" + std::to_string(*position);
    body += "}}";
    return json(status, std::move(body));
}

http::Response methodNotAllowed(const char* allow) {
    http::Response r = jsonError(405, "http", std::string("use ") + allow);
    r.headers.push_back({"Allow", allow});
    return r;
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

// INT64 values outside JavaScript's exact integer range are sent as
// strings so the browser never shows a silently rounded number.
std::string valueJson(const Value& v) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (*i >= -kMaxSafeInt && *i <= kMaxSafeInt) return std::to_string(*i);
        return jsonQuote(std::to_string(*i));
    }
    return jsonQuote(std::get<std::string>(v));
}

const char* accessName(AccessKind a) {
    return a == AccessKind::IndexLookup ? "IndexLookup" : "FullScan";
}

}  // namespace

DemoApi::DemoApi(std::string dataDir, std::map<std::string, StaticFile> staticFiles,
                 ApiLimits limits)
    : dataDir_(std::move(dataDir)), staticFiles_(std::move(staticFiles)), limits_(limits) {
    loadSample();
}

void DemoApi::loadSample() {
    auto fresh = std::make_unique<Engine>();
    const std::string dir = dataDir_ + "/";
    fresh->loadCsv("users", dir + "users.csv", loadSchemaFile(dir + "users.schema.json"));
    fresh->createIndex("users", "id");
    // Same generated data twice, so the page can compare a full scan with
    // an index lookup on identical rows.
    fresh->addTable(generateUsers("users_100k", kLargeRows, kSeed));
    fresh->addTable(generateUsers("users_100k_indexed", kLargeRows, kSeed));
    fresh->createIndex("users_100k_indexed", "id");
    fresh->createIndex("users_100k_indexed", "city");
    engine_ = std::move(fresh);
}

http::Response DemoApi::handle(const http::Request& req) {
    const std::string& path = req.path;

    if (path == "/health") {
        if (req.method != "GET") return methodNotAllowed("GET");
        return json(200, "{\"status\":\"ok\"}");
    }
    if (path == "/api/tables") {
        if (req.method != "GET") return methodNotAllowed("GET");
        return tables();
    }
    if (path == "/api/load-sample") {
        if (req.method != "POST") return methodNotAllowed("POST");
        loadSample();
        return tables();
    }
    if (path == "/api/query" || path == "/api/explain") {
        if (req.method != "POST") return methodNotAllowed("POST");
        return query(req, path == "/api/explain");
    }

    // Static files: exact matches only, from the fixed set loaded at
    // startup. No filesystem access per request, so no path traversal.
    auto it = staticFiles_.find(path == "/" ? "/index.html" : path);
    if (it != staticFiles_.end()) {
        if (req.method != "GET") return methodNotAllowed("GET");
        http::Response r;
        r.contentType = it->second.contentType;
        r.body = it->second.body;
        r.headers.push_back({"Content-Security-Policy",
                             "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
                             "form-action 'none'"});
        return r;
    }
    return jsonError(404, "http", "not found");
}

http::Response DemoApi::tables() const {
    std::string body = "{\"tables\":[";
    bool firstTable = true;
    for (const auto& name : engine_->catalog().tableNames()) {
        const Table& t = *engine_->catalog().find(name);
        if (!firstTable) body += ",";
        firstTable = false;
        body += "{\"name\":" + jsonQuote(name) + ",\"rows\":" + std::to_string(t.rowCount()) +
                ",\"columns\":[";
        for (std::size_t c = 0; c < t.columns().size(); ++c) {
            if (c) body += ",";
            body += "{\"name\":" + jsonQuote(t.columns()[c].name) + ",\"type\":" +
                    jsonQuote(typeName(t.columns()[c].type)) +
                    ",\"indexed\":" + (t.indexOn(c) ? "true" : "false") + "}";
        }
        body += "]}";
    }
    body += "]}";
    return json(200, std::move(body));
}

http::Response DemoApi::query(const http::Request& req, bool explainOnly) const {
    const std::string* contentType = req.header("content-type");
    if (!contentType || contentType->rfind("application/json", 0) != 0) {
        return jsonError(415, "http", "send the body as application/json: {\"sql\": \"...\"}");
    }

    std::string sql;
    try {
        const Json body = parseJson(req.body, ErrorKind::Syntax);
        const Json* field = body.kind == Json::Kind::Object ? body.get("sql") : nullptr;
        if (!field || field->kind != Json::Kind::String) {
            return jsonError(400, "http", "body must be a JSON object with a string \"sql\"");
        }
        sql = field->str;
    } catch (const Error& e) {
        return jsonError(400, "http", std::string("request body: ") + e.what());
    }
    if (sql.size() > limits_.maxSqlBytes) {
        return jsonError(400, "http", "query is longer than " +
                                          std::to_string(limits_.maxSqlBytes) + " bytes");
    }

    QueryOutcome out;
    try {
        if (explainOnly) {
            // Accept the query with or without a leading EXPLAIN.
            const PhysicalPlan p = engine_->planFor(sql);
            out.explain = true;
            out.planText = explainPlan(p);
            out.access = p.access;
        } else {
            out = engine_->run(sql);
        }
    } catch (const Error& e) {
        return jsonError(400, errorKindName(e.kind()), e.what(), e.position());
    }

    std::string body = "{\"explain\":" + std::string(out.explain ? "true" : "false") +
                       ",\"access\":" + jsonQuote(accessName(out.access)) +
                       ",\"plan\":" + jsonQuote(out.planText);
    if (!explainOnly) body += ",\"elapsedMs\":" + number(out.elapsedMs);
    if (!out.explain) {
        const ResultSet& rs = out.result;
        const std::size_t shown = std::min(rs.rows.size(), limits_.maxResultRows);
        body += ",\"rowCount\":" + std::to_string(rs.rows.size()) +
                ",\"rowsExamined\":" + std::to_string(rs.rowsExamined) +
                ",\"truncated\":" + (shown < rs.rows.size() ? "true" : "false") + ",\"columns\":[";
        for (std::size_t c = 0; c < rs.columns.size(); ++c) {
            if (c) body += ",";
            body += "{\"name\":" + jsonQuote(rs.columns[c].name) +
                    ",\"type\":" + jsonQuote(typeName(rs.columns[c].type)) + "}";
        }
        body += "],\"rows\":[";
        for (std::size_t r = 0; r < shown; ++r) {
            if (r) body += ",";
            body += "[";
            for (std::size_t c = 0; c < rs.rows[r].size(); ++c) {
                if (c) body += ",";
                body += valueJson(rs.rows[r][c]);
            }
            body += "]";
        }
        body += "]";
    }
    body += "}";
    return json(200, std::move(body));
}

}  // namespace minisql::demo
