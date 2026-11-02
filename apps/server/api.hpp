#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "http.hpp"
#include "minisql/engine.hpp"

namespace minisql::demo {

struct ApiLimits {
    std::size_t maxSqlBytes = 4096;  // longer queries are rejected
    std::size_t maxResultRows = 500; // rows returned; rowCount still reports the total
};

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
// The API never reads user-supplied paths. Datasets are the bundled CSV
// (data/users.csv) and deterministic generated tables, loaded at startup
// and by /api/load-sample.
class DemoApi {
public:
    DemoApi(std::string dataDir, std::map<std::string, StaticFile> staticFiles,
            ApiLimits limits = {});

    http::Response handle(const http::Request& request);

    // Replaces the catalog with fresh copies of the sample datasets.
    void loadSample();
    const Engine& engine() const { return *engine_; }

private:
    http::Response tables() const;
    http::Response query(const http::Request& request, bool explainOnly) const;

    std::string dataDir_;
    std::map<std::string, StaticFile> staticFiles_;
    ApiLimits limits_;
    std::unique_ptr<Engine> engine_;
};

}  // namespace minisql::demo
