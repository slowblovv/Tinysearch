#include "tinysearch/search_engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_set>

#include "tinysearch/index_store.h"
#include "tinysearch/parser.h"
#include "tinysearch/tokenizer.h"

namespace tinysearch {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file");
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) throw std::runtime_error("read error");
    return ss.str();
}

std::string extract_title(const std::string& content, const std::string& path) {
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        std::size_t b = line.find_last_not_of(" \t\r");
        std::string trimmed = line.substr(a, b - a + 1);
        if (trimmed.empty()) continue;
        // strip leading markdown heading hashes: "## Title" -> "Title"
        std::size_t h = 0;
        while (h < trimmed.size() && trimmed[h] == '#') ++h;
        if (h > 0 && h < trimmed.size() && trimmed[h] == ' ') {
            trimmed = trimmed.substr(h + 1);
        }
        if (!trimmed.empty()) return trimmed;
    }
    std::filesystem::path p(path);
    return p.stem().string();
}

const char* ranker_name(RankerType r) { return r == RankerType::BM25 ? "bm25" : "tfidf"; }

}  // namespace

// NOTE: `config_.stop_words_path` is accepted by Config for forward
// compatibility (spec section 15: "stop-word list must be configurable")
// but v1 always seeds from the built-in default list; loading a custom
// file from disk is a natural, low-risk v1.1 addition.
SearchEngine::SearchEngine(Config config)
    : config_(std::move(config)),
      normalizer_(Normalizer::default_stop_words()),
      snippet_gen_(config_.snippet_context_chars),
      index_(std::make_unique<InvertedIndex>()),
      cache_(config_.cache_capacity) {}

IndexReport SearchEngine::index_directory(const std::string& directory, bool incremental) {
    auto t0 = std::chrono::steady_clock::now();
    IndexReport report;

    // 1. Snapshot the current index under a shared (read) lock, then work
    //    entirely on a private copy — readers keep using the old index_
    //    the whole time. This is the "build new index -> atomic swap"
    //    strategy from spec section 54, option B.
    std::unique_ptr<InvertedIndex> work;
    {
        std::shared_lock lock(mutex_);
        work = incremental ? std::make_unique<InvertedIndex>(*index_)
                            : std::make_unique<InvertedIndex>();
    }

    std::vector<ScannedFile> files = Scanner::scan(directory);
    report.documents_found = files.size();

    std::unordered_set<std::string> seen_ids;
    for (const auto& f : files) {
        std::string stable_id = hash_path(f.path);
        seen_ids.insert(stable_id);

        const DocumentMeta* existing = work->meta_for_stable_id(stable_id);
        if (existing != nullptr) {
            if (existing->path == f.path && existing->modified_time == f.modified_time) {
                ++report.unchanged;
                continue;
            }
        }

        try {
            std::string content = read_file(f.path);
            std::vector<std::string> raw_tokens = Tokenizer::tokenize(content);
            std::vector<std::string> terms = normalizer_.normalize_all(raw_tokens);

            DocumentMeta meta;
            meta.id = stable_id;
            meta.path = f.path;
            meta.title = extract_title(content, f.path);
            meta.token_count = static_cast<std::uint32_t>(terms.size());
            meta.modified_time = f.modified_time;

            work->add_document(meta, terms);
            if (existing != nullptr) {
                ++report.updated;
            } else {
                ++report.added;
            }
        } catch (const std::exception& e) {
            ++report.errors;
            report.error_details.emplace_back(f.path, e.what());
        }
    }

    // 2. Deletions: anything the (private copy of the) index still knows
    //    about whose source file we did not see on this scan.
    for (const auto& meta : work->raw_docs()) {
        if (!work->contains(meta.id)) continue;  // already tombstoned above
        if (seen_ids.find(meta.id) == seen_ids.end()) {
            work->remove_document(meta.id);
            ++report.deleted;
        }
    }

    // 3. Atomic swap.
    {
        std::unique_lock lock(mutex_);
        index_ = std::move(work);
    }
    cache_.invalidate_all();  // stale results would otherwise outlive the old index

    auto t1 = std::chrono::steady_clock::now();
    report.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return report;
}

