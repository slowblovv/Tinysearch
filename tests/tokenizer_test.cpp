#include "mini_test.h"
#include "tinysearch/tokenizer.h"

using tinysearch::Tokenizer;

TEST_CASE(tokenizer_basic_punctuation) {
    auto toks = Tokenizer::tokenize("Machine learning is amazing!");
    std::vector<std::string> expected = {"Machine", "learning", "is", "amazing"};
    EXPECT_EQ(toks.size(), expected.size());
    for (std::size_t i = 0; i < expected.size() && i < toks.size(); ++i) {
        EXPECT_EQ(toks[i], expected[i]);
    }
}

TEST_CASE(tokenizer_repeated_whitespace) {
    auto toks = Tokenizer::tokenize("hello     world\t\tfoo\n\nbar");
    EXPECT_EQ(toks.size(), static_cast<size_t>(4));
}

TEST_CASE(tokenizer_numbers) {
    auto toks = Tokenizer::tokenize("BM25 uses k1=1.2 and b=0.75");
    // "1.2" splits on '.' into "1" and "2"; numbers are word bytes.
    bool has_bm25 = false;
    for (auto& t : toks) if (t == "BM25") has_bm25 = true;
    EXPECT_TRUE(has_bm25);
}

TEST_CASE(tokenizer_empty_string) {
    auto toks = Tokenizer::tokenize("");
    EXPECT_EQ(toks.size(), static_cast<size_t>(0));
}

TEST_CASE(tokenizer_only_punctuation) {
    auto toks = Tokenizer::tokenize("!!! ... ???");
    EXPECT_EQ(toks.size(), static_cast<size_t>(0));
}

TEST_CASE(tokenizer_utf8_kept_together) {
    // "café" — the multi-byte 'é' should not split the token in two.
    auto toks = Tokenizer::tokenize("café world");
    EXPECT_EQ(toks.size(), static_cast<size_t>(2));
    EXPECT_EQ(toks[0], std::string("caf\xC3\xA9"));
}

TEST_CASE(tokenizer_uppercase_preserved_pre_normalization) {
    auto toks = Tokenizer::tokenize("MACHINE Learning");
    EXPECT_EQ(toks[0], std::string("MACHINE"));
}
