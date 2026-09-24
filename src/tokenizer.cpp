#include "tinysearch/tokenizer.h"

namespace tinysearch {

bool Tokenizer::is_word_byte(unsigned char c) {
    if (c >= 0x80) return true;  // part of a multi-byte UTF-8 sequence
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return false;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(32);

    for (unsigned char c : text) {
        if (is_word_byte(c)) {
            current.push_back(static_cast<char>(c));
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

}  // namespace tinysearch