std::vector<std::uint32_t> SearchEngine::evaluate(const QueryNode& node,
                                                    const InvertedIndex& idx) const {
    std::vector<std::uint32_t> result;
    switch (node.type) {
        case QueryType::TERM: {
            const PostingList* pl = idx.postings_for(node.value);
            if (pl == nullptr) return {};
            result.reserve(pl->postings.size());
            for (const auto& p : pl->postings) result.push_back(p.document_id);
            std::sort(result.begin(), result.end());
            return result;
        }
        case QueryType::PHRASE: {
            auto matches = match_phrase(node.phrase_terms, idx);
            result.reserve(matches.size());
            for (const auto& [doc_id, positions] : matches) result.push_back(doc_id);
            std::sort(result.begin(), result.end());
            return result;
        }
        case QueryType::AND: {
            auto l = evaluate(*node.left, idx);
            auto r = evaluate(*node.right, idx);
            std::set_intersection(l.begin(), l.end(), r.begin(), r.end(),
                                   std::back_inserter(result));
            return result;
        }
        case QueryType::OR: {
            auto l = evaluate(*node.left, idx);
            auto r = evaluate(*node.right, idx);
            std::set_union(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(result));
            return result;
        }
        case QueryType::NOT: {
            auto r = evaluate(*node.right, idx);
            auto all = idx.all_document_ids();  // already sorted ascending
            std::set_difference(all.begin(), all.end(), r.begin(), r.end(),
                                 std::back_inserter(result));
            return result;
        }
    }
    return result;
}

void SearchEngine::collect_scoring_terms(const QueryNode& node, bool negated,
                                          std::vector<const QueryNode*>& out) const {
    switch (node.type) {
        case QueryType::TERM:
        case QueryType::PHRASE:
            if (!negated) out.push_back(&node);
            return;
        case QueryType::NOT:
            collect_scoring_terms(*node.right, !negated, out);
            return;
        case QueryType::AND:
        case QueryType::OR:
            collect_scoring_terms(*node.left, negated, out);
            collect_scoring_terms(*node.right, negated, out);
            return;
    }
}

std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> SearchEngine::match_phrase(
    const std::vector<std::string>& terms, const InvertedIndex& idx) const {
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> result;
    if (terms.empty()) return result;

    const PostingList* first = idx.postings_for(terms[0]);
    if (first == nullptr) return result;

    // Index every other term's postings by document id once (O(df) each)
    // so the per-candidate lookup below is O(1) instead of O(df) — the
    // naive nested linear scan is a classic quadratic trap once postings
    // lists get large (this fix came directly out of the search
    // benchmark, see docs/ranking.md's "Design decisions" note on phrase
    // matching).
    std::vector<const PostingList*> rest;
    std::vector<std::unordered_map<std::uint32_t, const Posting*>> rest_by_doc;
    for (std::size_t i = 1; i < terms.size(); ++i) {
        const PostingList* pl = idx.postings_for(terms[i]);
        if (pl == nullptr) return result;  // a required term is entirely absent
        rest.push_back(pl);
        std::unordered_map<std::uint32_t, const Posting*> by_doc;
        by_doc.reserve(pl->postings.size() * 2);
        for (const auto& p : pl->postings) by_doc.emplace(p.document_id, &p);
        rest_by_doc.push_back(std::move(by_doc));
    }

    for (const auto& p0 : first->postings) {
        std::vector<const Posting*> others;
        others.reserve(rest_by_doc.size());
        bool all_present = true;
        for (const auto& by_doc : rest_by_doc) {
            auto it = by_doc.find(p0.document_id);
            if (it == by_doc.end()) {
                all_present = false;
                break;
            }
            others.push_back(it->second);
        }
        if (!all_present) continue;

        std::vector<std::uint32_t> starts;
        for (std::uint32_t s : p0.positions) {
            bool ok = true;
            for (std::size_t i = 0; i < others.size(); ++i) {
                std::uint32_t want = s + static_cast<std::uint32_t>(i + 1);
                const auto& positions = others[i]->positions;
                if (!std::binary_search(positions.begin(), positions.end(), want)) {
                    ok = false;
                    break;
                }
            }
            if (ok) starts.push_back(s);
        }
        if (!starts.empty()) result.emplace(p0.document_id, std::move(starts));
    }
    return result;
}

