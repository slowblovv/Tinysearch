#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "tinysearch/config.h"
#include "tinysearch/http_server.h"
#include "tinysearch/json_util.h"
#include "tinysearch/search_engine.h"

namespace fs = std::filesystem;
using namespace tinysearch;

namespace {

HttpServer* g_server = nullptr;

void handle_signal(int) {
    if (g_server) g_server->stop();
}

RankerType parse_ranker(const std::string& s) {
    return (s == "tfidf" || s == "tf-idf" || s == "tf_idf") ? RankerType::TF_IDF : RankerType::BM25;
}

std::string get_flag(const std::vector<std::string>& args, const std::string& name,
                      const std::string& fallback) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name && i + 1 < args.size()) return args[i + 1];
    }
    return fallback;
}

void print_report(const IndexReport& r) {
    std::cout << "Scanning...\n";
    std::cout << "Documents found: " << r.documents_found << "\n\n";
    std::cout << "Added:       " << r.added << "\n";
    std::cout << "Updated:     " << r.updated << "\n";
    std::cout << "Deleted:     " << r.deleted << "\n";
    std::cout << "Unchanged: " << r.unchanged << "\n";
    std::cout << "Errors:      " << r.errors << "\n";
    if (!r.error_details.empty()) {
        std::cout << "\nErrors:\n";
        for (auto& [path, reason] : r.error_details) {
            std::cout << "  " << path << " -> " << reason << "\n";
        }
    }
    std::cout << "\nTime: " << std::fixed << std::setprecision(2) << (r.duration_ms / 1000.0)
               << " sec\n";
}

void print_stats(const EngineStats& s) {
    std::cout << "Documents:      " << s.documents << "\n";
    std::cout << "Unique terms:   " << s.terms << "\n";
    std::cout << "Total tokens:   " << s.total_tokens << "\n";
    std::cout << "Average doc length: " << std::fixed << std::setprecision(1)
               << s.average_document_length << "\n";
    std::cout << "Index size (est): " << std::fixed << std::setprecision(2)
               << (static_cast<double>(s.index_size_bytes) / (1024.0 * 1024.0)) << " MB\n\n";
    std::cout << "Cache:\n";
    std::cout << "Hits:   " << s.cache_hits << "\n";
    std::cout << "Misses: " << s.cache_misses << "\n";
    std::uint64_t total = s.cache_hits + s.cache_misses;
    double rate = total > 0 ? 100.0 * static_cast<double>(s.cache_hits) / static_cast<double>(total) : 0.0;
    std::cout << "Hit rate: " << std::fixed << std::setprecision(1) << rate << "%\n";
}

// --- JSON serialization for the API --------------------------------------

std::string result_to_json(const SearchResult& r) {
    std::ostringstream o;
    o << "{"
      << "\"document_id\":" << json::quote(r.document_id) << ","
      << "\"score\":" << r.score << ","
      << "\"title\":" << json::quote(r.title) << ","
      << "\"path\":" << json::quote(r.path) << ","
      << "\"snippet\":" << json::quote(r.snippet) << "}";
    return o.str();
}

std::string response_to_json(const SearchResponse& resp) {
    std::ostringstream o;
    o << "{"
      << "\"query\":" << json::quote(resp.query) << ","
      << "\"ranker\":" << json::quote(resp.ranker) << ","
      << "\"total_candidates\":" << resp.total_candidates << ","
      << "\"duration_ms\":" << resp.duration_ms << ","
      << "\"cache_hit\":" << (resp.cache_hit ? "true" : "false") << ","
      << "\"results\":[";
    for (std::size_t i = 0; i < resp.results.size(); ++i) {
        if (i) o << ",";
        o << result_to_json(resp.results[i]);
    }
    o << "]}";
    return o.str();
}

