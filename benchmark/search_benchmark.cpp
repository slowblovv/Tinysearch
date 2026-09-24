// Search latency benchmark (spec sections 59-60).
//
// Builds a synthetic corpus once, then measures p50/p95/p99 query latency
// for different query shapes (single term, AND, OR, phrase) and for cold
// vs warm cache.
#include <algorithm>
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
};

std::string generate_document(std::mt19937& rng, std::size_t target_words) {
    std::uniform_int_distribution<std::size_t> word_pick(0, kVocab.size() - 1);
    std::ostringstream out;
    out << "# Document about " << kVocab[word_pick(rng)] << "\n\n";
    for (std::size_t i = 0; i < target_words; ++i) out << kVocab[word_pick(rng)] << " ";
    return out.str();
}

void build_corpus(const std::string& dir, std::size_t n_docs) {
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::mt19937 rng(7);
    for (std::size_t i = 0; i < n_docs; ++i) {
        std::ofstream(dir + "/doc_" + std::to_string(i) + ".md") << generate_document(rng, 150);
    }
}

struct Percentiles {
    double p50, p95, p99;
};

Percentiles percentiles_of(std::vector<double> samples_ms) {
    std::sort(samples_ms.begin(), samples_ms.end());
    auto at = [&](double pct) {
        std::size_t idx = static_cast<std::size_t>(pct * (samples_ms.size() - 1));
        return samples_ms[idx];
    };
    return {at(0.50), at(0.95), at(0.99)};
}

void report(const std::string& label, SearchEngine& engine, const std::string& query,
            int iterations) {
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto resp = engine.search(query, RankerType::BM25, 10);
        auto t1 = std::chrono::steady_clock::now();
        (void)resp;
        latencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    Percentiles p = percentiles_of(latencies);
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  " << std::left << std::setw(14) << label << " p50=" << p.p50
               << "ms  p95=" << p.p95 << "ms  p99=" << p.p99 << "ms\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t n_docs = 10000;
    if (argc > 1) n_docs = static_cast<std::size_t>(std::stoul(argv[1]));

    std::string dir = "/tmp/tinysearch_bench/search";
    build_corpus(dir, n_docs);

    Config cfg = Config::defaults();
    cfg.cache_capacity = 1000;
    SearchEngine engine(cfg);
    IndexReport rep = engine.index_directory(dir);

    std::cout << "TinySearch Search Benchmark\n";
    std::cout << "============================\n\n";
    std::cout << "Corpus: " << rep.added << " documents indexed\n\n";

    const int iterations = 200;

    std::cout << "Cold cache (each query below hits a distinct term/combo the first time):\n";
    report("single term", engine, "machine", 1);
    report("AND", engine, "machine AND learning", 1);
    report("OR", engine, "machine OR database", 1);
    report("phrase", engine, "\"machine learning\"", 1);

    std::cout << "\nWarm cache (same query repeated, " << iterations << " iterations):\n";
    report("single term", engine, "machine", iterations);
    report("AND", engine, "machine AND learning", iterations);
    report("OR", engine, "machine OR database", iterations);
    report("phrase", engine, "\"machine learning\"", iterations);

    EngineStats stats = engine.stats();
    std::cout << "\nCache hits=" << stats.cache_hits << " misses=" << stats.cache_misses << "\n";

    fs::remove_all(dir);
    return 0;
}