double SearchEngine::score_document(
    std::uint32_t doc_id, RankerType ranker, const std::vector<const QueryNode*>& scoring_terms,
    const InvertedIndex& idx,
    const std::unordered_map<const QueryNode*,
                              std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>>&
        phrase_cache) const {
    double total = 0.0;
    std::uint32_t doc_len = idx.document_length(doc_id);
    double avgdl = idx.stats().average_document_length;
    std::uint64_t N = idx.stats().document_count;

    for (const QueryNode* node : scoring_terms) {
        if (node->type == QueryType::TERM) {
            if (node->value.empty()) continue;
            const PostingList* pl = idx.postings_for(node->value);
            if (pl == nullptr) continue;
            std::uint32_t tf = 0;
            for (const auto& p : pl->postings) {
                if (p.document_id == doc_id) {
                    tf = p.term_frequency;
                    break;
                }
            }
            if (tf == 0) continue;
            double idf = (ranker == RankerType::BM25) ? Scorer::idf_bm25(N, pl->document_frequency)
                                                        : Scorer::idf_tfidf(N, pl->document_frequency);
            total += (ranker == RankerType::BM25)
                         ? Scorer::bm25_score(tf, idf, doc_len, avgdl, config_.bm25)
                         : Scorer::tfidf_score(tf, idf);
        } else if (node->type == QueryType::PHRASE) {
            // `phrase_cache` is populated once per search() call, not once
            // per candidate document — see search() for why that matters.
            auto cache_it = phrase_cache.find(node);
            if (cache_it == phrase_cache.end()) continue;
            const auto& matches = cache_it->second;
            auto it = matches.find(doc_id);
            if (it == matches.end()) continue;
            std::uint32_t tf = static_cast<std::uint32_t>(it->second.size());
            std::uint32_t df = static_cast<std::uint32_t>(matches.size());
            double idf = (ranker == RankerType::BM25) ? Scorer::idf_bm25(N, df)
                                                        : Scorer::idf_tfidf(N, df);
            total += (ranker == RankerType::BM25)
                         ? Scorer::bm25_score(tf, idf, doc_len, avgdl, config_.bm25)
                         : Scorer::tfidf_score(tf, idf);
        }
    }
    return total;
}

std::string SearchEngine::make_cache_key(const std::string& query_text, RankerType ranker,
                                          std::size_t top_k) const {
    return std::to_string(static_cast<int>(ranker)) + "|" + std::to_string(top_k) + "|" +
           query_text;
}

