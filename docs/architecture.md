# Architecture

## Components

| Component | File | Responsibility |
|---|---|---|
| HTTP server | [backend/src/main.cpp](../backend/src/main.cpp) | Routes, JSON, CORS, static SPA mount |
| Engine | [backend/src/Engine.cpp](../backend/src/Engine.cpp) | Orchestrates the suggestion + write flows |
| Trie | [backend/src/core/Trie.cpp](../backend/src/core/Trie.cpp) | In-memory prefix index |
| Consistent-hash ring | [backend/src/core/ConsistentHashRing.cpp](../backend/src/core/ConsistentHashRing.cpp) | Maps a key → cache node |
| Redis cache | [backend/src/cache/RedisCache.cpp](../backend/src/cache/RedisCache.cpp) | Distributed cache over N Redis nodes |
| Postgres store | [backend/src/store/PostgresStore.cpp](../backend/src/store/PostgresStore.cpp) | Durable primary store + bulk upsert |
| Batch writer | [backend/src/batch/BatchWriter.cpp](../backend/src/batch/BatchWriter.cpp) | Buffers + flushes search counts |
| Scorer | [backend/src/ranking/Scorer.h](../backend/src/ranking/Scorer.h) | Count vs recency-aware scoring |
| Metrics | [backend/src/metrics/Metrics.h](../backend/src/metrics/Metrics.h) | Hit rate, write counts, latency reservoir |

## Three-tier suggestion path

1. **Redis cache** (distributed, 3 nodes). The cache key is
   `sug:<mode>:<prefix>`. The consistent-hash ring picks which node owns the key.
   A hit returns the serialized top-10 immediately.
2. **In-memory Trie** (on a miss). `collect(prefix)` returns every Entry in the
   prefix subtree; the Scorer ranks them and the top-10 is taken. The result is
   written back to the owning Redis node with a TTL.
3. **PostgreSQL** (the durable source of truth). The Trie + in-memory map are
   rebuilt from Postgres at startup; the database itself is only *read* at startup
   and *written* by the batch writer.

So the cache sits in front of the compute step, and the compute step (Trie) is
derived from the primary store — exactly the "cache → primary" fallback the
assignment asks for, with the Trie as the fast in-memory representation of the
primary data.

## Data model (PostgreSQL)

```sql
CREATE TABLE queries (
  query         TEXT PRIMARY KEY,
  total_count   BIGINT           NOT NULL DEFAULT 0,  -- all-time popularity
  recent_count  DOUBLE PRECISION NOT NULL DEFAULT 0,  -- time-decayed recent activity
  last_searched TIMESTAMPTZ
);
CREATE INDEX idx_queries_prefix ON queries (query text_pattern_ops);
```

`total_count` is authoritative and durable. `recent_count` + `last_searched`
capture recency. The `text_pattern_ops` index makes a `LIKE 'prefix%'` scan
index-usable — used by the loader and available as a fallback compute path.

## Consistent hashing

- Each physical Redis node is placed at `TA_VNODES` (default 150) points on a
  32-bit ring (`std::map<uint32_t,int>`), keyed by `hash(nodeName#i)`.
- The hash is **FNV-1a followed by a MurmurHash3 `fmix32` finalizer** — the
  finalizer is what makes the virtual nodes (and keys) land evenly; plain FNV-1a
  clustered them (measured 47/37/16% vs ~33% each after the fix).
- A key is owned by the **first virtual node clockwise** from `hash(key)`
  (`lower_bound`, wrapping at the end).
- Virtual nodes keep the load balanced and ensure that adding/removing a node
  only remaps that node's share of keys, not everything.
- `GET /cache/debug?prefix=…` reports the chosen node + ring position; the server
  also logs every routing decision.

## Recency-aware ranking (trending)

- **Basic mode** (`count`): `score = total_count`.
- **Enhanced mode** (`recency`): `score = w_total·log(1+total_count) + w_recent·R`,
  where `R` is the recent activity **decayed to now**:
  `R = recent_count · e^(−λ·Δt)`, `λ = ln2 / half_life`.
- On each flushed search: `recent_count = decay(recent_count) + Δ`, so a query's
  recency weight **rises on activity and fades over time**. This is what stops a
  briefly-popular query from being permanently over-ranked — once activity stops,
  the exponential decay pulls it back down on its own.
- `/trending` ranks by the decayed recent activity (falling back to all-time
  popularity before any searches happen).

## Batch writes

- `POST /search` never writes Postgres synchronously; it pushes the (normalized)
  query into an in-memory buffer (`query → count`).
- A background thread flushes when the buffer reaches `TA_BATCH_SIZE` **or** every
  `TA_FLUSH_MS`. Repeated queries are already coalesced in the buffer, and the
  flush is a **single bulk `INSERT … ON CONFLICT DO UPDATE`** per ≤1000-row chunk.
- After persisting, the affected cache keys (prefixes of each written query) are
  invalidated so rankings refresh; the TTL bounds staleness either way.

## Cache invalidation

Two mechanisms work together:
- **TTL** (`TA_CACHE_TTL`, default 60s) — nothing is ever permanently stale.
- **Targeted invalidation** — on each flush the server deletes
  `sug:<mode>:<prefix>` for prefixes (up to length 12) of every updated query.
- A **ranking-mode switch** flushes the cache entirely (old entries were ranked
  under the old mode).

## Concurrency model

- The Trie + entry map are guarded by a `std::shared_mutex`: `/suggest` and
  `/trending` take a shared (read) lock; the batch flush takes a unique (write)
  lock.
- Each Redis node has its own connection + mutex (hiredis contexts are
  single-threaded).
- The Postgres connection is touched only at startup and by the single batch
  thread, matching libpqxx's non-thread-safe connection model.
