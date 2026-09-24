#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace tinysearch {

enum class QueryType {
    TERM,    // single term, e.g. machine
    PHRASE,  // quoted phrase, e.g. "machine learning"
    AND,
    OR,
    NOT,     // unary: right child is the negated subtree
};

struct QueryNode {
    QueryType type;
    std::string value;                    // TERM: the term. PHRASE: unused (see phrase_terms)
    std::vector<std::string> phrase_terms; // PHRASE: the ordered terms in the phrase
    std::unique_ptr<QueryNode> left;       // AND/OR: left operand. unused otherwise.
    std::unique_ptr<QueryNode> right;      // AND/OR: right operand. NOT: the operand.

    static std::unique_ptr<QueryNode> make_term(std::string term) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryType::TERM;
        n->value = std::move(term);
        return n;
    }
    static std::unique_ptr<QueryNode> make_phrase(std::vector<std::string> terms) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryType::PHRASE;
        n->phrase_terms = std::move(terms);
        return n;
    }
    static std::unique_ptr<QueryNode> make_not(std::unique_ptr<QueryNode> operand) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryType::NOT;
        n->right = std::move(operand);
        return n;
    }
    static std::unique_ptr<QueryNode> make_and(std::unique_ptr<QueryNode> l,
                                                std::unique_ptr<QueryNode> r) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryType::AND;
        n->left = std::move(l);
        n->right = std::move(r);
        return n;
    }
    static std::unique_ptr<QueryNode> make_or(std::unique_ptr<QueryNode> l,
                                               std::unique_ptr<QueryNode> r) {
        auto n = std::make_unique<QueryNode>();
        n->type = QueryType::OR;
        n->left = std::move(l);
        n->right = std::move(r);
        return n;
    }
};

// Thrown by the parser/lexer on malformed queries. Carries a short,
// user-facing message suitable for a 400 response.
class QueryParseError : public std::runtime_error {
public:
    explicit QueryParseError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace tinysearch
