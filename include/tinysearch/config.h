#pragma once

#include <cstddef>
#include <string>

#include "tinysearch/scorer.h"

namespace tinysearch {

// All tunables in one place, as required by spec (BM25 k1/b, stop word
// list, cache size, top-k limits...). Loadable from a simple `key=value`
// text file; falls back to documented defaults for anything unset.
struct Config {
    Bm25Params bm25;
    RankerType default_ranker = RankerType::BM25;

    std::size_t default_top_k = 10;
    std::size_t max_top_k = 100;
    std::size_t max_query_bytes = 4096;
    std::size_t max_phrase_terms = 32;

    std::size_t cache_capacity = 1000;
    std::size_t snippet_context_chars = 120;

    std::string index_path = "./data/index.dat";
    std::string stop_words_path;  // empty => use built-in default list

    std::string host = "127.0.0.1";
    int port = 8080;

    static Config load_from_file(const std::string& path);
    static Config defaults() { return Config{}; }
};

}  // namespace tinysearch
