# TinySearch

A lightweight full-text search engine built from scratch — inverted index,
BM25 ranking, phrase search, boolean queries, incremental indexing, and a
REST API — with no Elasticsearch, no Lucene, no third-party search library.
Everything under `indexing → tokenization → ranking → query execution` is a
from-scratch implementation; only the standard library, POSIX sockets, and
the C++ compiler are used (see [Design decisions](#design-decisions) for
why there's no vendored JSON/HTTP/test library either).

This is the third in a three-project sequence (`PocketKV` → systems/C++,
`TinyQueue` → backend/PostgreSQL, **TinySearch** → information retrieval),
meant as a stepping stone toward embeddings/RAG — but deliberately **not**
an AI project itself. See [Limitations](#limitations) for what's
intentionally out of scope.

## 1. What is TinySearch?

Point it at a directory of Markdown/text files:

```bash
./build/tinysearch index ./examples/documents
./build/tinysearch search "machine learning" --ranker bm25
```

or run it as a small HTTP service:

```bash
./build/tinysearch serve --port 8080
curl "http://127.0.0.1:8080/search?q=machine+learning&ranker=bm25"
```

```json
{
  "query": "machine learning",
  "ranker": "bm25",
  "total_candidates": 6,
  "results": [
    {
      "document_id": "a1b2c3d4e5f60718",
      "score": 3.241,
      "title": "Machine Learning Training Infrastructure",
      "path": "examples/documents/ml/model_training_infra.md",
      "snippet": "Training large <mark>machine</mark> <mark>learning</mark> models requires..."
    }
  ]
}
```

## 2. Architecture

Full write-up, with rationale for every design choice, in
[`docs/architecture.md`](docs/architecture.md):

```
documents → Scanner → Tokenizer → Normalizer → InvertedIndex
query text → QueryParser → AST → boolean evaluation → BM25/TF-IDF → top-K → snippets
```

## 3. Quick start

Requires a C++20 compiler and CMake ≥ 3.16 (no other dependencies —
everything below is standard library + POSIX):

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
ctest                       # run the test suite
./tinysearch index ../examples/documents
./tinysearch search "database transaction"
```

## 4. Indexing

```bash
tinysearch index <directory>
```

Recursively scans `<directory>` for `.md` and `.txt` files, tokenizes and
normalizes their content (lowercase, stop-word removal, light stemming —
see [`docs/architecture.md`](docs/architecture.md)), builds the inverted
index, and saves it to `data/index.dat` (configurable via
`tinysearch.conf`'s `index_path`). Re-running `index` on the same directory
is **incremental**: unchanged files (same path, same mtime) are skipped
entirely; changed files are re-indexed; files that disappeared are removed
from the index. Example output:

```
Scanning...
Documents found: 25

Added:       25
Updated:     0
Deleted:     0
Unchanged: 0
Errors:      0

Time: 0.01 sec
```

## 5. Query syntax

| Query                              | Meaning                                   |
|-------------------------------------|--------------------------------------------|
| `machine learning`                  | implicit AND — both terms must be present  |
| `machine AND learning`              | same as above, explicit                    |
| `machine OR learning`               | either term                                |
| `machine NOT biology`               | contains "machine", excludes "biology"     |
| `"machine learning"`                | exact phrase, words must be adjacent       |
| `(machine OR deep) AND learning`    | parentheses for grouping                   |

Precedence (spec-mandated, and tested in `tests/parser_test.cpp`):
`NOT` > `AND` > `OR`, so `a OR b AND c` parses as `a OR (b AND c)`.
Malformed queries (`machine AND`, unbalanced parens/quotes, empty query,
oversized query) raise a `QueryParseError`, surfaced as CLI error text or
an HTTP `400`.

**Not implemented in v1** (see [Roadmap](#roadmap)): prefix search (`mach*`)
and field search (`title:machine`) — both explicitly marked optional/v2 in
the spec.

## 6. Ranking

Two rankers, selectable per-query:

```bash
tinysearch search "machine learning" --ranker bm25    # default
tinysearch search "machine learning" --ranker tfidf
```

BM25's `k1`/`b` parameters are configurable (`tinysearch.conf`). Full
formulas, trade-offs, and a **real measured** evaluation comparing the two
rankers are in [`docs/ranking.md`](docs/ranking.md) — short version: BM25
saturates term frequency and normalizes for document length; TF-IDF does
neither. On the small, uniform example corpus shipped here the two
rankers measure identically (explained in the doc); BM25 is still the
default because it degrades to TF-IDF-like behavior in that case and only
gets *better* as documents vary more in length and repetition.

## 7. API

| Endpoint                     | Method | Description                          |
|-------------------------------|--------|---------------------------------------|
| `/search?q=...&ranker=&top_k=`| GET    | Run a query, get ranked results       |
| `/index`                      | POST   | `{"path": "..."}` — (re)index a dir   |
| `/documents/{id}`              | GET    | Metadata for one document             |
| `/stats`                      | GET    | Index/cache statistics                |
| `/health`                     | GET    | `{"status": "ok"}`                    |

Limits: query ≤ 4 KB, `top_k` ≤ 100, phrase ≤ 32 terms (enforced by the
parser and by the API layer clamping `top_k`).

## 8. Persistence

`tinysearch index` and `tinysearch serve` (on graceful shutdown) save the
index to a versioned custom binary format (`data/index.dat` by default) via
write-to-temp-file-then-atomic-rename, so a crash mid-write never leaves a
corrupted primary index file behind (`IndexStore::save`/`load`,
`tests/persistence_test.cpp` covers the crash-safety and version-mismatch
cases). `serve --index <path>` loads an existing index without rescanning
the source directory.

## 9. Incremental indexing

See [`docs/architecture.md`](docs/architecture.md#how-incremental-indexing-works)
for the full mechanism (comparison against path+mtime, atomic swap so
searches never see a half-built index). Deleted source files are detected
and removed from the index on the next `index` run.

## 10. Cache

An LRU cache (`cache.h`) memoizes full search responses, keyed by
`ranker | top_k | raw query text`, default capacity 1000 entries.
Automatically invalidated whenever the index changes. `tinysearch stats`
and `GET /stats` report hit/miss counts and rate.

## 11. Benchmarks

```bash
cmake --build build --target indexing_benchmark search_benchmark
./build/indexing_benchmark 100000
./build/search_benchmark 10000
```

Real measured numbers (this environment; synthetic corpora, see the
benchmark source for exact generation parameters):

**Indexing throughput:**

| Documents | Time    | Docs/sec | MB/sec | Index size |
|-----------|---------|----------|--------|------------|
| 100       | 0.00 s  | ~25,400  | 27.6   | 0.10 MB    |
| 1,000     | 0.04 s  | ~23,900  | 25.9   | 0.95 MB    |
| 10,000    | 0.43 s  | ~23,000  | 25.0   | 9.52 MB    |
| 100,000   | 9.73 s  | ~10,300  | 11.1   | 95.4 MB    |

**Search latency, 10,000-doc corpus** (cold = first time a query/combo is
seen; warm = same query repeated, 200 iterations):

| Query type   | Cold p50   | Warm p50 | Warm p99 |
|--------------|------------|----------|----------|
| single term  | 21.4 ms    | 0.0041 ms| 0.0069 ms|
| AND          | 37.1 ms    | 0.0041 ms| 0.0061 ms|
| OR           | 37.0 ms    | 0.0037 ms| 0.0081 ms|
| phrase       | 8.6 ms     | 0.0038 ms| 0.0053 ms|

The phrase-query number has a story worth reading —
[`docs/ranking.md`'s "Design decisions"](docs/ranking.md#design-decisions-found-by-benchmarking-not-just-theory)
section describes a real bug the benchmark caught (cold phrase latency was
originally ~45 **seconds**, not milliseconds, until a redundant
per-candidate recomputation was fixed).

## 12. Evaluation

```bash
cmake --build build --target ranking_evaluation
./build/ranking_evaluation examples/documents queries/evaluation.json
```

Runs 33 hand-labeled queries (`queries/evaluation.json`) against the 25
example documents (`examples/documents/`) and reports Precision@5,
Recall@10, and MRR for both rankers. See
[`docs/ranking.md`](docs/ranking.md#ranking-evaluation--real-measured-numbers)
for the table and an honest discussion of what it does and doesn't show.

## 13. Design decisions

The most consequential ones, with rationale, live in
[`docs/architecture.md`](docs/architecture.md). Two worth calling out
explicitly here because they depart from a literal reading of the spec:

- **Document id**: 64-bit FNV-1a hash of the path, not SHA-256 (spec
  section 10's suggestion) — no crypto dependency needed for id stability
  at this scale.
- **No vendored JSON/HTTP/test libraries.** This environment had no
  network access to fetch `nlohmann/json`, `cpp-httplib`, or `GoogleTest`,
  so TinySearch ships minimal, purpose-built equivalents
  (`json_util.h`, `http_server.h/.cpp`, `tests/mini_test.h`) sized to
  exactly what this project needs — documented as trade-offs in the
  relevant headers, not hidden.

## 14. Limitations

- **Phrase positions ignore stop-word removal and paragraph/sentence
  boundaries** — see `docs/architecture.md`'s "Why posting lists" section
  for a concrete example of when this produces a technically-correct-but-
  surprising phrase match.
- **Stemming is a small suffix-stripping ruleset**, not Porter/Snowball —
  under-stems irregular words (`ran`/`run`), documented in
  `normalizer.h`.
- **`NOT` is O(N)** (all documents minus the operand), not lazily
  evaluated — fine at the personal/small-corpus scale this targets, not at
  web scale.
- **Internal document ids are never reclaimed** after an update (the old
  slot is tombstoned, a new one appended) — `IndexStore::save` compacts on
  write, but the in-memory index can accumulate tombstones across many
  update cycles between saves.
- **No prefix search, fuzzy search, field search, or BM25F** — explicitly
  deferred to v2 (spec sections 25, 81).
- No distributed search, sharding, replication, or any ML/embeddings —
  intentionally out of scope (spec section 80); that's a different,
  future project.

## 15. Roadmap (v2, per spec section 81)

- Prefix search (`mach*`) via a trie or sorted-dictionary scan.
- Fuzzy search (edit-distance tolerant matching).
- Field search (`title:machine`) and BM25F (per-field weighting).
- Query autocomplete.
- Hybrid lexical (BM25) + embedding similarity search — the natural bridge
  to the next project in the sequence (`LocalRAG`).
