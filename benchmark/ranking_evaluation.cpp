// Ranking evaluation harness (spec sections 55-57).
//
// Indexes examples/documents/, runs every query in queries/evaluation.json
// against both rankers, and reports real, measured Precision@5, Recall@10
// and MRR — no numbers in this file's output are hand-picked; they come
// straight out of SearchEngine::search().
//
// Usage: ranking_evaluation [corpus_dir] [evaluation_json]
//   defaults: examples/documents   queries/evaluation.json
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "tinysearch/config.h"
#include "tinysearch/index_store.h"
#include "tinysearch/json_util.h"
#include "tinysearch/search_engine.h"

using namespace tinysearch;

namespace {

struct EvalQuery {
    std::string query;
    std::unordered_set<std::string> relevant_ids;  // stable ids, see main()
};

struct Metrics {
    double precision_at_5 = 0.0;
    double recall_at_10 = 0.0;
    double mrr = 0.0;
};

Metrics evaluate(SearchEngine& engine, RankerType ranker, const std::vector<EvalQuery>& queries) {
    double sum_p5 = 0.0, sum_r10 = 0.0, sum_rr = 0.0;
    for (const auto& eq : queries) {
        auto resp = engine.search(eq.query, ranker, 10);

        int retrieved_at_5 = 0;
        int retrieved_at_10 = 0;
        int first_relevant_rank = 0;
        for (std::size_t i = 0; i < resp.results.size(); ++i) {
            bool is_relevant = eq.relevant_ids.count(resp.results[i].document_id) > 0;
            if (is_relevant) {
                if (i < 5) ++retrieved_at_5;
                ++retrieved_at_10;
                if (first_relevant_rank == 0) first_relevant_rank = static_cast<int>(i) + 1;
            }
        }
        sum_p5 += static_cast<double>(retrieved_at_5) / 5.0;
        std::size_t total_relevant = eq.relevant_ids.empty() ? 1 : eq.relevant_ids.size();
        sum_r10 += static_cast<double>(retrieved_at_10) / static_cast<double>(total_relevant);
        sum_rr += first_relevant_rank > 0 ? 1.0 / first_relevant_rank : 0.0;
    }
    std::size_t n = queries.size();
    Metrics m;
    m.precision_at_5 = sum_p5 / static_cast<double>(n);
    m.recall_at_10 = sum_r10 / static_cast<double>(n);
    m.mrr = sum_rr / static_cast<double>(n);
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_dir = argc > 1 ? argv[1] : "examples/documents";
    std::string eval_path = argc > 2 ? argv[2] : "queries/evaluation.json";

    std::ifstream in(eval_path);
    if (!in) {
        std::cerr << "cannot open evaluation dataset: " << eval_path << "\n";
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string raw = buf.str();

    std::vector<EvalQuery> queries;
    for (const auto& obj : json::split_top_level_objects(raw)) {
        auto q = json::extract_string_field(obj, "query");
        if (!q) continue;
        EvalQuery eq;
        eq.query = *q;
        for (const auto& rel_path : json::extract_string_array_field(obj, "relevant")) {
            eq.relevant_ids.insert(hash_path(corpus_dir + "/" + rel_path));
        }
        queries.push_back(std::move(eq));
    }

    if (queries.empty()) {
        std::cerr << "no queries loaded from " << eval_path << "\n";
        return 1;
    }

    Config cfg = Config::defaults();
    SearchEngine engine(cfg);
    IndexReport report = engine.index_directory(corpus_dir);

    std::cout << "TinySearch Ranking Evaluation\n";
    std::cout << "==============================\n\n";
    std::cout << "Corpus: " << report.added << " documents (" << corpus_dir << ")\n";
    std::cout << "Queries: " << queries.size() << " (" << eval_path << ")\n\n";

    Metrics bm25 = evaluate(engine, RankerType::BM25, queries);
    Metrics tfidf = evaluate(engine, RankerType::TF_IDF, queries);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << std::left << std::setw(10) << "Ranker" << std::right << std::setw(14)
               << "Precision@5" << std::setw(12) << "Recall@10" << std::setw(10) << "MRR" << "\n";
    std::cout << std::string(46, '-') << "\n";
    std::cout << std::left << std::setw(10) << "TF-IDF" << std::right << std::setw(14)
               << tfidf.precision_at_5 << std::setw(12) << tfidf.recall_at_10 << std::setw(10)
               << tfidf.mrr << "\n";
    std::cout << std::left << std::setw(10) << "BM25" << std::right << std::setw(14)
               << bm25.precision_at_5 << std::setw(12) << bm25.recall_at_10 << std::setw(10)
               << bm25.mrr << "\n";

    return 0;
}
