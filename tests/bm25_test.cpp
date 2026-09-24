#include "mini_test.h"
#include "tinysearch/scorer.h"

using namespace tinysearch;

TEST_CASE(bm25_zero_tf_scores_zero) {
    Bm25Params params;
    double idf = Scorer::idf_bm25(100, 10);
    EXPECT_EQ(Scorer::bm25_score(0, idf, 50, 40.0, params), 0.0);
}

TEST_CASE(bm25_missing_term_df_zero_scores_zero) {
    // A term with df=0 shouldn't happen in practice (postings_for returns
    // nullptr instead), but idf_bm25 should still degrade gracefully.
    double idf = Scorer::idf_bm25(100, 0);
    EXPECT_EQ(idf, 0.0);
}

TEST_CASE(bm25_score_increases_with_tf) {
    Bm25Params params;
    double idf = Scorer::idf_bm25(1000, 50);
    double low = Scorer::bm25_score(1, idf, 100, 100.0, params);
    double high = Scorer::bm25_score(10, idf, 100, 100.0, params);
    EXPECT_TRUE(high > low);
}

TEST_CASE(bm25_score_ordering_rarer_term_scores_higher) {
    // Same tf/doc length, but a rarer term (lower df) should score higher
    // due to a larger idf.
    Bm25Params params;
    double idf_common = Scorer::idf_bm25(1000, 900);
    double idf_rare = Scorer::idf_bm25(1000, 5);
    double score_common = Scorer::bm25_score(3, idf_common, 100, 100.0, params);
    double score_rare = Scorer::bm25_score(3, idf_rare, 100, 100.0, params);
    EXPECT_TRUE(score_rare > score_common);
}

TEST_CASE(bm25_long_document_normalization) {
    // With b > 0, a longer-than-average document should be penalized
    // relative to an average-length one for the same raw term frequency.
    Bm25Params params;  // b = 0.75
    double idf = Scorer::idf_bm25(1000, 100);
    double avgdl = 100.0;
    double score_avg_len = Scorer::bm25_score(5, idf, 100, avgdl, params);
    double score_long = Scorer::bm25_score(5, idf, 500, avgdl, params);
    EXPECT_TRUE(score_avg_len > score_long);
}

TEST_CASE(bm25_b_zero_disables_length_normalization) {
    Bm25Params params;
    params.b = 0.0;
    double idf = Scorer::idf_bm25(1000, 100);
    double score_short = Scorer::bm25_score(5, idf, 10, 100.0, params);
    double score_long = Scorer::bm25_score(5, idf, 1000, 100.0, params);
    EXPECT_NEAR(score_short, score_long, 1e-9);
}

TEST_CASE(tfidf_zero_tf_scores_zero) {
    EXPECT_EQ(Scorer::tfidf_score(0, 5.0), 0.0);
}

TEST_CASE(tfidf_increases_with_idf) {
    double low = Scorer::tfidf_score(3, 1.0);
    double high = Scorer::tfidf_score(3, 5.0);
    EXPECT_TRUE(high > low);
}

TEST_CASE(idf_tfidf_matches_formula) {
    // log(N/df) + 1
    double idf = Scorer::idf_tfidf(100, 10);
    EXPECT_NEAR(idf, std::log(10.0) + 1.0, 1e-9);
}
