#include "mini_test.h"
#include "tinysearch/index.h"

using namespace tinysearch;

namespace {
DocumentMeta make_meta(const std::string& id, const std::string& title) {
    DocumentMeta m;
    m.id = id;
    m.path = "/tmp/" + id;
    m.title = title;
    return m;
}
}  // namespace

TEST_CASE(index_insert_and_lookup) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"machine", "learn", "machine"});
    const PostingList* pl = idx.postings_for("machine");
    EXPECT_TRUE(pl != nullptr);
    EXPECT_EQ(pl->postings.size(), static_cast<size_t>(1));
    EXPECT_EQ(pl->postings[0].term_frequency, static_cast<uint32_t>(2));
}

TEST_CASE(index_positions_recorded) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"machine", "learn", "machine"});
    const PostingList* pl = idx.postings_for("machine");
    EXPECT_EQ(pl->postings[0].positions.size(), static_cast<size_t>(2));
    EXPECT_EQ(pl->postings[0].positions[0], static_cast<uint32_t>(0));
    EXPECT_EQ(pl->postings[0].positions[1], static_cast<uint32_t>(2));
}

TEST_CASE(index_document_frequency) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"machine", "learn"});
    idx.add_document(make_meta("d2", "Doc 2"), {"machine"});
    idx.add_document(make_meta("d3", "Doc 3"), {"database"});
    EXPECT_EQ(idx.postings_for("machine")->document_frequency, static_cast<uint32_t>(2));
    EXPECT_EQ(idx.postings_for("database")->document_frequency, static_cast<uint32_t>(1));
    EXPECT_TRUE(idx.postings_for("nonexistent") == nullptr);
}

TEST_CASE(index_multiple_docs_and_stats) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"a", "b", "c"});
    idx.add_document(make_meta("d2", "Doc 2"), {"a", "b"});
    EXPECT_EQ(idx.stats().document_count, static_cast<uint64_t>(2));
    EXPECT_EQ(idx.stats().total_tokens, static_cast<uint64_t>(5));
    EXPECT_NEAR(idx.stats().average_document_length, 2.5, 1e-9);
}

TEST_CASE(index_remove_document) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"machine"});
    idx.add_document(make_meta("d2", "Doc 2"), {"machine"});
    EXPECT_TRUE(idx.remove_document("d1"));
    EXPECT_FALSE(idx.contains("d1"));
    EXPECT_EQ(idx.postings_for("machine")->document_frequency, static_cast<uint32_t>(1));
    EXPECT_EQ(idx.stats().document_count, static_cast<uint64_t>(1));
}

TEST_CASE(index_update_existing_document) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"machine"});
    idx.add_document(make_meta("d1", "Doc 1 v2"), {"database"});
    EXPECT_TRUE(idx.postings_for("machine") == nullptr ||
                idx.postings_for("machine")->document_frequency == 0);
    EXPECT_EQ(idx.postings_for("database")->document_frequency, static_cast<uint32_t>(1));
    EXPECT_EQ(idx.stats().document_count, static_cast<uint64_t>(1));
    EXPECT_EQ(idx.meta_for_stable_id("d1")->title, std::string("Doc 1 v2"));
}

TEST_CASE(index_duplicate_term_within_doc_counted_once_in_df) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1"), {"a", "a", "a"});
    EXPECT_EQ(idx.postings_for("a")->document_frequency, static_cast<uint32_t>(1));
    EXPECT_EQ(idx.postings_for("a")->postings[0].term_frequency, static_cast<uint32_t>(3));
}