SearchResponse SearchEngine::search(const std::string& query_text, RankerType ranker,
                                     std::size_t top_k) {
    auto t0 = std::chrono::steady_clock::now();
    top_k = std::min(top_k, config_.max_top_k);
    if (top_k == 0) top_k = config_.default_top_k;

    std::string key = make_cache_key(query_text, ranker, top_k);
    if (auto cached = cache_.get(key)) {
        SearchResponse resp = *cached;
        resp.cache_hit = true;
        auto t1 = std::chrono::steady_clock::now();
        resp.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return resp;
    }

    QueryParser parser(normalizer_);
    std::unique_ptr<QueryNode> ast = parser.parse(query_text);  // throws QueryParseError

    SearchResponse response;
    response.query = query_text;
    response.ranker = ranker_name(ranker);
    response.cache_hit = false;

    std::vector<std::string> raw_query_words = Tokenizer::tokenize(query_text);

    {
        std::shared_lock lock(mutex_);
        std::vector<std::uint32_t> candidates = evaluate(*ast, *index_);
        response.total_candidates = candidates.size();

        std::vector<const QueryNode*> scoring_terms;
        collect_scoring_terms(*ast, false, scoring_terms);

        // Phrase matching is the expensive part (scans a whole posting
        // list). Compute it once per distinct phrase node here, rather
        // than once per candidate document inside score_document() — see
        // docs/ranking.md, "Design decisions", for the benchmark that
        // caught this as an O(N x df) hotspot.
        std::unordered_map<const QueryNode*,
                            std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>>
            phrase_cache;
        for (const QueryNode* node : scoring_terms) {
            if (node->type == QueryType::PHRASE) {
                phrase_cache[node] = match_phrase(node->phrase_terms, *index_);
            }
        }

        using ScoredDoc = std::pair<double, std::uint32_t>;
        auto cmp = [](const ScoredDoc& a, const ScoredDoc& b) { return a.first > b.first; };
        std::priority_queue<ScoredDoc, std::vector<ScoredDoc>, decltype(cmp)> heap(cmp);

        for (std::uint32_t doc_id : candidates) {
            double score = score_document(doc_id, ranker, scoring_terms, *index_, phrase_cache);
            if (heap.size() < top_k) {
                heap.push({score, doc_id});
            } else if (score > heap.top().first) {
                heap.pop();
                heap.push({score, doc_id});
            }
        }

        std::vector<ScoredDoc> ordered;
        ordered.reserve(heap.size());
        while (!heap.empty()) {
            ordered.push_back(heap.top());
            heap.pop();
        }
        std::sort(ordered.begin(), ordered.end(), [](const ScoredDoc& a, const ScoredDoc& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;  // deterministic tie-break
        });

        for (const auto& [score, doc_id] : ordered) {
            const DocumentMeta* meta = index_->meta_for(doc_id);
            if (meta == nullptr) continue;
            SearchResult r;
            r.document_id = meta->id;
            r.title = meta->title;
            r.path = meta->path;
            r.score = score;
            try {
                std::string content = read_file(meta->path);
                r.snippet = snippet_gen_.generate(content, raw_query_words);
            } catch (const std::exception&) {
                r.snippet = "";
            }
            response.results.push_back(std::move(r));
        }
    }

    cache_.put(key, response);

    auto t1 = std::chrono::steady_clock::now();
    response.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return response;
}

const DocumentMeta* SearchEngine::get_document(const std::string& id) const {
    std::shared_lock lock(mutex_);
    return index_->meta_for_stable_id(id);
}

EngineStats SearchEngine::stats() const {
    std::shared_lock lock(mutex_);
    EngineStats s;
    s.documents = index_->stats().document_count;
    s.terms = index_->term_count();
    s.total_tokens = index_->stats().total_tokens;
    s.average_document_length = index_->stats().average_document_length;

    // Rough in-memory size estimate: doc metadata + postings + positions.
    std::uint64_t bytes = 0;
    for (const auto& d : index_->raw_docs()) {
        bytes += d.id.size() + d.path.size() + d.title.size() + 24;
    }
    for (const auto& [term, pl] : index_->raw_terms()) {
        bytes += term.size() + 8;
        for (const auto& p : pl.postings) {
            bytes += 8 + p.positions.size() * 4;
        }
    }
    s.index_size_bytes = bytes;
    s.cache_hits = cache_.hits();
    s.cache_misses = cache_.misses();
    return s;
}

void SearchEngine::save(const std::string& path) {
    std::shared_lock lock(mutex_);
    IndexStore::save(*index_, path);
}

void SearchEngine::load(const std::string& path) {
    auto loaded = std::make_unique<InvertedIndex>();
    IndexStore::load(*loaded, path);  // no lock needed yet — not published
    std::unique_lock lock(mutex_);
    index_ = std::move(loaded);
    cache_.invalidate_all();
}

}  // namespace tinysearch
