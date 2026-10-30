#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace minisql {

enum class ErrorKind { Syntax, Semantic, Csv, Schema, Catalog, Io };

// Every user-facing failure in the engine is reported through this type.
// `position` is a zero-based byte offset into SQL text, or a 1-based line
// number for CSV/schema errors (see `line`).
class Error : public std::runtime_error {
public:
    Error(ErrorKind kind, std::string message,
          std::optional<std::size_t> position = std::nullopt,
          std::optional<std::size_t> line = std::nullopt)
        : std::runtime_error(format(kind, message, position, line)),
          kind_(kind), position_(position), line_(line) {}

    ErrorKind kind() const noexcept { return kind_; }
    std::optional<std::size_t> position() const noexcept { return position_; }
    std::optional<std::size_t> line() const noexcept { return line_; }

private:
    static std::string kindName(ErrorKind k) {
        switch (k) {
            case ErrorKind::Syntax: return "syntax error";
            case ErrorKind::Semantic: return "semantic error";
            case ErrorKind::Csv: return "csv error";
            case ErrorKind::Schema: return "schema error";
            case ErrorKind::Catalog: return "catalog error";
            case ErrorKind::Io: return "io error";
        }
        return "error";
    }

    static std::string format(ErrorKind kind, const std::string& msg,
                              std::optional<std::size_t> pos,
                              std::optional<std::size_t> line) {
        std::string out = kindName(kind);
        if (line) out += " (line " + std::to_string(*line) + ")";
        if (pos) out += " at position " + std::to_string(*pos);
        out += ": " + msg;
        return out;
    }

    ErrorKind kind_;
    std::optional<std::size_t> position_;
    std::optional<std::size_t> line_;
};

}  // namespace minisql
