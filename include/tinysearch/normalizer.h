#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace tinysearch {

// Lowercases tokens, drops stop words and applies a small suffix-stripping
// stemmer.
//
// Stemming trade-off (documented per spec section 16 / docs/ranking.md):
// TinySearch does NOT implement full Porter/Snowball stemming. It uses a
// small set of high-confidence English suffix-stripping rules (plurals,
// -ing, -ed, -ly, -ness, -ment ...). This under-stems some irregular words
// (e.g. "ran" is not linked to "run") and can occasionally over-stem short
// words, but it is simple, fast, dependency-free and good enough to noticeably
// improve recall for the common regular-morphology case, which is the
// explicit goal for v1.
class Normalizer {
public:
    explicit Normalizer(std::unordered_set<std::string> stop_words = default_stop_words());

    // Lowercase + strip. Returns std::nullopt-like empty string if the token
    // should be dropped entirely (stop word, or empty after stemming).
    // Empty string return means "drop this token".
    std::string normalize(const std::string& token) const;

    // Convenience: normalize a whole token stream, dropping empty results.
    std::vector<std::string> normalize_all(const std::vector<std::string>& tokens) const;

    static std::unordered_set<std::string> default_stop_words();

    bool is_stop_word(const std::string& lower_token) const;

private:
    std::unordered_set<std::string> stop_words_;

    static std::string to_lower(const std::string& s);
    static std::string stem(const std::string& s);
};

}  // namespace tinysearch
