#include <filesystem>
#include <fstream>

#include "mini_test.h"
#include "tinysearch/config.h"
#include "tinysearch/search_engine.h"

using namespace tinysearch;
namespace fs = std::filesystem;

namespace {

void write_file(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

std::string setup_corpus(const std::string& tag) {
    std::string dir = "/tmp/tinysearch_search_test_" + tag + "/documents";
    fs::remove_all(fs::path(dir).parent_path());
    write_file(dir + "/ml.md",
               "# Introduction to Machine Learning\n\n"
               "Machine learning is a subfield of artificial intelligence. Machine "
               "learning systems learn patterns from data without explicit rules.\n");
    write_file(dir + "/db.md",
               "# Databases\n\n"
               "A database stores structured data. PostgreSQL is a relational "
               "database used widely in backend systems.\n");
    write_file(dir + "/deep.txt",
               "Deep learning is a type of machine learning based on neural "
               "networks with many layers. Deep learning powers modern computer "
               "vision and natural language processing.\n");
    return dir;
}

}  // namespace

TEST_CASE(search_single_term_returns_matching_docs) {
    std::string dir = setup_corpus("single_term");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    auto resp = engine.search("database", RankerType::BM25, 10);
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(1));
    EXPECT_EQ(resp.results[0].title, std::string("Databases"));
}

TEST_CASE(search_implicit_and_requires_all_terms) {
    std::string dir = setup_corpus("implicit_and");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    // "machine" appears in ml.md and deep.txt; "postgresql" only in db.md.
    // The AND of the two should match nothing.
    auto resp = engine.search("machine postgresql", RankerType::BM25, 10);
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(0));
}

TEST_CASE(search_or_is_a_superset_of_and) {
    std::string dir = setup_corpus("or_superset");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    auto and_resp = engine.search("machine AND database", RankerType::BM25, 10);
    auto or_resp = engine.search("machine OR database", RankerType::BM25, 10);
    EXPECT_TRUE(or_resp.results.size() >= and_resp.results.size());
    EXPECT_EQ(or_resp.results.size(), static_cast<size_t>(3));
}

TEST_CASE(search_not_excludes_documents) {
    std::string dir = setup_corpus("not_excludes");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    auto resp = engine.search("learning NOT database", RankerType::BM25, 10);
    for (auto& r : resp.results) {
        EXPECT_TRUE(r.title != std::string("Databases"));
    }
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(2));
}

TEST_CASE(search_phrase_requires_adjacency) {
    std::string dir = setup_corpus("phrase_adjacency");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    // "machine learning" appears contiguously in this corpus; "learning
    // database" never does ("learning" only appears in ml.md/deep.txt,
    // which never mention "database"), so it must produce zero phrase
    // matches, unlike a plain AND of the two terms.
    auto hit = engine.search("\"machine learning\"", RankerType::BM25, 10);
    auto phrase_miss = engine.search("\"learning database\"", RankerType::BM25, 10);
    EXPECT_TRUE(hit.results.size() >= 1);
    EXPECT_EQ(phrase_miss.results.size(), static_cast<size_t>(0));
}

TEST_CASE(search_top_k_limits_results) {
    std::string dir = setup_corpus("top_k");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);

    auto resp = engine.search("learning OR database", RankerType::BM25, 1);
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(1));
    EXPECT_TRUE(resp.total_candidates >= 1);
}

TEST_CASE(search_invalid_query_throws) {
    std::string dir = setup_corpus("invalid_query");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);
    EXPECT_THROW(engine.search("machine AND", RankerType::BM25, 10), QueryParseError);
}

TEST_CASE(search_empty_index_returns_no_results_not_a_crash) {
    Config cfg = Config::defaults();
    SearchEngine engine(cfg);
    auto resp = engine.search("anything", RankerType::BM25, 10);
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(0));
    EXPECT_EQ(resp.total_candidates, static_cast<size_t>(0));
}

TEST_CASE(search_incremental_reindex_detects_unchanged) {
    std::string dir = setup_corpus("incremental");
    SearchEngine engine(Config::defaults());
    auto r1 = engine.index_directory(dir);
    EXPECT_EQ(r1.added, static_cast<size_t>(3));
    auto r2 = engine.index_directory(dir);
    EXPECT_EQ(r2.added, static_cast<size_t>(0));
    EXPECT_EQ(r2.unchanged, static_cast<size_t>(3));
}

TEST_CASE(search_incremental_detects_deletion) {
    std::string dir = setup_corpus("deletion");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);
    fs::remove(dir + "/db.md");
    auto r = engine.index_directory(dir);
    EXPECT_EQ(r.deleted, static_cast<size_t>(1));
    auto resp = engine.search("database", RankerType::BM25, 10);
    EXPECT_EQ(resp.results.size(), static_cast<size_t>(0));
}

TEST_CASE(search_cache_hit_on_repeated_query) {
    std::string dir = setup_corpus("cache_hit");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);
    auto first = engine.search("machine learning", RankerType::BM25, 10);
    auto second = engine.search("machine learning", RankerType::BM25, 10);
    EXPECT_FALSE(first.cache_hit);
    EXPECT_TRUE(second.cache_hit);
}

TEST_CASE(search_bm25_and_tfidf_can_disagree_on_order) {
    // Not a strict requirement that they always differ, but both rankers
    // must at least produce a valid, non-empty ranking for the same query.
    std::string dir = setup_corpus("ranker_compare");
    SearchEngine engine(Config::defaults());
    engine.index_directory(dir);
    auto bm25 = engine.search("learning", RankerType::BM25, 10);
    auto tfidf = engine.search("learning", RankerType::TF_IDF, 10);
    EXPECT_TRUE(!bm25.results.empty());
    EXPECT_TRUE(!tfidf.results.empty());
    EXPECT_EQ(bm25.results.size(), tfidf.results.size());
}
