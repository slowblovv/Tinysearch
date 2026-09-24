# TinySearch — Architecture

This document explains the *why* behind TinySearch's design. For the *what*
(build/run instructions, CLI, API), see [`README.md`](../README.md). For
ranking specifically, see [`ranking.md`](ranking.md).

## Pipeline overview

```
documents/  →  Scanner  →  Tokenizer  →  Normalizer  →  InvertedIndex
                                                              │
                                                              ▼
query text  →  QueryParser  →  AST  →  SearchEngine::evaluate (boolean)
                                              │
                                              ▼
                                   candidate document ids
                                              │
                                              ▼
                                   Scorer (TF-IDF / BM25)  →  top-K heap
                                              │
                                              ▼
                                   SnippetGenerator  →  SearchResult[]
```

Every box above is its own translation unit under `include/tinysearch/` and
`src/`, on purpose: each stage is independently unit-testable (see
`tests/`), and none of them know about HTTP, the CLI, or persistence.
`SearchEngine` (`search_engine.h/.cpp`) is the only class that ties the
pipeline together; `main.cpp` and `http_server.cpp` are thin adapters on
top of it.

## Why an inverted index?

A **forward index** (`document → words`) answers "what words are in this
document?" — useful for nothing a search engine needs. A query like
`machine learning` needs the opposite question answered: "which documents
contain `machine`?". Without an inverted index, answering that requires
scanning every document — O(N) work per query term, where N is the whole
collection. An **inverted index** (`word → documents`) turns that into a
single hash-map lookup: O(1) to find the term, O(df) to walk its postings,
where df (document frequency) is usually orders of magnitude smaller than N.
This is *the* foundational idea of information retrieval, and the reason
TinySearch exists as a learning project.

## Why posting lists, and why store positions?

A posting list is the list of `(document, term_frequency, positions)`
tuples for one term. `term_frequency` alone is enough for TF-IDF/BM25
ranking (bag-of-words scoring). Storing `positions` — the token offsets
where the term occurs — costs extra space (roughly 4 bytes per occurrence)
but is what makes **phrase search** (`"machine learning"`) possible at all:
without positions, "documents containing `machine` and `learning`" and
"documents containing the exact phrase `machine learning`" are
indistinguishable. TinySearch's phrase matcher (`SearchEngine::match_phrase`)
walks the first term's positions and checks, for each candidate start `s`,
whether the i-th other term has a position at `s + i` — an O(df₁ · df) check
using a per-term hash map keyed by document id, plus a binary search into
each document's (sorted) position list.

**Important limitation, stated plainly:** positions are assigned to the
token stream *after* stop-word removal, and the whole document (title +
body, across paragraph and sentence boundaries) is treated as one
contiguous stream. Two consequences:

1. A phrase that only appears "adjacent" because a stop word was silently
   removed between them (e.g. "TF-IDF *and* BM25" indexing as `tfidf bm25`
   adjacent) will match a phrase query for `"tfidf bm25"` even though the
   original text had a word between them.
2. Because there's no sentence/paragraph boundary marker, the last word of
   one paragraph and the first word of the next are treated as adjacent.
   This can very occasionally produce a phrase "match" that doesn't exist
   in the rendered document (this surfaced in `tests/search_test.cpp`
   while writing tests — see the comment there for a worked example).

A production engine would insert a large position gap (or a sentinel) at
paragraph/sentence boundaries to prevent this. TinySearch v1 doesn't, to
keep indexing and the position model simple; it's the single biggest
correctness caveat worth knowing about before trusting phrase search on
prose with lots of stop words.

## Why two ids per document?

