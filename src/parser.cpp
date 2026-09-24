#include "tinysearch/parser.h"

#include <cctype>

#include "tinysearch/tokenizer.h"

namespace tinysearch {

namespace {
std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool is_word_start(unsigned char c) {
    return std::isalnum(c) || c >= 0x80;
}
}  // namespace

std::vector<QueryParser::LexToken> QueryParser::lex(const std::string& query_text) const {
    if (query_text.size() > kMaxQueryBytes) {
        throw QueryParseError("query too long (max " +
                               std::to_string(kMaxQueryBytes) + " bytes)");
    }

    std::vector<LexToken> tokens;
    std::size_t i = 0;
    const std::size_t n = query_text.size();

    while (i < n) {
        unsigned char c = static_cast<unsigned char>(query_text[i]);
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        if (c == '(') {
            tokens.push_back({TokType::LPAREN, "("});
            ++i;
            continue;
        }
        if (c == ')') {
            tokens.push_back({TokType::RPAREN, ")"});
            ++i;
            continue;
        }
        if (c == '"') {
            std::size_t j = i + 1;
            while (j < n && query_text[j] != '"') ++j;
            if (j >= n) {
                throw QueryParseError("unterminated phrase (missing closing \")");
            }
            std::string phrase = query_text.substr(i + 1, j - i - 1);
            tokens.push_back({TokType::PHRASE, phrase});
            i = j + 1;
            continue;
        }
        if (is_word_start(c)) {
            std::size_t j = i;
            while (j < n) {
                unsigned char cj = static_cast<unsigned char>(query_text[j]);
                if (std::isspace(cj) || cj == '(' || cj == ')' || cj == '"') break;
                ++j;
            }
            std::string word = query_text.substr(i, j - i);
            std::string w = upper(word);
            if (w == "AND") {
                tokens.push_back({TokType::AND, word});
            } else if (w == "OR") {
                tokens.push_back({TokType::OR, word});
            } else if (w == "NOT") {
                tokens.push_back({TokType::NOT, word});
            } else {
                tokens.push_back({TokType::TERM, word});
            }
            i = j;
            continue;
        }
        // Any other punctuation byte on its own is just skipped as a
        // separator (mirrors Tokenizer's treatment of punctuation).
        ++i;
    }
    tokens.push_back({TokType::END, ""});
    return tokens;
}

bool QueryParser::starts_primary(TokType t) {
    return t == TokType::TERM || t == TokType::PHRASE || t == TokType::LPAREN ||
           t == TokType::NOT;
}

std::unique_ptr<QueryNode> QueryParser::parse(const std::string& query_text) const {
    std::string trimmed = query_text;
    // trim
    std::size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        throw QueryParseError("empty query");
    }

    std::vector<LexToken> tokens = lex(query_text);
    ParseState st{tokens, 0};
    auto ast = parse_or(st);
    if (st.tokens[st.pos].type != TokType::END) {
        throw QueryParseError("unexpected token near '" + st.tokens[st.pos].text + "'");
    }
    return ast;
}

std::unique_ptr<QueryNode> QueryParser::parse_or(ParseState& st) const {
    auto left = parse_and(st);
    while (st.tokens[st.pos].type == TokType::OR) {
        ++st.pos;
        if (!starts_primary(st.tokens[st.pos].type)) {
            throw QueryParseError("expected expression after OR");
        }
        auto right = parse_and(st);
        left = QueryNode::make_or(std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<QueryNode> QueryParser::parse_and(ParseState& st) const {
    auto left = parse_not(st);
    for (;;) {
        TokType t = st.tokens[st.pos].type;
        if (t == TokType::AND) {
            ++st.pos;
            if (!starts_primary(st.tokens[st.pos].type)) {
                throw QueryParseError("expected expression after AND");
            }
            auto right = parse_not(st);
            left = QueryNode::make_and(std::move(left), std::move(right));
        } else if (starts_primary(t)) {
            // implicit AND: "machine learning" == "machine AND learning"
            auto right = parse_not(st);
            left = QueryNode::make_and(std::move(left), std::move(right));
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<QueryNode> QueryParser::parse_not(ParseState& st) const {
    if (st.tokens[st.pos].type == TokType::NOT) {
        ++st.pos;
        if (!starts_primary(st.tokens[st.pos].type) ||
            st.tokens[st.pos].type == TokType::NOT) {
            // NOT NOT / NOT <end> are both rejected as ambiguous/pointless
            if (st.tokens[st.pos].type != TokType::NOT) {
                throw QueryParseError("expected expression after NOT");
            }
        }
        auto operand = parse_not(st);
        return QueryNode::make_not(std::move(operand));
    }
    return parse_primary(st);
}

std::unique_ptr<QueryNode> QueryParser::parse_primary(ParseState& st) const {
    const LexToken& tok = st.tokens[st.pos];
    switch (tok.type) {
        case TokType::TERM: {
            ++st.pos;
            std::string normalized = normalizer_.normalize(tok.text);
            return QueryNode::make_term(normalized);
        }
        case TokType::PHRASE: {
            ++st.pos;
            auto raw_terms = Tokenizer::tokenize(tok.text);
            std::vector<std::string> terms;
            for (auto& t : raw_terms) {
                std::string n = normalizer_.normalize(t);
                if (!n.empty()) terms.push_back(std::move(n));
            }
            if (terms.size() > kMaxPhraseTerms) {
                throw QueryParseError("phrase too long (max " +
                                       std::to_string(kMaxPhraseTerms) + " terms)");
            }
            if (terms.empty()) {
                throw QueryParseError("phrase has no searchable terms");
            }
            return QueryNode::make_phrase(std::move(terms));
        }
        case TokType::LPAREN: {
            ++st.pos;
            auto inner = parse_or(st);
            if (st.tokens[st.pos].type != TokType::RPAREN) {
                throw QueryParseError("expected closing ')'");
            }
            ++st.pos;
            return inner;
        }
        case TokType::AND:
        case TokType::OR:
            throw QueryParseError("unexpected '" + tok.text + "'");
        case TokType::RPAREN:
            throw QueryParseError("unexpected ')'");
        case TokType::NOT:
            // handled in parse_not; reaching here means "NOT" with nothing
            // meaningful before it was mis-routed — treat as error.
            throw QueryParseError("unexpected NOT");
        case TokType::END:
            throw QueryParseError("unexpected end of query");
    }
    throw QueryParseError("internal parser error");
}

}  // namespace tinysearch
