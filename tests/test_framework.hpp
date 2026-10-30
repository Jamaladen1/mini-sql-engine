#pragma once

// Tiny dependency-free test harness. Each test file defines tests with
// TEST(name) { ... } and ends with MINISQL_TEST_MAIN().

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "minisql/error.hpp"

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct Failure : std::exception {
    std::string msg;
    explicit Failure(std::string m) : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

inline int runAll() {
    int failed = 0;
    for (const auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[ PASS ] " << t.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << ": " << e.what() << "\n";
        }
    }
    std::cout << (registry().size() - static_cast<std::size_t>(failed)) << "/"
              << registry().size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)

#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::testfw::Registrar TEST_CONCAT(registrar_, name)(#name, &name);         \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::ostringstream os_;                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";      \
            throw ::testfw::Failure(os_.str());                                     \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        const auto va_ = (a);  /* copies: (a) may be a temporary */            \
        const auto vb_ = (b);                                                       \
        if (!(va_ == vb_)) {                                                        \
            std::ostringstream os_;                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ") failed: [" \
                << va_ << "] != [" << vb_ << "]";                                   \
            throw ::testfw::Failure(os_.str());                                     \
        }                                                                           \
    } while (0)

// Asserts that `expr` throws minisql::Error whose message contains `needle`.
#define CHECK_THROWS_WITH(expr, needle)                                             \
    do {                                                                            \
        bool threw_ = false;                                                        \
        try {                                                                       \
            (void)(expr);                                                           \
        } catch (const ::minisql::Error& e_) {                                      \
            threw_ = true;                                                          \
            if (std::string(e_.what()).find(needle) == std::string::npos) {         \
                std::ostringstream os_;                                             \
                os_ << __FILE__ << ":" << __LINE__ << ": error message '" << e_.what() \
                    << "' does not contain '" << (needle) << "'";                   \
                throw ::testfw::Failure(os_.str());                                 \
            }                                                                       \
        }                                                                           \
        if (!threw_) {                                                              \
            std::ostringstream os_;                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": expected minisql::Error from " #expr; \
            throw ::testfw::Failure(os_.str());                                     \
        }                                                                           \
    } while (0)

#define MINISQL_TEST_MAIN() \
    int main() { return ::testfw::runAll(); }
