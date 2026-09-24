#pragma once

#include <cstdint>

namespace tinysearch {

enum class RankerType { TF_IDF, BM25 };

struct Bm25Params {
    double k1 = 1.2;
    double b = 0.75;
};

// Pure scoring functions — no knowledge of the index or documents, just
// numbers in, a score out. Kept free of any I/O so they're trivially unit
// testable (see tests/bm25_test.cpp).
class Scorer {
public:
    // Inverse document frequency, classic "probabilistic" IDF used by BM25.
    // Guarantees IDF >= 0 by flooring at a small epsilon, so very common
    // terms (df close to N) don't produce negative scores that would sort
    // documents backwards.
    static double idf_bm25(std::uint64_t document_count, std::uint64_t document_frequency);

    // Classic smoothed IDF used for the TF-IDF baseline: log(N / df) + 1.
    static double idf_tfidf(std::uint64_t document_count, std::uint64_t document_frequency);

    static double tfidf_score(double tf, double idf);

    static double bm25_score(double tf, double idf, std::uint32_t doc_length,
                              double average_document_length, const Bm25Params& params);
};

}  // namespace tinysearch
