#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "http.hpp"
#include "minisql/engine.hpp"

namespace minisql::demo {

struct ApiOptions {
    std::size_t maxSqlBytes = 4096;  // longer queries are rejected
    std::size_t maxResultRows = 500; // rows returned; rowCount still reports the total
    // Accepted Host header values ("host:port", compared case-insensitively).
    // Empty means any host. A local server should list its own names, so a
    // DNS-rebinding page can't reach it under another name.
    std::vector<std::string> allowedHosts;
};

// One cell as JSON. INT64 values outside +-2^53 become strings, so a
// JavaScript client never shows a silently rounded number.
std::string valueToJson(const Value& value);

struct StaticFile {
    std::string contentType;
    std::string body;
};

// The demo's JSON API plus a fixed set of static files.
//
//   GET  /health            {"status":"ok"}
//   GET  /api/tables        tables, row counts, columns, types, indexes
//   POST /api/load-sample   reset to the bundled sample datasets
//   POST /api/query         {"sql": "..."} -> columns, rows, plan, timing
//   POST /api/explain       {"sql": "..."} -> plan only
//
// Every POST must be application/json. Browsers can't send that
// cross-origin without a CORS preflight, which this server never approves.
//
// The API never reads user-supplied paths. Datasets are the bundled CSV
// (data/users.csv) and deterministic generated tables, loaded at startup
// and by /api/load-sample.
class DemoApi {
public:
    DemoApi(std::string dataDir, std::map<std::string, StaticFile> staticFiles,
            ApiOptions options = {});

    http::Response handle(const http::Request& request);

    // Replaces the catalog with fresh copies of the sample datasets.
    void loadSample();
    const Engine& engine() const { return *engine_; }

private:
    http::Response tables() const;
    http::Response query(const http::Request& request, bool explainOnly) const;
    // Returns an error response if the Host/Origin/Content-Type checks fail.
    std::optional<http::Response> checkRequest(const http::Request& request) const;

    std::string dataDir_;
    std::map<std::string, StaticFile> staticFiles_;
    ApiOptions options_;
    std::unique_ptr<Engine> engine_;
};

}  // namespace minisql::demo
