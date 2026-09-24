#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "tinysearch/index.h"

namespace tinysearch {

struct ScannedFile {
    std::string path;
    std::int64_t modified_time = 0;
};

// Recursively finds indexable files (.md, .txt) under a root directory.
class Scanner {
public:
    static std::vector<ScannedFile> scan(const std::string& root_dir);
};

// Stable document id, derived deterministically from the document's path.
//
// Trade-off (spec section 10): the spec suggests SHA-256(path). TinySearch
// v1 instead uses a 64-bit FNV-1a hash of the path, hex-encoded. This is
// NOT cryptographically strong, but document ids only need to be stable and
// collision-resistant for a "personal notes / small corpus" search engine
// (thousands, not billions, of documents) — and it avoids pulling in a
// crypto library just for id generation. Documented in docs/architecture.md.
std::string hash_path(const std::string& path);

// Thrown when loading a corrupted or version-incompatible index file.
class IndexLoadError : public std::runtime_error {
public:
    explicit IndexLoadError(const std::string& msg) : std::runtime_error(msg) {}
};

// Binary on-disk format for the inverted index (spec sections 47-49).
// See docs/architecture.md for the exact byte layout and the
// write-tmp-then-rename durability strategy.
class IndexStore {
public:
    static constexpr std::uint32_t kFormatVersion = 1;

    // Writes `index` to `path` via a temp file + atomic rename, so a crash
    // mid-write can never leave a half-written index.dat behind.
    static void save(const InvertedIndex& index, const std::string& path);

    // Loads an index previously written by save(). Throws IndexLoadError on
    // a missing file, corrupted content, or an unsupported format version.
    static void load(InvertedIndex& index, const std::string& path);
};

}  // namespace tinysearch
