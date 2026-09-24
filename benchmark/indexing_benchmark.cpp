// Indexing throughput benchmark (spec section 58).
//
// Generates synthetic Markdown documents from a small fixed vocabulary
// (so results are reproducible without needing a real corpus on disk),
// writes them to a temp directory, and measures indexing throughput at
// several corpus sizes.
//
// Usage: indexing_benchmark [max_docs]   (default 10000; use 100000 for
// the full spec-recommended sweep — that takes a while and a few hundred
// MB of temp disk space).
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "tinysearch/config.h"
#include "tinysearch/search_engine.h"

namespace fs = std::filesystem;
using namespace tinysearch;

namespace {

const std::vector<std::string> kVocab = {
    "machine", "learning", "database", "network", "system", "algorithm",
    "performance", "storage", "query", "index", "vector", "model", "training",
    "inference", "cluster", "replication", "transaction", "concurrency",
    "memory", "cache", "protocol", "encryption", "language", "processing",
    "architecture", "pipeline", "deployment", "scaling", "throughput", "latency",
};

std::string generate_document(std::mt19937& rng, std::size_t target_words) {
    std::uniform_int_distribution<std::size_t> word_pick(0, kVocab.size() - 1);
    std::ostringstream out;
    out << "# Document about " << kVocab[word_pick(rng)] << " and " << kVocab[word_pick(rng)]
        << "\n\n";
    for (std::size_t i = 0; i < target_words; ++i) {
        out << kVocab[word_pick(rng)] << " ";
        if (i % 15 == 14) out << "\n";
    }
    return out.str();
}

double mb_of(std::uintmax_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

void run_size(std::size_t n_docs) {
    std::string dir = "/tmp/tinysearch_bench/indexing_" + std::to_string(n_docs);
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::mt19937 rng(42);
    std::uintmax_t total_bytes = 0;
    for (std::size_t i = 0; i < n_docs; ++i) {
        std::string content = generate_document(rng, 120);  // ~120 words/doc
        std::string path = dir + "/doc_" + std::to_string(i) + ".md";
        std::ofstream(path) << content;
        total_bytes += content.size();
    }

    Config cfg = Config::defaults();
    cfg.index_path = dir + "/index.dat";
    SearchEngine engine(cfg);

    auto t0 = std::chrono::steady_clock::now();
    IndexReport report = engine.index_directory(dir);
    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    engine.save(cfg.index_path);
    std::uintmax_t index_size = fs::exists(cfg.index_path) ? fs::file_size(cfg.index_path) : 0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "docs=" << n_docs << "  indexed=" << report.added << "  time=" << seconds
               << "s  docs/sec=" << (seconds > 0 ? n_docs / seconds : 0.0)
               << "  MB/sec=" << (seconds > 0 ? mb_of(total_bytes) / seconds : 0.0)
               << "  corpus=" << mb_of(total_bytes) << "MB"
               << "  index_size=" << mb_of(index_size) << "MB\n";

    fs::remove_all(dir);
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t max_docs = 10000;
    if (argc > 1) max_docs = static_cast<std::size_t>(std::stoul(argv[1]));

    std::cout << "TinySearch Indexing Benchmark\n";
    std::cout << "==============================\n\n";
    std::vector<std::size_t> sizes = {100, 1000, 10000, 100000};
    for (std::size_t n : sizes) {
        if (n > max_docs) break;
        run_size(n);
    }
    return 0;
}
