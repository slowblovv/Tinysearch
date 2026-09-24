# TinySearch — Ranking

## TF-IDF: the baseline

TF-IDF scores a (term, document) pair as **term frequency × inverse
document frequency**:

```
TFIDF(t, d) = TF(t, d) × IDF(t)
```

TinySearch uses the common log-dampened variant for TF, and a smoothed IDF:

```
TF'(t, d) = 1 + log(TF(t, d))          (only when TF(t,d) > 0)
IDF(t)    = log(N / df(t)) + 1
score     = TF'(t, d) × IDF(t)
```

(`Scorer::tfidf_score` / `Scorer::idf_tfidf` in `scorer.cpp`.)

### Limitations of plain TF-IDF

1. **No term-frequency saturation.** Even with the `log` dampening above,
   TF-IDF has no ceiling: a document that repeats a query term 50 times
   scores meaningfully higher than one that mentions it 5 times, even
   though after a handful of occurrences a human reader gets essentially
   no more evidence of relevance. This makes naive TF-IDF gameable by
   keyword stuffing.
2. **No document-length normalization.** A query term appearing twice in a
   50-word document is much stronger evidence of relevance than the same
   term appearing twice in a 5,000-word document, but plain TF-IDF scores
   them identically (for the same raw TF). Long documents have a structural
   advantage: more words means more chances to match query terms at all,
   independent of actual topical relevance.

## BM25: the main ranker

BM25 (Best Matching 25) fixes both problems with two extra terms:

```
score(D, Q) = Σ_{t ∈ Q}  IDF(t) · f(t,D)·(k1+1)
                          ─────────────────────────────────────
                          f(t,D) + k1·(1 - b + b·|D|/avgdl)
```

- **`k1`** (default `1.2`) controls TF saturation: as `f(t,D) → ∞`, the
  fraction approaches `k1 + 1`, an explicit ceiling. Higher `k1` lets more
  term repetitions still add score; `k1 = 0` would ignore TF entirely
  beyond "present vs. absent."
- **`b`** (default `0.75`, range `[0, 1]`) controls length normalization:
  `b = 1` fully normalizes by document length (relative to `avgdl`, the
  average document length across the collection); `b = 0` disables length
  normalization entirely, making BM25 behave like a saturating TF-IDF.
  `tests/bm25_test.cpp::bm25_b_zero_disables_length_normalization` checks
  exactly this.
- **IDF** uses the standard probabilistic form,
  `log(1 + (N - df + 0.5) / (df + 0.5))`, floored at a small epsilon so an
  extremely common term (df close to N) contributes ~nothing rather than a
  *negative* score that would invert the ranking of otherwise-similar
  documents (`Scorer::idf_bm25`).

Both `k1` and `b` are configurable (`tinysearch.conf`: `bm25.k1`, `bm25.b`,
or `Bm25Params` directly), as required.

## Phrase and boolean queries under both rankers

A phrase match is scored as a single pseudo-term: its "term frequency" is
the number of phrase occurrences in the document, and its "document
frequency" is the number of documents the phrase occurs in at all — both
computed once by `SearchEngine::match_phrase` (see `architecture.md`, "Why
posting lists"). `NOT`-excluded terms never contribute to scoring (see
`collect_scoring_terms` in `architecture.md`) — only whether they exclude a
document from the candidate set.

## Design decisions found by benchmarking, not just theory

`benchmark/search_benchmark.cpp` initially measured phrase-query p50
latency at **~45 seconds** on a 10,000-document synthetic corpus. Two real
bugs, not just "acceptable v1 simplifications," were hiding behind that
number:

1. `match_phrase`'s per-candidate lookup of "does the other term also
   occur in this document?" was a **linear scan** of the other term's
   entire posting list, for every posting in the first term's list — an
   `O(df₁ × df₂)` nested scan. Fix: build an `unordered_map<doc_id,
   Posting*>` for each other term once, turning the per-candidate lookup
   into `O(1)`. This alone brought p50 down to ~7.5 s.
2. The real killer: `score_document()` was calling `match_phrase()` **from
   scratch, once per candidate document**, inside the top-K scoring loop —
   turning one `O(df)` computation into `N_candidates × O(df)`. Fix:
   compute `match_phrase()` once per distinct phrase node *before* the
   scoring loop in `search()`, and pass the precomputed map down. Final
   p50: **~8.6 ms** — roughly a **5,300x** improvement from the original
   number, on the same corpus and query.

This is exactly the kind of thing a real benchmark is supposed to catch,
and is worth remembering as a general lesson: don't compute the same
posting-list-scanning work once per document when it only depends on the
query.

## Ranking evaluation — real measured numbers

`benchmark/ranking_evaluation.cpp` indexes `examples/documents/` (25
short Markdown articles across five topics: machine learning, databases,
networking, security, and computer graphics) and runs every query in
`queries/evaluation.json` (33 queries, each with a hand-labeled relevant
set) against both rankers, computing:

```
Precision@5 = relevant documents in top 5 / 5
Recall@10   = relevant documents in top 10 / total relevant for that query
MRR         = mean of 1/rank_of_first_relevant_result (0 if none in top 10)
```

Measured output (`build/ranking_evaluation examples/documents queries/evaluation.json`):

```
Ranker       Precision@5   Recall@10       MRR
----------------------------------------------
TF-IDF             0.248       0.879     0.970
BM25               0.248       0.879     0.970
```

**Honest reading of this table:** on this corpus, the two rankers produce
*identical* aggregate metrics. That's a real, measured result — not a
failure to find a difference worth hiding. It has a straightforward
explanation:

- All 25 example documents are similar, short lengths (roughly 40-60 words
  each). BM25's main advantage over TF-IDF — penalizing documents that are
  long relative to the collection average — has almost nothing to bite on
  when every document is close to `avgdl` already; the `b` term in BM25's
  denominator is close to a constant across documents.
- The evaluation queries are short (1-3 terms) and the documents are short
  enough that no query term ever appears more than 2-3 times in any single
  document, so TF-IDF's lack of TF saturation never actually saturates
  differently than BM25's — there's no keyword-stuffed document in this
  corpus to punish.
- With few, short documents, the two rankers also end up agreeing on the
  *order* of candidates for a query even where their raw scores differ
  numerically (relevant docs already tend to have the highest raw term
  overlap by a wide margin).

**Where BM25 should be expected to pull ahead** (and the honest thing to
say in an interview about why it's still the default): a corpus with (a)
widely varying document lengths — long reference pages mixed with short
notes — and (b) enough text per document that keyword repetition varies
meaningfully. `indexing_benchmark`'s synthetic corpus (120 words/doc,
uniform) also isn't stressing enough on length variance to show a gap;
reproducing a bigger gap would need a corpus built specifically to vary
document length and TF, which is a natural follow-up experiment, not
something to fake numbers for here.

**Precision@5 of 0.248 explained:** the metric divides by a fixed `5`,
regardless of how many relevant documents actually exist for a query — and
21 of the 33 evaluation queries have only 1-2 hand-labeled relevant
documents. A query with exactly one relevant document has a maximum
possible Precision@5 of `0.2` no matter how good the ranker is; the
measured `0.248` average reflects that ceiling, not a ranking failure —
cross-checked against the much healthier `0.879` Recall@10, which confirms
the relevant document is nearly always being found, just not filling up a
top-5 window it was never going to fill.

## Re-running the evaluation

```bash
cmake --build build --target ranking_evaluation
./build/ranking_evaluation examples/documents queries/evaluation.json
```
