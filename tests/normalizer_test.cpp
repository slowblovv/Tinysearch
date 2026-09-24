#include "mini_test.h"
#include "tinysearch/normalizer.h"

using tinysearch::Normalizer;

TEST_CASE(normalizer_lowercase) {
    Normalizer n;
    EXPECT_EQ(n.normalize("MACHINE"), std::string("machine"));
}

TEST_CASE(normalizer_drops_stop_words) {
    Normalizer n;
    EXPECT_EQ(n.normalize("the"), std::string(""));
    EXPECT_EQ(n.normalize("is"), std::string(""));
    EXPECT_EQ(n.normalize("and"), std::string(""));
}

TEST_CASE(normalizer_keeps_content_words) {
    Normalizer n;
    EXPECT_FALSE(n.normalize("machine").empty());
}

TEST_CASE(normalizer_stemming_plurals) {
    Normalizer n;
    EXPECT_EQ(n.normalize("documents"), std::string("document"));
    EXPECT_EQ(n.normalize("studies"), std::string("study"));
}

TEST_CASE(normalizer_stemming_ing) {
    Normalizer n;
    EXPECT_EQ(n.normalize("running"), std::string("run"));
    EXPECT_EQ(n.normalize("learning"), std::string("learn"));
}

TEST_CASE(normalizer_stemming_ed_ly) {
    Normalizer n;
    EXPECT_EQ(n.normalize("learned"), std::string("learn"));
    EXPECT_EQ(n.normalize("quickly"), std::string("quick"));
}

TEST_CASE(normalizer_normalize_all_drops_empties) {
    Normalizer n;
    auto out = n.normalize_all({"The", "Machine", "is", "Learning"});
    EXPECT_EQ(out.size(), static_cast<size_t>(2));
}

TEST_CASE(normalizer_custom_stop_words) {
    Normalizer n({"custom"});
    EXPECT_EQ(n.normalize("custom"), std::string(""));
    EXPECT_FALSE(n.normalize("the").empty());  // no longer a stop word with a custom list
}
