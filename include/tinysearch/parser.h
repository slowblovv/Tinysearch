#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tinysearch/normalizer.h"
#include "tinysearch/query.h"

namespace tinysearch {

// Recursive-descent parser for the TinySearch query language.
//
// Grammar (NOT binds tighter than AND, which binds tighter than OR — see
// spec section 63, and docs/architecture.md for worked examples):
//
//   or_expr   := and_expr ( "OR" and_expr )*
//   and_expr  := not_expr ( ("AND")? not_expr )*      // implicit AND
//   not_expr  := "NOT" not_expr | primary
//   primary   := TERM | PHRASE | "(" or_expr ")"
//
// Term/phrase values are normalized (lowercased, stopword-filtered,
// stemmed) with the same Normalizer used at index time, so queries and the
// index speak the same vocabulary.
class QueryParser {
public:
    explicit QueryParser(const Normalizer& normalizer) : normalizer_(normalizer) {}

    // Throws QueryParseError on malformed input.
    std::unique_ptr<QueryNode> parse(const std::string& query_text) const;

    static constexpr std::size_t kMaxQueryBytes = 4096;
    static constexpr std::size_t kMaxPhraseTerms = 32;

private:
    enum class TokType { TERM, PHRASE, AND, OR, NOT, LPAREN, RPAREN, END };
    struct LexToken {
        TokType type;
        std::string text;              // TERM: raw word. PHRASE: raw phrase content.
    };

    const Normalizer& normalizer_;

    std::vector<LexToken> lex(const std::string& query_text) const;

    // Parser state carried through the recursive-descent helpers.
    struct ParseState {
        const std::vector<LexToken>& tokens;
        std::size_t pos = 0;
    };
    std::unique_ptr<QueryNode> parse_or(ParseState& st) const;
    std::unique_ptr<QueryNode> parse_and(ParseState& st) const;
    std::unique_ptr<QueryNode> parse_not(ParseState& st) const;
    std::unique_ptr<QueryNode> parse_primary(ParseState& st) const;

    static bool starts_primary(TokType t);
};

}  // namespace tinysearch
