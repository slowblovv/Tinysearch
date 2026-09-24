#pragma once

#include <string>
#include <vector>

namespace tinysearch {

// Builds a short, human-readable excerpt of a document around the first
// place one of the query terms appears, with the matching words wrapped in
// <mark>...</mark> (see spec section 37 — <mark> chosen over <b> since it's
// the semantically-correct HTML tag for "highlighted search text").
class SnippetGenerator {
public:
    // context_chars: how many characters of context to show on each side of
    // the first match (spec suggests 80-160 total; we default to a 120-char
    // radius, i.e. ~240 chars total, then trim to whole words).
    explicit SnippetGenerator(std::size_t context_chars = 120) : context_chars_(context_chars) {}

    // `content` is the raw (un-tokenized) document text. `query_terms_lower`
    // are the already-normalized query terms to look for — matching is done
    // via simple case-insensitive substring search on token boundaries,
    // independent of stemming (so e.g. searching "running" highlights the
    // literal word "running" in the snippet, not "run").
    std::string generate(const std::string& content,
                          const std::vector<std::string>& raw_query_words) const;

private:
    std::size_t context_chars_;
};

}  // namespace tinysearch