int run_serve(SearchEngine& engine, const Config& cfg) {
    HttpServer server(cfg.host, cfg.port);
    g_server = &server;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    server.on("GET", "/health", [](const HttpRequest&) {
        return HttpResponse::json(200, R"({"status":"ok"})");
    });

    server.on("GET", "/stats", [&engine](const HttpRequest&) {
        EngineStats s = engine.stats();
        std::ostringstream o;
        o << "{"
          << "\"documents\":" << s.documents << ","
          << "\"terms\":" << s.terms << ","
          << "\"total_tokens\":" << s.total_tokens << ","
          << "\"average_document_length\":" << s.average_document_length << ","
          << "\"index_size_bytes\":" << s.index_size_bytes << ","
          << "\"cache_hits\":" << s.cache_hits << ","
          << "\"cache_misses\":" << s.cache_misses << "}";
        return HttpResponse::json(200, o.str());
    });

    server.on("GET", "/search", [&engine, &cfg](const HttpRequest& req) {
        auto qit = req.query.find("q");
        if (qit == req.query.end() || qit->second.empty()) {
            return HttpResponse::json(400, R"({"error":"missing required query param 'q'"})");
        }
        std::size_t top_k = cfg.default_top_k;
        auto tk = req.query.find("top_k");
        if (tk != req.query.end()) {
            try {
                top_k = std::min<std::size_t>(std::stoul(tk->second), cfg.max_top_k);
            } catch (...) {
            }
        }
        RankerType ranker = cfg.default_ranker;
        auto rk = req.query.find("ranker");
        if (rk != req.query.end()) ranker = parse_ranker(rk->second);

        try {
            SearchResponse resp = engine.search(qit->second, ranker, top_k);
            return HttpResponse::json(200, response_to_json(resp));
        } catch (const QueryParseError& e) {
            std::ostringstream o;
            o << "{\"error\":" << json::quote(e.what()) << "}";
            return HttpResponse::json(400, o.str());
        }
    });

    server.on("POST", "/index", [&engine](const HttpRequest& req) {
        auto path = json::extract_string_field(req.body, "path");
        if (!path) {
            return HttpResponse::json(400, R"({"error":"missing required field 'path'"})");
        }
        IndexReport r = engine.index_directory(*path);
        std::ostringstream o;
        o << "{"
          << "\"documents_indexed\":" << (r.added + r.updated + r.unchanged) << ","
          << "\"added\":" << r.added << ","
          << "\"updated\":" << r.updated << ","
          << "\"deleted\":" << r.deleted << ","
          << "\"errors\":" << r.errors << ","
          << "\"duration_ms\":" << r.duration_ms << "}";
        return HttpResponse::json(200, o.str());
    });

    server.on_prefix("GET", "/documents/", [&engine](const HttpRequest& req) {
        std::string id = req.path;  // remainder after "/documents/"
        const DocumentMeta* meta = engine.get_document(id);
        if (meta == nullptr) {
            return HttpResponse::json(404, R"({"error":"document not found"})");
        }
        std::ostringstream o;
        o << "{"
          << "\"id\":" << json::quote(meta->id) << ","
          << "\"title\":" << json::quote(meta->title) << ","
          << "\"path\":" << json::quote(meta->path) << ","
          << "\"token_count\":" << meta->token_count << "}";
        return HttpResponse::json(200, o.str());
    });

    server.run();  // blocks until stop()

    // Graceful shutdown: persist the index so the next `serve` doesn't have
    // to rescan everything (spec section 73).
    try {
        engine.save(cfg.index_path);
        std::cout << "Index saved to " << cfg.index_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "warning: failed to save index on shutdown: " << e.what() << "\n";
    }
    return 0;
}

void try_load_existing_index(SearchEngine& engine, const Config& cfg) {
    if (fs::exists(cfg.index_path)) {
        try {
            engine.load(cfg.index_path);
        } catch (const std::exception& e) {
            std::cerr << "warning: could not load existing index (" << e.what()
                      << "); starting empty\n";
        }
    }
}

void print_usage() {
    std::cout <<
        "TinySearch - a small local search engine\n\n"
        "Usage:\n"
        "  tinysearch index <directory>              Index (or re-index) a directory\n"
        "  tinysearch search \"<query>\" [--top N] [--ranker bm25|tfidf]\n"
        "  tinysearch stats                          Show index statistics\n"
        "  tinysearch save [path]                    Save the current index to disk\n"
        "  tinysearch serve [--host H] [--port P] [--index path]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 1;
    }

    Config cfg = Config::load_from_file("tinysearch.conf");
    std::string cmd = args[0];

    if (cmd == "index") {
        if (args.size() < 2) {
            std::cerr << "usage: tinysearch index <directory>\n";
            return 1;
        }
        SearchEngine engine(cfg);
        try_load_existing_index(engine, cfg);
        IndexReport r = engine.index_directory(args[1]);
        print_report(r);
        engine.save(cfg.index_path);
        return r.errors > 0 ? 1 : 0;
    }

    if (cmd == "search") {
        if (args.size() < 2) {
            std::cerr << "usage: tinysearch search \"<query>\" [--top N] [--ranker bm25|tfidf]\n";
            return 1;
        }
        SearchEngine engine(cfg);
        if (!fs::exists(cfg.index_path)) {
            std::cerr << "no index found at " << cfg.index_path << " — run `tinysearch index <dir>` first\n";
            return 1;
        }
        engine.load(cfg.index_path);

        std::size_t top_k = std::stoul(get_flag(args, "--top", std::to_string(cfg.default_top_k)));
        RankerType ranker = parse_ranker(get_flag(args, "--ranker", "bm25"));

        try {
            SearchResponse resp = engine.search(args[1], ranker, top_k);
            std::cout << "query: " << resp.query << " (ranker=" << resp.ranker
                       << ", candidates=" << resp.total_candidates << ", "
                       << std::fixed << std::setprecision(2) << resp.duration_ms << " ms)\n\n";
            int rank = 1;
            for (auto& r : resp.results) {
                std::cout << rank++ << ". [" << std::fixed << std::setprecision(3) << r.score
                           << "] " << r.title << "  (" << r.path << ")\n";
                std::cout << "   " << r.snippet << "\n\n";
            }
            if (resp.results.empty()) std::cout << "(no results)\n";
        } catch (const QueryParseError& e) {
            std::cerr << "invalid query: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (cmd == "stats") {
        SearchEngine engine(cfg);
        if (!fs::exists(cfg.index_path)) {
            std::cerr << "no index found at " << cfg.index_path << "\n";
            return 1;
        }
        engine.load(cfg.index_path);
        print_stats(engine.stats());
        return 0;
    }

    if (cmd == "save") {
        std::cerr << "note: `index` and `serve` already save automatically; "
                     "nothing to do without a running in-memory session.\n";
        return 0;
    }

    if (cmd == "serve") {
        cfg.host = get_flag(args, "--host", cfg.host);
        cfg.port = std::stoi(get_flag(args, "--port", std::to_string(cfg.port)));
        cfg.index_path = get_flag(args, "--index", cfg.index_path);

        SearchEngine engine(cfg);
        try_load_existing_index(engine, cfg);
        return run_serve(engine, cfg);
    }

    print_usage();
    return 1;
}
