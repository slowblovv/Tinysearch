#include "tinysearch/normalizer.h"

#include <algorithm>
#include <cctype>

namespace tinysearch {

Normalizer::Normalizer(std::unordered_set<std::string> stop_words)
    : stop_words_(std::move(stop_words)) {}

std::unordered_set<std::string> Normalizer::default_stop_words() {
    return {
        "the", "a", "an", "is", "of", "to", "in", "on", "and", "or",
        "are", "was", "were", "be", "been", "being", "it", "this", "that",
        "as", "at", "by", "for", "from", "with", "not", "but", "if",
        "then", "than", "so", "such", "no", "nor", "too", "very",
        "can", "will", "just", "do", "does", "did", "has", "have", "had",
        "i", "you", "he", "she", "we", "they", "them", "his", "her",
        "its", "our", "your", "their",
    };
}

std::string Normalizer::to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// Small, ordered suffix-stripping stemmer. Rules are checked longest-suffix
// first so e.g. "-ies" is preferred over a naive "-s" strip.
// Only ASCII-lowercase input is expected here (see normalize()).
std::string Normalizer::stem(const std::string& s) {
    const std::size_t len = s.size();

    auto ends_with = [&](const std::string& suffix) {
        return len > suffix.size() &&
               s.compare(len - suffix.size(), suffix.size(), suffix) == 0;
    };

    // Don't stem very short words — too easy to destroy meaning
    // ("as", "is", "run" at 3 chars stay as-is unless matched below).
    if (len <= 3) return s;

    if (ends_with("ies") && len > 4) {
        return s.substr(0, len - 3) + "y";        // studies -> study
    }
    if (ends_with("ing") && len > 5) {
        std::string stem = s.substr(0, len - 3);   // running -> runn -> run
        if (stem.size() >= 2 && stem[stem.size() - 1] == stem[stem.size() - 2]) {
            stem.pop_back();  // undo double consonant: runn -> run
        }
        return stem;
    }
    if (ends_with("ness") && len > 5) {
        return s.substr(0, len - 4);               // happiness -> happi
    }
    if (ends_with("ment") && len > 5) {
        return s.substr(0, len - 4);               // development -> develop
    }
    if (ends_with("ed") && len > 4) {
        std::string stem = s.substr(0, len - 2);    // learned -> learn
        return stem;
    }
    if (ends_with("ly") && len > 4) {
        return s.substr(0, len - 2);                // quickly -> quick
    }
    if (ends_with("es") && len > 4) {
        return s.substr(0, len - 2);                // matches -> match
    }
    if (ends_with("s") && !ends_with("ss") && len > 3) {
        return s.substr(0, len - 1);                // documents -> document
    }
    return s;
}

std::string Normalizer::normalize(const std::string& token) const {
    if (token.empty()) return "";
    std::string lower = to_lower(token);
    if (is_stop_word(lower)) return "";
    std::string stemmed = stem(lower);
    return stemmed;
}

bool Normalizer::is_stop_word(const std::string& lower_token) const {
    return stop_words_.find(lower_token) != stop_words_.end();
}

std::vector<std::string> Normalizer::normalize_all(
    const std::vector<std::string>& tokens) const {
    std::vector<std::string> out;
    out.reserve(tokens.size());
    for (const auto& t : tokens) {
        std::string n = normalize(t);
        if (!n.empty()) out.push_back(std::move(n));
    }
    return out;
}

}  // namespace tinysearch
