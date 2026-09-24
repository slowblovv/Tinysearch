#include "tinysearch/index_store.h"

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace tinysearch {

namespace {

bool has_indexable_extension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".md" || ext == ".txt";
}

std::int64_t file_mtime(const fs::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return 0;
    return static_cast<std::int64_t>(st.st_mtime);
}

// --- little binary IO helpers -------------------------------------------

void write_u32(std::ostream& os, std::uint32_t v) { os.write(reinterpret_cast<char*>(&v), 4); }
void write_u64(std::ostream& os, std::uint64_t v) { os.write(reinterpret_cast<char*>(&v), 8); }
void write_i64(std::ostream& os, std::int64_t v) { os.write(reinterpret_cast<char*>(&v), 8); }
void write_str(std::ostream& os, const std::string& s) {
    write_u32(os, static_cast<std::uint32_t>(s.size()));
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::uint32_t read_u32(std::istream& is) {
    std::uint32_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 4);
    if (!is) throw IndexLoadError("corrupted index: unexpected EOF");
    return v;
}
std::uint64_t read_u64(std::istream& is) {
    std::uint64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    if (!is) throw IndexLoadError("corrupted index: unexpected EOF");
    return v;
}
std::int64_t read_i64(std::istream& is) {
    std::int64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    if (!is) throw IndexLoadError("corrupted index: unexpected EOF");
    return v;
}
std::string read_str(std::istream& is) {
    std::uint32_t len = read_u32(is);
    if (len > (1u << 28)) throw IndexLoadError("corrupted index: implausible string length");
    std::string s(len, '\0');
    is.read(s.data(), static_cast<std::streamsize>(len));
    if (!is) throw IndexLoadError("corrupted index: unexpected EOF");
    return s;
}

}  // namespace

std::vector<ScannedFile> Scanner::scan(const std::string& root_dir) {
    std::vector<ScannedFile> out;
    if (!fs::exists(root_dir) || !fs::is_directory(root_dir)) {
        return out;
    }
    for (auto it = fs::recursive_directory_iterator(
             root_dir, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        if (!has_indexable_extension(it->path())) continue;
        ScannedFile f;
        f.path = it->path().string();
        f.modified_time = file_mtime(it->path());
        out.push_back(std::move(f));
    }
    return out;
}

std::string hash_path(const std::string& path) {
    // FNV-1a 64-bit — see header for the trade-off vs SHA-256.
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : path) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

void IndexStore::save(const InvertedIndex& index, const std::string& path) {
    fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path());
    }
    fs::path tmp = target;
    tmp += ".tmp";

    {
        std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
        if (!os) throw IndexLoadError("cannot open temp index file for writing: " + tmp.string());

        os.write("TSIX", 4);
        write_u32(os, kFormatVersion);

        // Compact: only live (non-tombstoned) documents get written, with
        // fresh dense ids 0..K-1. old_id -> new_id remap is used below when
        // writing postings.
        const auto& docs = index.raw_docs();
        const auto& tomb = index.tombstones();
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        std::vector<std::uint32_t> live_ids;
        for (std::uint32_t i = 0; i < docs.size(); ++i) {
            if (i < tomb.size() && tomb[i]) continue;
            remap[i] = static_cast<std::uint32_t>(live_ids.size());
            live_ids.push_back(i);
        }

        write_u64(os, live_ids.size());
        for (std::uint32_t old_id : live_ids) {
            const DocumentMeta& m = docs[old_id];
            write_str(os, m.id);
            write_str(os, m.path);
            write_str(os, m.title);
            write_u32(os, m.token_count);
            write_i64(os, m.modified_time);
        }

        const auto& terms = index.raw_terms();
        write_u64(os, terms.size());
        for (const auto& [term, plist] : terms) {
            write_str(os, term);
            // count only postings whose document is still live
            std::vector<const Posting*> live_postings;
            for (const auto& p : plist.postings) {
                if (remap.count(p.document_id)) live_postings.push_back(&p);
            }
            write_u32(os, static_cast<std::uint32_t>(live_postings.size()));
            for (const Posting* p : live_postings) {
                write_u32(os, remap[p->document_id]);
                write_u32(os, p->term_frequency);
                write_u32(os, static_cast<std::uint32_t>(p->positions.size()));
                for (std::uint32_t pos : p->positions) write_u32(os, pos);
            }
        }
        os.flush();
        if (!os) throw IndexLoadError("failed writing temp index file: " + tmp.string());
    }

    // Atomic on POSIX filesystems: rename() within the same filesystem.
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        throw IndexLoadError("failed to atomically replace index file: " + ec.message());
    }
}

void IndexStore::load(InvertedIndex& index, const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) throw IndexLoadError("index file not found: " + path);

    char magic[4] = {0};
    is.read(magic, 4);
    if (!is || std::string(magic, 4) != "TSIX") {
        throw IndexLoadError("not a TinySearch index file (bad magic)");
    }
    std::uint32_t version = read_u32(is);
    if (version != IndexStore::kFormatVersion) {
        throw IndexLoadError("unsupported index version " + std::to_string(version) +
                              " (expected " + std::to_string(IndexStore::kFormatVersion) + ")");
    }

    std::uint64_t doc_count = read_u64(is);
    std::vector<DocumentMeta> docs;
    std::vector<std::uint32_t> doc_lengths;
    docs.reserve(doc_count);
    doc_lengths.reserve(doc_count);
    for (std::uint64_t i = 0; i < doc_count; ++i) {
        DocumentMeta m;
        m.id = read_str(is);
        m.path = read_str(is);
        m.title = read_str(is);
        m.token_count = read_u32(is);
        m.modified_time = read_i64(is);
        doc_lengths.push_back(m.token_count);
        docs.push_back(std::move(m));
    }

    std::uint64_t term_count = read_u64(is);
    std::unordered_map<std::string, PostingList> terms;
    terms.reserve(term_count);
    for (std::uint64_t i = 0; i < term_count; ++i) {
        std::string term = read_str(is);
        std::uint32_t df = read_u32(is);
        PostingList pl;
        pl.postings.reserve(df);
        for (std::uint32_t j = 0; j < df; ++j) {
            Posting p;
            p.document_id = read_u32(is);
            p.term_frequency = read_u32(is);
            std::uint32_t pos_count = read_u32(is);
            p.positions.reserve(pos_count);
            for (std::uint32_t k = 0; k < pos_count; ++k) p.positions.push_back(read_u32(is));
            pl.postings.push_back(std::move(p));
        }
        pl.document_frequency = static_cast<std::uint32_t>(pl.postings.size());
        terms.emplace(std::move(term), std::move(pl));
    }

    index.load_raw(std::move(docs), std::move(doc_lengths), std::move(terms));
}

}  // namespace tinysearch
