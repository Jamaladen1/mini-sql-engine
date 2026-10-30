#include "minisql/parser.hpp"

#include <vector>

#include "minisql/error.hpp"
#include "minisql/lexer.hpp"

namespace minisql {

std::string_view compareOpSymbol(CompareOp op) noexcept {
    switch (op) {
        case CompareOp::Eq: return "=";
        case CompareOp::Ne: return "!=";
        case CompareOp::Lt: return "<";
        case CompareOp::Le: return "<=";
        case CompareOp::Gt: return ">";
        case CompareOp::Ge: return ">=";
    }
    return "?";
}

namespace {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Statement parseStatement() {
        Statement stmt;
        if (peek().type == TokenType::Explain) {
            advance();
            stmt.explain = true;
        }
        stmt.select = parseSelect();
        if (peek().type == TokenType::Semicolon) advance();
        if (peek().type != TokenType::End) {
            unexpected("end of query");
        }
        return stmt;
    }

private:
    const Token& peek() const { return tokens_[pos_]; }

    const Token& advance() {
        const Token& t = tokens_[pos_];
        if (pos_ + 1 < tokens_.size()) ++pos_;
        return t;
    }

    [[noreturn]] void unexpected(std::string_view expected) const {
        const Token& t = peek();
        if (t.type == TokenType::Unsupported) {
            throw Error(ErrorKind::Syntax, "unsupported SQL feature '" + t.text + "'",
                        t.position);
        }
        std::string found = (t.type == TokenType::End)
                                ? "end of input"
                                : std::string(tokenTypeName(t.type)) + " '" + t.text + "'";
        throw Error(ErrorKind::Syntax,
                    "expected " + std::string(expected) + " but found " + found, t.position);
    }

    const Token& expect(TokenType type, std::string_view what) {
        if (peek().type != type) unexpected(what);
        return advance();
    }

    Identifier parseIdentifier(std::string_view what) {
        const Token& t = expect(TokenType::Identifier, what);
        return Identifier{t.text, t.position};
    }

    SelectStmt parseSelect() {
        SelectStmt s;
        expect(TokenType::Select, "SELECT");

        if (peek().type == TokenType::Star) {
            advance();
            s.selectStar = true;
        } else {
            s.projection.push_back(parseIdentifier("column name or '*'"));
            while (peek().type == TokenType::Comma) {
                advance();
                s.projection.push_back(parseIdentifier("column name"));
            }
        }

        expect(TokenType::From, "FROM");
        s.table = parseIdentifier("table name");

        if (peek().type == TokenType::Where) {
            advance();
            s.where.push_back(parsePredicate());
            while (peek().type == TokenType::And) {
                advance();
                s.where.push_back(parsePredicate());
            }
        }

        if (peek().type == TokenType::Order) {
            advance();
            expect(TokenType::By, "BY after ORDER");
            OrderBy ob;
            ob.column = parseIdentifier("column name after ORDER BY");
            if (peek().type == TokenType::Asc) {
                advance();
            } else if (peek().type == TokenType::Desc) {
                advance();
                ob.descending = true;
            }
            s.orderBy = std::move(ob);
        }

        if (peek().type == TokenType::Limit) {
            advance();
            const Token& t = expect(TokenType::Integer, "integer after LIMIT");
            if (t.intValue < 0) {
                throw Error(ErrorKind::Syntax, "LIMIT must be a nonnegative integer", t.position);
            }
            s.limit = static_cast<std::uint64_t>(t.intValue);
        }
        return s;
    }

    Predicate parsePredicate() {
        Predicate p;
        p.column = parseIdentifier("column name in WHERE");
        const Token& opTok = peek();
        switch (opTok.type) {
            case TokenType::Eq: p.op = CompareOp::Eq; break;
            case TokenType::Ne: p.op = CompareOp::Ne; break;
            case TokenType::Lt: p.op = CompareOp::Lt; break;
            case TokenType::Le: p.op = CompareOp::Le; break;
            case TokenType::Gt: p.op = CompareOp::Gt; break;
            case TokenType::Ge: p.op = CompareOp::Ge; break;
            default: unexpected("comparison operator (=, !=, <, <=, >, >=)");
        }
        advance();
        const Token& lit = peek();
        p.literalPosition = lit.position;
        if (lit.type == TokenType::Integer) {
            p.literal = lit.intValue;
        } else if (lit.type == TokenType::String) {
            p.literal = lit.text;
        } else {
            unexpected("integer or 'string' literal");
        }
        advance();
        return p;
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

}  // namespace

Statement parse(std::string_view sql) {
    return Parser(tokenize(sql)).parseStatement();
}

}  // namespace minisql
