// Interactive REPL for the Mini SQL Query Engine.
//
// All commands run in one process and share one in-memory catalog.
//   minisql                      interactive REPL (or reads commands from piped stdin)
//   minisql -e "<cmd>" [-e ...]  run commands in order, then exit
//   minisql -f script.sql        run commands from a file, then exit

#include <cstdio>
#include <fstream>
#include <optional>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "minisql/csv.hpp"
#include "minisql/engine.hpp"
#include "minisql/error.hpp"
#include "minisql/parser.hpp"

namespace {

using namespace minisql;

const char* kHelp =
    "Commands:\n"
    "  load <table> <file.csv> --schema <schema.json>   load a CSV with a JSON schema\n"
    "  load <table> <file.csv> --columns id:INT64,name:TEXT\n"
    "  index <table> <column>          build a hash index (alias: create-index)\n"
    "  query <SELECT ...>              run a query (a bare SELECT line also works)\n"
    "  explain <SELECT ...>            show the execution plan (bare EXPLAIN also works)\n"
    "  tables                          list loaded tables\n"
    "  schema <table>                  show columns, types and indexes\n"
    "  help                            show this help\n"
    "  quit | exit                     leave\n"
    "Lines starting with '#' or '--' are comments.\n"
    "A query cut off mid-statement (e.g. 'SELECT id,') continues on the next\n"
    "lines until one ends with ';'. A blank line ends it early.\n";

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

// Splits on whitespace; double quotes group words (for paths with spaces).
std::vector<std::string> splitArgs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false, have = false;
    for (char c : line) {
        if (c == '"') { inQuote = !inQuote; have = true; continue; }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (have) { out.push_back(cur); cur.clear(); have = false; }
            continue;
        }
        cur.push_back(c);
        have = true;
    }
    if (inQuote) throw Error(ErrorKind::Syntax, "unterminated double quote in command");
    if (have) out.push_back(cur);
    return out;
}

class Repl {
public:
    // Returns false when the session should end.
    bool handle(const std::string& rawLine) {
        const std::string line = trim(rawLine);
        const bool comment = !line.empty() && (line[0] == '#' || line.rfind("--", 0) == 0);

        if (!pending_.empty()) {
            if (comment) return true;
            if (line.empty()) {
                // A blank line ends an unfinished statement; report why.
                return dispatch(takePending());
            }
            pending_ += "\n" + line;
            // A multi-line statement ends at ';' (outside a string literal),
            // so a complete prefix like "SELECT * FROM t" can still be
            // followed by a WHERE line.
            if (line.back() == ';' && !hasOpenString(pending_)) return dispatch(takePending());
            return true;
        }

        if (line.empty() || comment) return true;
        if (isIncompleteSql(line)) {
            pending_ = line;
            return true;
        }
        return dispatch(line);
    }

    // Runs whatever is still buffered (called at end of input).
    void finish() {
        if (!pending_.empty()) dispatch(takePending());
    }

    bool continuing() const { return !pending_.empty(); }
    bool failed() const { return failed_; }

private:
    std::string takePending() {
        std::string s = std::move(pending_);
        pending_.clear();
        return s;
    }

    // Single quotes come in pairs ('' is an escaped quote), so an odd
    // count means a string literal is still open.
    static bool hasOpenString(const std::string& text) {
        std::size_t quotes = 0;
        for (char c : text) quotes += (c == '\'') ? 1 : 0;
        return quotes % 2 == 1;
    }

