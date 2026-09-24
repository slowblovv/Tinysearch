#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tinysearch/document.h"

namespace tinysearch {

// One occurrence list for a (term, document) pair.
struct Posting {
    std::uint32_t document_id = 0;   // internal dense id, see InvertedIndex
    std::uint32_t term_frequency = 0;
    std::vector<std::uint32_t> positions;  // token offsets within the document
};

struct PostingList {
    std::uint32_t document_frequency = 0;  // == postings.size(), kept for O(1) reads
    std::vector<Posting> postings;         // sorted by document_id
};

// Collection-wide statistics needed by TF-IDF / BM25.
struct CollectionStats {
    std::uint64_t document_count = 0;
    std::uint64_t total_tokens = 0;
    double average_document_length = 0.0;
};

// The core data structure of TinySearch: term -> posting list.
//
// Documents are addressed internally by a dense uint32 id (index into
// docs_). The public, stable, string Document::id (derived from the file
// path) is only used at the edges (API responses, persistence, incremental
// indexing) — see docs/architecture.md, "Why two ids?".
class InvertedIndex {
public:
    // Adds (or replaces, if a document with the same stable id already
    // exists) a document's tokens into the index. `terms` must already be
    // normalized (lowercased/stemmed/stopword-filtered) and in original
    // document order, so that vector position == token offset.
    std::uint32_t add_document(const DocumentMeta& meta,
                                const std::vector<std::string>& terms);

    // Removes a document (by stable id) from the index, if present.
    // Returns true if something was removed.
    bool remove_document(const std::string& stable_id);

    bool contains(const std::string& stable_id) const;

    // Returns the posting list for a term, or nullptr if the term is unknown.
    const PostingList* postings_for(const std::string& term) const;

    std::uint32_t document_length(std::uint32_t doc_id) const;
    const DocumentMeta* meta_for(std::uint32_t doc_id) const;
    const DocumentMeta* meta_for_stable_id(const std::string& stable_id) const;
    std::optional<std::uint32_t> internal_id_for(const std::string& stable_id) const;

    const CollectionStats& stats() const { return stats_; }
    std::uint32_t document_count() const {
        return static_cast<std::uint32_t>(docs_.size());
    }
    std::uint64_t term_count() const { return terms_.size(); }

    // All internal ids currently live (i.e. not tombstoned). Used for NOT.
    std::vector<std::uint32_t> all_document_ids() const;

    // Iterate every term -> posting list, e.g. for persistence / stats.
    const std::unordered_map<std::string, PostingList>& raw_terms() const {
        return terms_;
    }
    const std::vector<DocumentMeta>& raw_docs() const { return docs_; }
    const std::vector<bool>& tombstones() const { return tombstone_; }

    // Rebuilds derived stats (avg doc length etc). Called automatically by
    // add/remove but exposed for persistence loading.
    void recompute_stats();

    // Bulk-loads an already-compacted (no tombstones, dense 0..N-1 ids)
    // index snapshot, e.g. from IndexStore::load(). Replaces all current
    // contents. See index_store.cpp for the on-disk format.
    void load_raw(std::vector<DocumentMeta> docs, std::vector<std::uint32_t> doc_lengths,
                  std::unordered_map<std::string, PostingList> terms);

private:
    std::unordered_map<std::string, PostingList> terms_;
    std::vector<DocumentMeta> docs_;                 // internal_id -> meta
    std::vector<std::uint32_t> doc_lengths_;          // internal_id -> #tokens indexed
    std::vector<bool> tombstone_;                     // internal_id -> deleted?
    std::unordered_map<std::string, std::uint32_t> id_to_internal_;
    CollectionStats stats_;

    void erase_term_postings(std::uint32_t internal_id);
};

}  // namespace tinysearch
