#include "mini_test.h"
#include "tinysearch/normalizer.h"
#include "tinysearch/parser.h"

using namespace tinysearch;

namespace {
Normalizer g_norm;
}

TEST_CASE(parser_single_term) {
    QueryParser p(g_norm);
    auto ast = p.parse("machine");
    EXPECT_TRUE(ast->type == QueryType::TERM);
    EXPECT_EQ(ast->value, std::string("machine"));
}

TEST_CASE(parser_phrase) {
    QueryParser p(g_norm);
    auto ast = p.parse("\"machine learning\"");
    EXPECT_TRUE(ast->type == QueryType::PHRASE);
    EXPECT_EQ(ast->phrase_terms.size(), static_cast<size_t>(2));
}

TEST_CASE(parser_implicit_and) {
    QueryParser p(g_norm);
    auto ast = p.parse("machine learning");
    EXPECT_TRUE(ast->type == QueryType::AND);
}

TEST_CASE(parser_explicit_and_or_not) {
    QueryParser p(g_norm);
    EXPECT_TRUE(p.parse("machine AND learning")->type == QueryType::AND);
    EXPECT_TRUE(p.parse("machine OR learning")->type == QueryType::OR);
    EXPECT_TRUE(p.parse("machine NOT learning")->type == QueryType::AND);  // NOT binds to "learning"
}

TEST_CASE(parser_precedence_or_lowest) {
    // "A OR B AND C" == "A OR (B AND C)"
    QueryParser p(g_norm);
    auto ast = p.parse("a OR b AND c");
    EXPECT_TRUE(ast->type == QueryType::OR);
    EXPECT_TRUE(ast->right->type == QueryType::AND);
}

TEST_CASE(parser_precedence_not_highest) {
    // "a AND NOT b" == "a AND (NOT b)"
    QueryParser p(g_norm);
    auto ast = p.parse("a AND NOT b");
    EXPECT_TRUE(ast->type == QueryType::AND);
    EXPECT_TRUE(ast->right->type == QueryType::NOT);
}

TEST_CASE(parser_parentheses) {
    QueryParser p(g_norm);
    auto ast = p.parse("(a OR b) AND c");
    EXPECT_TRUE(ast->type == QueryType::AND);
    EXPECT_TRUE(ast->left->type == QueryType::OR);
}

TEST_CASE(parser_invalid_queries_throw) {
    QueryParser p(g_norm);
    EXPECT_THROW(p.parse("AND machine"), QueryParseError);
    EXPECT_THROW(p.parse("machine AND"), QueryParseError);
    EXPECT_THROW(p.parse("machine OR OR learning"), QueryParseError);
    EXPECT_THROW(p.parse("(machine"), QueryParseError);
    EXPECT_THROW(p.parse("machine)"), QueryParseError);
    EXPECT_THROW(p.parse("\"unfinished phrase"), QueryParseError);
    EXPECT_THROW(p.parse(""), QueryParseError);
    EXPECT_THROW(p.parse("   "), QueryParseError);
}

TEST_CASE(parser_query_too_long_throws) {
    QueryParser p(g_norm);
    std::string huge(5000, 'a');
    EXPECT_THROW(p.parse(huge), QueryParseError);
}
