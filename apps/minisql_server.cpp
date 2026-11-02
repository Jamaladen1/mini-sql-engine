// Browser demo server for the Mini SQL Query Engine.
//
//   minisql_server [--host 127.0.0.1] [--port 8080] [--data data] [--web web]
//
// Serves the page in web/ and the JSON API (see apps/server/api.hpp).
// Binds to 127.0.0.1 unless told otherwise; pass --host 0.0.0.0 only when
// the server should be reachable from other machines (e.g. in Docker).

#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <string>

#include "api.hpp"
#include "http.hpp"
#include "minisql/csv.hpp"
#include "minisql/error.hpp"

namespace {

using namespace minisql;

// The only files ever served. Read once at startup.
const std::map<std::string, std::string> kStaticFiles{
    {"/index.html", "text/html; charset=utf-8"},
    {"/app.js", "text/javascript; charset=utf-8"},
    {"/style.css", "text/css; charset=utf-8"},
};

int usage() {
    std::cerr << "usage: minisql_server [--host IPV4] [--port N] [--data DIR] [--web DIR]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    http::ServerOptions options;
    std::string dataDir = "data";
    std::string webDir = "web";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        if (i + 1 >= argc) return usage();
        const std::string value = argv[++i];
        if (arg == "--host") {
            options.host = value;
        } else if (arg == "--port") {
            char* end = nullptr;
            const long port = std::strtol(value.c_str(), &end, 10);
            if (*end != '\0' || port < 1 || port > 65535) return usage();
            options.port = static_cast<int>(port);
        } else if (arg == "--data") {
            dataDir = value;
        } else if (arg == "--web") {
            webDir = value;
        } else {
            return usage();
        }
    }

    try {
        std::map<std::string, demo::StaticFile> files;
        for (const auto& [path, type] : kStaticFiles) {
            files[path] = {type, readFile(webDir + path)};
        }
        demo::DemoApi api(dataDir, std::move(files));
        return http::serve(options, [&api](const http::Request& r) { return api.handle(r); });
    } catch (const Error& e) {
        std::cerr << "startup failed: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "startup failed: " << e.what() << "\n";
        return 1;
    }
}
