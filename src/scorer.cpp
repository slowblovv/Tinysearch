#include "tinysearch/scorer.h"

#include <algorithm>
#include <cmath>

namespace tinysearch {

double Scorer::idf_bm25(std::uint64_t document_count, std::uint64_t document_frequency) {
    if (document_count == 0 || document_frequency == 0) return 0.0;
    double N = static_cast<double>(document_count);
    double df = static_cast<double>(document_frequency);
    double raw = std::log(1.0 + (N - df + 0.5) / (df + 0.5));
    // Floor at a small epsilon rather than 0: a term appearing in almost
    // every document should contribute ~nothing, not push the score
    // negative and invert the ranking of otherwise-similar documents.
    return std::max(raw, 1e-9);
}

double Scorer::idf_tfidf(std::uint64_t document_count, std::uint64_t document_frequency) {
    if (document_count == 0 || document_frequency == 0) return 0.0;
    double N = static_cast<double>(document_count);
    double df = static_cast<double>(document_frequency);
    return std::log(N / df) + 1.0;
}

double Scorer::tfidf_score(double tf, double idf) {
    if (tf <= 0.0) return 0.0;
    // Log-dampened TF, standard baseline variant: (1 + log(tf)) * idf.
    return (1.0 + std::log(tf)) * idf;
}

double Scorer::bm25_score(double tf, double idf, std::uint32_t doc_length,
                           double average_document_length, const Bm25Params& params) {
    if (tf <= 0.0) return 0.0;
    double avgdl = average_document_length > 0.0 ? average_document_length : 1.0;
    double denom = tf + params.k1 * (1.0 - params.b +
                                      params.b * (static_cast<double>(doc_length) / avgdl));
    if (denom <= 0.0) return 0.0;
    return idf * (tf * (params.k1 + 1.0)) / denom;
}

}  // namespace tinysearch
