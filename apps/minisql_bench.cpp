// Full scan vs hash index benchmark on deterministic generated data.
//
//   minisql_bench [--rows 10000,100000] [--iterations 25] [--warmup 3] [--seed 42]
//
// For each dataset size and query, the query is run with no index (full
// scan), then an index is built and the same query runs again (index
// lookup). Each timing is end-to-end Engine::run (parse, validate, plan,
// execute) measured with std::chrono::steady_clock. Results from the two
// paths are compared row by row; any mismatch fails the run.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "minisql/datagen.hpp"
#include "minisql/engine.hpp"

using namespace minisql;

namespace {

struct Timing {
    double medianMs;
    double minMs;
    double maxMs;
    ResultSet result;
};

Timing measure(const Engine& engine, const std::string& sql, int warmup, int iterations) {
    for (int i = 0; i < warmup; ++i) (void)engine.run(sql);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));
    ResultSet last;
    for (int i = 0; i < iterations; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        QueryOutcome out = engine.run(sql);
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        last = std::move(out.result);
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();
    const double median = n % 2 ? samples[n / 2] : (samples[n / 2 - 1] + samples[n / 2]) / 2.0;
    return {median, samples.front(), samples.back(), std::move(last)};
}

std::vector<std::size_t> parseSizes(const std::string& s) {
    std::vector<std::size_t> out;
    std::size_t start = 0;
    while (start < s.size()) {
        std::size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        out.push_back(static_cast<std::size_t>(std::stoull(s.substr(start, end - start))));
        start = end + 1;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes{10000, 100000};
    int iterations = 25;
    int warmup = 3;
    std::uint64_t seed = 42;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        const std::string v = argv[i + 1];
        if (a == "--rows") sizes = parseSizes(v);
        else if (a == "--iterations") iterations = std::max(1, std::atoi(v.c_str()));
        else if (a == "--warmup") warmup = std::max(0, std::atoi(v.c_str()));
        else if (a == "--seed") seed = std::stoull(v);
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }

    std::printf("# minisql benchmark\n\n");
    std::printf("seed=%llu warmup=%d iterations=%d (timings: end-to-end Engine::run, ms)\n\n",
                static_cast<unsigned long long>(seed), warmup, iterations);
    std::printf("| rows | query | rows returned | full scan median | index median | "
                "speedup (median) | parity |\n");
    std::printf("|---:|---|---:|---:|---:|---:|---|\n");

    bool allOk = true;
    for (std::size_t rows : sizes) {
        struct Q { std::string column; std::string sql; };
        const std::vector<Q> queries{
            {"id", "SELECT * FROM users WHERE id = " + std::to_string(rows / 2 + 7)},
            {"city", "SELECT id, name FROM users WHERE city = 'Columbus'"},
            {"city", "SELECT id FROM users WHERE city = 'Columbus' AND age >= 65"},
        };
        for (const auto& q : queries) {
            // Fresh engine per query so "no index" really means no index.
            Engine engine;
            engine.addTable(generateUsers("users", rows, seed));
            const Timing scan = measure(engine, q.sql, warmup, iterations);
            engine.createIndex("users", q.column);
            const Timing idx = measure(engine, q.sql, warmup, iterations);

            const bool parity = scan.result.rows == idx.result.rows;
            allOk = allOk && parity;
            std::printf("| %zu | `%s` | %zu | %.4f | %.4f | %.1fx | %s |\n", rows, q.sql.c_str(),
                        idx.result.rows.size(), scan.medianMs, idx.medianMs,
                        idx.medianMs > 0 ? scan.medianMs / idx.medianMs : 0.0,
                        parity ? "identical" : "MISMATCH");
        }
    }
    std::printf("\nspeedup = full-scan median / index median, computed from this run.\n");
    return allOk ? 0 : 1;
}
