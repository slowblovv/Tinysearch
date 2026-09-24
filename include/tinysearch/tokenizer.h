#pragma once

#include <string>
#include <vector>

namespace tinysearch {

// Splits raw text into a stream of tokens.
//
// Rules (see docs/architecture.md for the full rationale):
//  - Unicode-aware only at the byte level: we treat any byte >= 0x80 (i.e.
//    any multi-byte UTF-8 sequence) as a "word" byte, so multi-byte UTF-8
//    characters are kept together inside tokens instead of being split.
//    We do NOT do full Unicode case-folding / normalization (documented
//    trade-off — see docs/architecture.md, section "Tokenization").
//  - ASCII letters and digits are word characters.
//  - Everything else (punctuation, whitespace, control chars) is a
//    separator and is dropped.
//  - Runs of separators collapse to a single split point, so repeated
//    whitespace/punctuation never produces empty tokens.
class Tokenizer {
public:
    // Tokenize `text` into a sequence of raw tokens (not yet normalized).
    static std::vector<std::string> tokenize(const std::string& text);

private:
    static bool is_word_byte(unsigned char c);
};

}  // namespace tinysearch
