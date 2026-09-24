#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "tinysearch/cache.h"
#include "tinysearch/config.h"
#include "tinysearch/index.h"
#include "tinysearch/normalizer.h"
#include "tinysearch/query.h"
#include "tinysearch/scorer.h"
#include "tinysearch/snippet.h"

namespace tinysearch {

struct SearchResult {
    std::string document_id;
    std::string title;
    std::string path;
    double score = 0.0;
    std::string snippet;
};

struct SearchResponse {
    std::string query;
    std::string ranker;
    std::size_t total_candidates = 0;
    double duration_ms = 0.0;
    bool cache_hit = false;
    std::vector<SearchResult> results;
};

struct IndexReport {
    std::size_t documents_found = 0;
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t deleted = 0;
    std::size_t unchanged = 0;
    std::size_t errors = 0;
    std::vector<std::pair<std::string, std::string>> error_details;  // path -> reason
    double duration_ms = 0.0;
};

struct EngineStats {
    std::uint64_t documents = 0;
    std::uint64_t terms = 0;
    std::uint64_t total_tokens = 0;
    double average_document_length = 0.0;
    std::uint64_t index_size_bytes = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
};

// Orchestrates the whole indexing -> query -> ranking pipeline described in
// docs/architecture.md. Safe for concurrent use: many threads may call
// search() simultaneously; index_directory()/remove_document() take an
// exclusive lock only for the brief "atomic swap" step (see section 54 of
// the spec) after the new index has already been built off to the side.
class SearchEngine {
public:
    explicit SearchEngine(Config config);

    // Scans `directory`, indexes new/changed files, removes documents whose
    // source file disappeared (unless incremental == false, in which case
    // everything is rebuilt from scratch).
    IndexReport index_directory(const std::string& directory, bool incremental = true);

    // Parses, executes and ranks `query_text`. Throws QueryParseError on a
    // malformed query (caller should turn that into an HTTP 400).
    SearchResponse search(const std::string& query_text, RankerType ranker,
                           std::size_t top_k);

    const DocumentMeta* get_document(const std::string& id) const;

    EngineStats stats() const;

    void save(const std::string& path);
    void load(const std::string& path);

    const Config& config() const { return config_; }

private:
    Config config_;
    Normalizer normalizer_;
    SnippetGenerator snippet_gen_;
    std::unique_ptr<InvertedIndex> index_;
    mutable std::shared_mutex mutex_;  // protects `index_`
    LruCache<SearchResponse> cache_;

    // Boolean AST evaluation: returns the sorted set of internal doc ids
    // that structurally match `node` (AND=intersect, OR=union, NOT=set
    // difference, TERM/PHRASE=postings lookup).
    std::vector<std::uint32_t> evaluate(const QueryNode& node, const InvertedIndex& idx) const;

    // Collects the (term -> phrase-or-not) leaves that should contribute to
    // ranking, i.e. every TERM/PHRASE reachable without crossing a NOT.
    void collect_scoring_terms(const QueryNode& node, bool negated,
                                std::vector<const QueryNode*>& out) const;

    double score_document(std::uint32_t doc_id, RankerType ranker,
                           const std::vector<const QueryNode*>& scoring_terms,
                           const InvertedIndex& idx,
                           const std::unordered_map<
                               const QueryNode*,
                               std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>>&
                               phrase_cache) const;

    // Finds documents (and, per-document, the starting positions) where
    // `terms` occur as a contiguous phrase.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> match_phrase(
        const std::vector<std::string>& terms, const InvertedIndex& idx) const;

    std::string make_cache_key(const std::string& query_text, RankerType ranker,
                                std::size_t top_k) const;
};

}  // namespace tinysearch
