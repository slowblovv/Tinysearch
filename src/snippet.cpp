#include "tinysearch/snippet.h"

#include <algorithm>
#include <cctype>

namespace tinysearch {

namespace {
std::string to_lower(const std::string& s) {
    std::string out(s.size(), '\0');
    std::transform(s.begin(), s.end(), out.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return out;
}
}  // namespace

std::string SnippetGenerator::generate(const std::string& content,
                                        const std::vector<std::string>& raw_query_words) const {
    if (content.empty()) return "";
    std::string lower_content = to_lower(content);

    // Find the earliest match among any query word.
    std::size_t best_pos = std::string::npos;
    std::size_t best_len = 0;
    for (const auto& w : raw_query_words) {
        if (w.empty()) continue;
        std::string lw = to_lower(w);
        std::size_t pos = lower_content.find(lw);
        if (pos != std::string::npos && (best_pos == std::string::npos || pos < best_pos)) {
            best_pos = pos;
            best_len = lw.size();
        }
    }

    std::size_t start;
    if (best_pos == std::string::npos) {
        // No literal match (can happen since index terms are stemmed) —
        // just show the start of the document.
        start = 0;
    } else {
        std::size_t left = context_chars_;
        start = best_pos > left ? best_pos - left : 0;
    }

    std::size_t end = std::min(content.size(), start + 2 * context_chars_ + best_len);

    // Snap `start` forward to the next word boundary (unless already at 0),
    // and `end` backward, so we don't cut a word in half.
    if (start > 0) {
        while (start < content.size() && !std::isspace(static_cast<unsigned char>(content[start]))) {
            ++start;
        }
    }
    if (end < content.size()) {
        while (end > start && !std::isspace(static_cast<unsigned char>(content[end]))) {
            --end;
        }
    }
    if (start >= end) {
        start = 0;
        end = std::min(content.size(), static_cast<std::size_t>(2 * context_chars_));
    }

    std::string excerpt = content.substr(start, end - start);

    // Re-locate + highlight the match within the trimmed excerpt (positions
    // shifted because we snapped the window boundaries).
    if (best_pos != std::string::npos) {
        std::string lower_excerpt = to_lower(excerpt);
        for (const auto& w : raw_query_words) {
            if (w.empty()) continue;
            std::string lw = to_lower(w);
            std::size_t pos = lower_excerpt.find(lw);
            if (pos != std::string::npos) {
                excerpt.insert(pos + lw.size(), "</mark>");
                excerpt.insert(pos, "<mark>");
                break;
            }
        }
    }

    std::string prefix = start > 0 ? "..." : "";
    std::string suffix = end < content.size() ? "..." : "";
    return prefix + excerpt + suffix;
}

}  // namespace tinysearch
