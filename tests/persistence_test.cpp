#include <filesystem>
#include <fstream>

#include "mini_test.h"
#include "tinysearch/index_store.h"

using namespace tinysearch;
namespace fs = std::filesystem;

namespace {
DocumentMeta make_meta(const std::string& id, const std::string& title, const std::string& path) {
    DocumentMeta m;
    m.id = id;
    m.title = title;
    m.path = path;
    m.token_count = 3;
    m.modified_time = 1234;
    return m;
}
}  // namespace

TEST_CASE(persistence_round_trip) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1", "/tmp/d1.md"), {"machine", "learning", "machine"});
    idx.add_document(make_meta("d2", "Doc 2", "/tmp/d2.md"), {"database", "systems"});

    std::string path = "/tmp/tinysearch_persist_test/index.dat";
    fs::remove_all("/tmp/tinysearch_persist_test");
    IndexStore::save(idx, path);
    EXPECT_TRUE(fs::exists(path));
    EXPECT_FALSE(fs::exists(path + ".tmp"));  // temp file must not linger

    InvertedIndex loaded;
    IndexStore::load(loaded, path);
    EXPECT_EQ(loaded.document_count(), static_cast<uint32_t>(2));
    EXPECT_EQ(loaded.postings_for("machine")->postings[0].term_frequency, static_cast<uint32_t>(2));
    EXPECT_EQ(loaded.stats().document_count, static_cast<uint64_t>(2));
}

TEST_CASE(persistence_missing_file_throws) {
    InvertedIndex idx;
    EXPECT_THROW(IndexStore::load(idx, "/tmp/tinysearch_persist_test/does_not_exist.dat"),
                 IndexLoadError);
}

TEST_CASE(persistence_bad_magic_throws) {
    std::string path = "/tmp/tinysearch_persist_test/garbage.dat";
    std::ofstream(path, std::ios::binary) << "NOTANINDEXFILE";
    InvertedIndex idx;
    EXPECT_THROW(IndexStore::load(idx, path), IndexLoadError);
}

TEST_CASE(persistence_unsupported_version_throws) {
    std::string path = "/tmp/tinysearch_persist_test/futurever.dat";
    std::ofstream out(path, std::ios::binary);
    out.write("TSIX", 4);
    std::uint32_t future_version = 999;
    out.write(reinterpret_cast<char*>(&future_version), 4);
    out.close();
    InvertedIndex idx;
    EXPECT_THROW(IndexStore::load(idx, path), IndexLoadError);
}

TEST_CASE(persistence_skips_tombstoned_documents) {
    InvertedIndex idx;
    idx.add_document(make_meta("d1", "Doc 1", "/tmp/d1.md"), {"machine"});
    idx.add_document(make_meta("d2", "Doc 2", "/tmp/d2.md"), {"machine"});
    idx.remove_document("d1");

    std::string path = "/tmp/tinysearch_persist_test/compacted.dat";
    IndexStore::save(idx, path);
    InvertedIndex loaded;
    IndexStore::load(loaded, path);
    EXPECT_EQ(loaded.document_count(), static_cast<uint32_t>(1));
    EXPECT_FALSE(loaded.contains("d1"));
    EXPECT_TRUE(loaded.contains("d2"));
}
