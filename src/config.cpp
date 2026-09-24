#include "tinysearch/config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tinysearch {

namespace {
std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

Config Config::load_from_file(const std::string& path) {
    Config cfg;
    std::ifstream in(path);
    if (!in.is_open()) return cfg;  // missing config file -> defaults

    std::string line;
    while (std::getline(in, line)) {
        std::size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        if (key == "bm25.k1") cfg.bm25.k1 = std::stod(value);
        else if (key == "bm25.b") cfg.bm25.b = std::stod(value);
        else if (key == "default_ranker") {
            cfg.default_ranker = (value == "tfidf") ? RankerType::TF_IDF : RankerType::BM25;
        } else if (key == "default_top_k") cfg.default_top_k = std::stoul(value);
        else if (key == "max_top_k") cfg.max_top_k = std::stoul(value);
        else if (key == "cache_capacity") cfg.cache_capacity = std::stoul(value);
        else if (key == "snippet_context_chars") cfg.snippet_context_chars = std::stoul(value);
        else if (key == "index_path") cfg.index_path = value;
        else if (key == "stop_words_path") cfg.stop_words_path = value;
        else if (key == "host") cfg.host = value;
        else if (key == "port") cfg.port = std::stoi(value);
    }
    return cfg;
}

}  // namespace tinysearch