    // Splits "<cmd> <rest>" and returns the SQL text a SQL command carries,
    // or nullopt for non-SQL commands.
    static std::optional<std::string> sqlOf(const std::string& line) {
        const auto sp = line.find_first_of(" \t\n");
        const std::string cmd = lower(line.substr(0, sp));
        const std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp + 1));
        if (cmd == "select") return line;
        if (cmd == "explain") return "EXPLAIN " + rest;
        if (cmd == "query") return rest;
        return std::nullopt;
    }

    // True when the SQL stops partway through a statement, so more input
    // could complete it: the parser hit end of input, or a string literal
    // is still open. Real errors (bad tokens, unknown tables) are not
    // "incomplete" and are reported straight away.
    static bool isIncompleteSql(const std::string& line) {
        const auto sql = sqlOf(line);
        if (!sql) return false;
        const std::string text = trim(*sql);
        if (text.empty()) return false;
        try {
            (void)parse(text);
            return false;
        } catch (const Error& e) {
            if (e.kind() != ErrorKind::Syntax) return false;
            if (e.position() && *e.position() >= text.size()) return true;
            return std::string(e.what()).find("unterminated string literal") != std::string::npos;
        }
    }

    bool dispatch(const std::string& line) {

        const auto sp = line.find_first_of(" \t\n");
        const std::string cmd = lower(line.substr(0, sp));
        const std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp + 1));

        try {
            if (cmd == "quit" || cmd == "exit" || cmd == "\\q") return false;
            if (cmd == "help") { std::cout << kHelp; return true; }
            if (cmd == "select") { runSql(line); return true; }
            if (cmd == "explain") {
                // "explain SELECT ..." and "EXPLAIN SELECT ..." are the same thing.
                runSql("EXPLAIN " + rest);
                return true;
            }
            if (cmd == "query") { runSql(rest); return true; }
            if (cmd == "load") { load(splitArgs(rest)); return true; }
            if (cmd == "index" || cmd == "create-index") { index(splitArgs(rest)); return true; }
            if (cmd == "tables") { tables(); return true; }
            if (cmd == "schema") { schema(splitArgs(rest)); return true; }
            throw Error(ErrorKind::Syntax, "unknown command '" + cmd + "' (type 'help')");
        } catch (const Error& e) {
            failed_ = true;
            std::cout << "ERROR: " << e.what() << "\n";
        } catch (const std::exception& e) {
            failed_ = true;
            std::cout << "ERROR: internal: " << e.what() << "\n";
        }
        return true;
    }

    void runSql(const std::string& sql) {
        if (sql.empty()) throw Error(ErrorKind::Syntax, "missing SQL text");
        const QueryOutcome out = engine_.run(sql);
        if (out.explain) {
            std::cout << out.planText;
        } else {
            std::cout << renderResult(out.result);
        }
        std::printf("Time: %.3f ms\n", out.elapsedMs);
        std::fflush(stdout);
    }

    void load(const std::vector<std::string>& a) {
        if (a.size() != 4 || (a[2] != "--schema" && a[2] != "--columns")) {
            throw Error(ErrorKind::Syntax,
                        "usage: load <table> <file.csv> --schema <schema.json> | "
                        "--columns name:TYPE,...");
        }
        const auto cols = a[2] == "--schema" ? loadSchemaFile(a[3]) : parseColumnSpec(a[3]);
        engine_.loadCsv(a[0], a[1], cols);
        std::cout << "Loaded " << engine_.catalog().find(a[0])->rowCount() << " rows into '"
                  << a[0] << "'\n";
    }

    void index(const std::vector<std::string>& a) {
        if (a.size() != 2) throw Error(ErrorKind::Syntax, "usage: index <table> <column>");
        engine_.createIndex(a[0], a[1]);
        std::cout << "Created hash index on " << a[0] << "(" << a[1] << ")\n";
    }

    void tables() {
        const auto names = engine_.catalog().tableNames();
        if (names.empty()) { std::cout << "(no tables)\n"; return; }
        for (const auto& n : names) {
            std::cout << n << " (" << engine_.catalog().find(n)->rowCount() << " rows)\n";
        }
    }

    void schema(const std::vector<std::string>& a) {
        if (a.size() != 1) throw Error(ErrorKind::Syntax, "usage: schema <table>");
        const Table* t = engine_.catalog().find(a[0]);
        if (!t) throw Error(ErrorKind::Catalog, "unknown table '" + a[0] + "'");
        std::cout << "Table " << t->name() << " (" << t->rowCount() << " rows)\n";
        const auto indexed = t->indexedColumns();
        for (std::size_t i = 0; i < t->columns().size(); ++i) {
            const auto& c = t->columns()[i];
            std::cout << "  " << c.name << " " << typeName(c.type)
                      << (t->indexOn(i) ? "  [hash index]" : "") << "\n";
        }
    }

    Engine engine_;
    bool failed_ = false;
    std::string pending_;  // unfinished multi-line SQL statement
};

int runStream(Repl& repl, std::istream& in, bool interactive) {
    std::string line;
    while (true) {
        if (interactive) {
            std::cout << (repl.continuing() ? "   ...> " : "minisql> ") << std::flush;
        }
        if (!std::getline(in, line)) {
            repl.finish();
            break;
        }
        if (!repl.handle(line)) break;
    }
    if (interactive) std::cout << "\n";
    return repl.failed() && !interactive ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Repl repl;
    std::vector<std::string> commands;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-e" || arg == "-f") && i + 1 < argc) {
            (arg == "-e" ? commands : files).push_back(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: minisql [-e \"command\"]... [-f script]...\n\n" << kHelp;
            return 0;
        } else {
            std::cerr << "unknown argument '" << arg << "' (try --help)\n";
            return 2;
        }
    }

    if (commands.empty() && files.empty()) {
        const bool interactive = isatty(STDIN_FILENO) != 0;
        if (interactive) std::cout << "Mini SQL Query Engine. Type 'help' for commands.\n";
        return runStream(repl, std::cin, interactive);
    }
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in) {
            std::cerr << "cannot open script '" << f << "'\n";
            return 2;
        }
        runStream(repl, in, false);
    }
    for (const auto& c : commands) {
        if (!repl.handle(c)) break;
    }
    repl.finish();
    return repl.failed() ? 1 : 0;
}