Every document has both a **stable id** (`hash_path(path)` — a 64-bit
FNV-1a hash of the file path, hex-encoded) and an **internal id** (a dense
`uint32_t`, the index into `InvertedIndex`'s internal `docs_` vector).
The stable id survives re-indexing (same path ⇒ same id, which is what
lets incremental indexing detect "this is the same document, is it
changed?"), and is what the API and persisted index expose externally.
The internal id exists purely so postings can store a compact 4-byte
integer instead of a variable-length string, and so `document_length()` /
`meta_for()` are O(1) array indexing rather than hash lookups. Section 10
of the spec suggested SHA-256(path) for the stable id; TinySearch uses
FNV-1a instead — cryptographic strength isn't needed for id stability in a
personal/small-corpus search engine, and it avoids a crypto dependency
just for id generation.

## Why BM25, not just TF-IDF?

See [`ranking.md`](ranking.md) for the full comparison, formulas, and a
real measured evaluation table. Short version: BM25 fixes two things
TF-IDF's classic formulation gets wrong — it saturates term frequency
(a term appearing 100 times isn't 100x as relevant as it appearing once)
and it normalizes for document length, so a paragraph and a book aren't
compared on equal footing. Both rankers are implemented and selectable
(`--ranker tfidf|bm25`) specifically so the difference is something you
can *measure*, not just take on faith.

## How AND / OR / NOT are executed

The query parser builds an AST (`query.h`); `SearchEngine::evaluate()`
walks it recursively and returns a **sorted `vector<uint32_t>`** of
matching internal document ids at every node:

- `TERM` → the posting list's document ids, sorted.
- `PHRASE` → the keys of the phrase-match map (see above), sorted.
- `AND` → `std::set_intersection` of the two children.
- `OR` → `std::set_union` of the two children.
- `NOT` → `std::set_difference(all_document_ids(), evaluate(child))`.

Keeping ids sorted throughout is what makes the merge-based set operations
linear instead of quadratic. `NOT` is evaluated as "all documents minus the
operand," which is O(N) rather than lazy — acceptable for the personal/
small-corpus scale TinySearch targets (see spec section 80, "what not to
do"), and simple to reason about since it composes correctly inside
`AND`/`OR` (e.g. `machine AND NOT biology` still narrows correctly, because
intersecting with "everything except biology" is the same as subtracting
biology's ids from machine's).

Ranking is computed **separately** from boolean evaluation: after finding
the candidate set, `collect_scoring_terms()` walks the same AST and
collects every `TERM`/`PHRASE` leaf that isn't inside a `NOT` subtree (a
`negated` flag flips at each `NOT` and propagates down). Those leaves —
not the boolean structure itself — are what get scored and summed. This is
a common, deliberate simplification: e.g. `(a OR b) AND c` scores as
`score(a) + score(b) + score(c)` for a document, rather than modeling the
OR/AND structure into a compound scoring function. It's what most small
search engines do, and keeps the scorer decoupled from parsing.

## Why top-K via a min-heap?

Once candidates are scored, TinySearch never sorts the full candidate list.
It keeps a `std::priority_queue` (min-heap) of at most `top_k` elements:
push while under capacity, then pop-and-push only when a new score beats
the current minimum. This is `O(N log K)` instead of the `O(N log N)` a
full sort would cost — for `K=10` against `N=100,000` candidates that's
roughly 17x fewer comparisons in the heap-maintenance step. The heap is
drained and sorted (`O(K log K)`) only at the very end, once it's already
small.

## Why "build new index → atomic swap" for indexing?

Spec section 54 asks for read/write consistency: a search running during
`POST /index` must never see a half-built index. TinySearch takes approach
B — build the new index off to the side, then swap a pointer:

```
shared_lock  → copy current index (readers keep using the old one)
(no lock)    → mutate the copy: add/update/remove documents from disk scan
unique_lock  → index_ = std::move(new_index)   // atomic pointer swap
```

The copy step is `O(index size)` (an `InvertedIndex` copy), which is the
honest cost of this approach — it trades index size for the guarantee that
`search()` never has to know indexing is happening. `std::shared_mutex`
gives many concurrent readers (searches) or one exclusive writer
(the swap) — see `cache.h`/`search_engine.h`. The lock is held only for the
pointer assignment itself, not for the (much slower) file re-scan/parse
work, so a long re-index doesn't block searches for its whole duration.

## How the cache works

`LruCache<SearchResponse>` (`cache.h`) is a textbook doubly-linked-list +
hash-map LRU, guarded by its own `std::mutex` (independent of the index's
`shared_mutex` — the cache doesn't need to be consistent with the index at
sub-request granularity, just invalidated whenever the index changes,
which `index_directory()`/`load()` do explicitly). The cache key is
`ranker | top_k | raw_query_text` — *not* normalized, to keep the key
construction free of any query-parsing cost on the hot cache-hit path;
this means `"Machine Learning"` and `"machine learning"` are cached
separately even though they'd normalize to the same query, a small
memory-for-simplicity trade-off.

## How incremental indexing works

`index_directory(dir, incremental=true)`:

1. Scans the directory for `.md`/`.txt` files, recording `(path,
   modified_time)` for each (`Scanner::scan`, using `stat()`'s `st_mtime`).
2. For each scanned file, computes its stable id and checks the *working
   copy* of the index (see "atomic swap" above): if a document with that id
   already exists **and** its path/modified_time are unchanged, it's
   `unchanged` — the file is not re-read, tokenized, or re-indexed at all,
   which is the actual performance win of incremental indexing.
3. Otherwise the file is read, tokenized, and `add_document()`'d — which is
   `added` if this id was never seen before, `updated` if it was.
4. Any document the working copy already knew about, whose id wasn't seen
   in this scan, is `deleted` (its source file is gone).
5. The working copy replaces the live index (atomic swap, see above).

## Why no third-party JSON/HTTP library?

The spec allows a JSON library and an HTTP framework (section 4).
This environment had no network access to fetch and vendor a header-only
dependency like `nlohmann/json` or `cpp-httplib`, so TinySearch ships:

- `json_util.h`: a ~100-line writer (proper string escaping) and a reader
  limited to the exact fixed-shape request/response bodies this API needs
  (`{"path": "..."}`, and the evaluation dataset's `[{"query": ..., "relevant": [...]}]`).
  It is explicitly *not* a general JSON parser.
- `http_server.h/.cpp`: a ~200-line blocking, thread-per-connection
  HTTP/1.1 server over raw POSIX sockets — just enough GET/POST, headers,
  and query-string parsing for five endpoints. `select()` with a 200ms
  timeout on the accept loop is what makes graceful shutdown possible
  without needing a self-pipe or eventfd trick.

If you have network access in your own environment, swapping either of
these for `nlohmann/json` / `cpp-httplib` is a self-contained change (the
`HttpServer`/`json` interfaces are narrow) and would remove some sharp
edges (e.g. the JSON reader would reject nested objects it doesn't expect).

## Concurrency model, summarized

| Actor                          | Lock                                  |
|---------------------------------|----------------------------------------|
| `search()`                      | `shared_lock` on `SearchEngine::mutex_` |
| `index_directory()` (build step)| none (works on a private copy)         |
| `index_directory()` (swap step) | `unique_lock` on `SearchEngine::mutex_` |
| `LruCache` get/put               | its own internal `std::mutex`          |
| HTTP server                      | one OS thread per connection            |

This means: unbounded concurrent searches, one indexing operation at a
time (a second `POST /index` while one is running will queue behind the
lock only for the brief swap, not the whole rebuild — but doing two
rebuilds "at once" still means two full copies of the index in memory
simultaneously, which is a real resource cost worth knowing about for
very large corpora).
