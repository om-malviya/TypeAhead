# Performance Report

Measured locally (Apple Silicon, macOS) against the live stack: C++ backend +
PostgreSQL + 3 Redis nodes, dataset = **312,444 queries** loaded from Wikipedia
Pageviews. Reproduce with `python3 scripts/bench.py`.

## 1. Suggestion latency

`/suggest`, 5,000 requests over a realistic mix of 2–3 character prefixes,
concurrency 16:

| Metric | Client-side (incl. Python/HTTP overhead) | Server-side (`/stats`) |
|---|---|---|
| average | 2.19 ms | **0.27 ms** |
| p50 | 1.85 ms | — |
| p95 | 3.52 ms | **0.59 ms** |
| p99 | 8.44 ms | — |
| throughput | ~7,270 req/s | — |

The server-side figures are the true compute/serve time; the client column
includes Python `urllib` + thread-pool overhead and is the end-to-end number a
caller would see from this harness. Postgres is **not** on the read path, which is
why even cache misses (in-memory Trie walk) stay sub-millisecond.

## 2. Cache hit rate

Starting from a cold cache, the same 5,000-request pass:

```
hits = 4555   misses = 445   hit_rate = 91.1%
```

Misses are computed from the Trie and back-filled into Redis; hits are a single
Redis round-trip. Hit rate rises with traffic because popular prefixes dominate
real query distributions.

## 3. Write reduction from batching

5,000 `POST /search` requests spread over 8 distinct terms:

```
searches submitted = 5000
flushes            = 2
SQL write statements = 2
rows upserted      = 16
=> 2500x fewer SQL writes than one-write-per-search
```

Two effects compound:
1. **Coalescing** — 5,000 searches over 8 unique terms collapse to ≤8 rows per
   flush in the in-memory buffer.
2. **Batching** — each flush is a single bulk `INSERT … ON CONFLICT DO UPDATE`,
   and only 2 flushes occurred in the window.

The cumulative `write_reduction_ratio` reported by `/stats`
(searches ÷ SQL statements) was **1262x** across the whole run. With a more
diverse term set the reduction is lower but still large — the win scales with how
much the buffer can coalesce and how many searches accumulate per flush interval.

## 4. Consistent-hash distribution

Routing of distinct keys across the 3 Redis nodes (150 virtual nodes each):

```
all aa..zz (676 keys):  redis-0=35.4%  redis-1=33.0%  redis-2=31.7%
3000 random keys:       redis-0=35.6%  redis-1=33.2%  redis-2=31.2%
```

Within ±3pp of the ideal 33.3% — even balance. (An earlier version using plain
FNV-1a clustered the virtual nodes badly: 47/37/16. Adding a MurmurHash3 `fmix32`
finalizer to the hash fixed the avalanche and the balance — see
[interview-notes.md](interview-notes.md).)

`GET /cache/debug?prefix=…` shows the owning node + ring position per key, and the
server logs every routing decision, e.g.:

```
[cache/debug] prefix="epu" key="sug:recency:epu" -> redis-2 (vnode=2980738427) MISS
```

## How to reproduce

```bash
bash scripts/start_services.sh
bash scripts/download_dataset.sh
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release && cmake --build backend/build -j
./backend/build/load_dataset data/queries.csv
./backend/build/typeahead &        # :8080
python3 scripts/bench.py
```
