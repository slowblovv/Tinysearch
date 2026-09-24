#include "tinysearch/index.h"

#include <algorithm>

namespace tinysearch {

std::uint32_t InvertedIndex::add_document(const DocumentMeta& meta,
                                           const std::vector<std::string>& terms) {
    // Update semantics: if a document with this stable id already exists,
    // tombstone the old one first (see erase_term_postings) and append a
    // fresh internal id. We deliberately do not reclaim/reuse internal ids —
    // it keeps add/remove O(1)-ish and simple to reason about for a v1.
    // Documented trade-off: over many update cycles this can leave holes in
    // docs_/doc_lengths_; a future version could compact on save().
    auto existing = id_to_internal_.find(meta.id);
    if (existing != id_to_internal_.end()) {
        erase_term_postings(existing->second);
        tombstone_[existing->second] = true;
    }

    std::uint32_t internal_id = static_cast<std::uint32_t>(docs_.size());
    docs_.push_back(meta);
    doc_lengths_.push_back(static_cast<std::uint32_t>(terms.size()));
    tombstone_.push_back(false);
    id_to_internal_[meta.id] = internal_id;

    std::unordered_map<std::string, std::vector<std::uint32_t>> positions_by_term;
    for (std::uint32_t pos = 0; pos < terms.size(); ++pos) {
        positions_by_term[terms[pos]].push_back(pos);
    }
    for (auto& [term, positions] : positions_by_term) {
        PostingList& pl = terms_[term];
        Posting p;
        p.document_id = internal_id;
        p.term_frequency = static_cast<std::uint32_t>(positions.size());
        p.positions = std::move(positions);
        pl.postings.push_back(std::move(p));
        pl.document_frequency = static_cast<std::uint32_t>(pl.postings.size());
    }

    recompute_stats();
    return internal_id;
}

void InvertedIndex::erase_term_postings(std::uint32_t internal_id) {
    for (auto& [term, pl] : terms_) {
        auto it = std::remove_if(pl.postings.begin(), pl.postings.end(),
                                  [internal_id](const Posting& p) {
                                      return p.document_id == internal_id;
                                  });
        if (it != pl.postings.end()) {
            pl.postings.erase(it, pl.postings.end());
            pl.document_frequency = static_cast<std::uint32_t>(pl.postings.size());
        }
    }
}

bool InvertedIndex::remove_document(const std::string& stable_id) {
    auto it = id_to_internal_.find(stable_id);
    if (it == id_to_internal_.end()) return false;
    std::uint32_t internal_id = it->second;
    if (tombstone_[internal_id]) return false;
    erase_term_postings(internal_id);
    tombstone_[internal_id] = true;
    id_to_internal_.erase(it);
    recompute_stats();
    return true;
}

bool InvertedIndex::contains(const std::string& stable_id) const {
    return id_to_internal_.find(stable_id) != id_to_internal_.end();
}

const PostingList* InvertedIndex::postings_for(const std::string& term) const {
    auto it = terms_.find(term);
    if (it == terms_.end()) return nullptr;
    return &it->second;
}

std::uint32_t InvertedIndex::document_length(std::uint32_t doc_id) const {
    if (doc_id >= doc_lengths_.size()) return 0;
    return doc_lengths_[doc_id];
}

const DocumentMeta* InvertedIndex::meta_for(std::uint32_t doc_id) const {
    if (doc_id >= docs_.size() || tombstone_[doc_id]) return nullptr;
    return &docs_[doc_id];
}

const DocumentMeta* InvertedIndex::meta_for_stable_id(const std::string& stable_id) const {
    auto it = id_to_internal_.find(stable_id);
    if (it == id_to_internal_.end()) return nullptr;
    return meta_for(it->second);
}

std::optional<std::uint32_t> InvertedIndex::internal_id_for(const std::string& stable_id) const {
    auto it = id_to_internal_.find(stable_id);
    if (it == id_to_internal_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::uint32_t> InvertedIndex::all_document_ids() const {
    std::vector<std::uint32_t> ids;
    ids.reserve(docs_.size());
    for (std::uint32_t i = 0; i < docs_.size(); ++i) {
        if (!tombstone_[i]) ids.push_back(i);
    }
    return ids;
}

void InvertedIndex::recompute_stats() {
    std::uint64_t count = 0;
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < docs_.size(); ++i) {
        if (tombstone_[i]) continue;
        ++count;
        total += doc_lengths_[i];
    }
    stats_.document_count = count;
    stats_.total_tokens = total;
    stats_.average_document_length = count > 0
        ? static_cast<double>(total) / static_cast<double>(count)
        : 0.0;
}

void InvertedIndex::load_raw(std::vector<DocumentMeta> docs,
                              std::vector<std::uint32_t> doc_lengths,
                              std::unordered_map<std::string, PostingList> terms) {
    docs_ = std::move(docs);
    doc_lengths_ = std::move(doc_lengths);
    terms_ = std::move(terms);
    tombstone_.assign(docs_.size(), false);
    id_to_internal_.clear();
    for (std::uint32_t i = 0; i < docs_.size(); ++i) {
        id_to_internal_[docs_[i].id] = i;
    }
    recompute_stats();
}

}  // namespace tinysearch
