# Search Typeahead System

A search-typeahead service (like the suggestion box in search engines / e-commerce
sites) focused on the **backend data-system design**: how query-count data is
stored, how suggestions are served with low latency, how the cache is distributed
with **consistent hashing**, how rankings incorporate **recency**, and how write
pressure is reduced with **batch writes**.

- **Backend:** C++20 — hand-written Trie, consistent-hash ring, and batch writer
  (HTTP via [cpp-httplib], Postgres via [libpqxx], Redis via [hiredis], JSON via
  [nlohmann/json]).
- **Frontend:** React + Vite.
- **Primary store:** PostgreSQL. **Distributed cache:** 3 Redis nodes.
- **Dataset:** real, public Wikipedia Pageviews (no auth) → 300k+ `query,count` rows.

![typeahead](docs/screenshots/ui-light.png)

The UI has debounced prefix suggestions (matched prefix shown in bold), a search
response toast, recent-search history, a recency-aware trending section, keyboard
navigation, and a light/dark theme toggle.

## Architecture at a glance

```
            React UI (Vite)
                 │  /suggest  /search  /trending  /cache/debug  /stats
                 ▼
        ┌───────────────────────────────────────────────┐
        │            C++ backend (cpp-httplib)            │
        │                                                 │
        │   /suggest ─► [1] Redis cache  ──hit──► result  │
        │                   (consistent-hash ring picks   │
        │                    one of 3 nodes)              │
        │                     │ miss                      │
        │                [2] in-memory Trie  ─► top-10    │
        │                     │  (ranked by Scorer)       │
        │                     ▼  populate cache (TTL)      │
        │   /search ─► BatchWriter buffer ─┐              │
        │                                  │ flush (size  │
        │                                  ▼  or interval)│
        │   [3] PostgreSQL  ◄── 1 bulk UPSERT per flush   │
        │        (durable primary; Trie built from it)    │
        └───────────────────────────────────────────────┘
```

Full write-up: [docs/architecture.md](docs/architecture.md) ·
measured numbers: [docs/performance.md](docs/performance.md).

## Prerequisites (macOS / Homebrew)

```bash
brew install postgresql@16 redis libpqxx hiredis nlohmann-json cmake
# Node 18+ for the frontend
```

No Docker required — Postgres and the 3 Redis cache nodes run natively from a
script under `./.run`. (A `docker-compose.yml` is also possible, but native is the
supported path here.)

## Run it

```bash
# 1. Start the primary store + 3 Redis cache nodes (ports 6379/6380/6381).
bash scripts/start_services.sh

# 2. Download a real dataset (one hour of Wikipedia Pageviews -> data/queries.csv).
#    300k+ rows, query,count. Override the hour with YEAR/MONTH/DAY/HOUR env vars.
bash scripts/download_dataset.sh

# 3. Build the backend.
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j

# 4. Load the dataset into Postgres.
./backend/build/load_dataset data/queries.csv

# 5. Start the API server (listens on :8080).
./backend/build/typeahead

# 6. In another terminal, start the UI.
cd frontend && npm install && npm run dev
#    open the printed URL (http://localhost:5173, or 5174/5175 if taken)
```

To stop the services later: `bash scripts/stop_services.sh`.

## API

| Method | Endpoint | Purpose | Example |
|---|---|---|---|
| GET | `/suggest?q=<prefix>` | Up to 10 prefix matches, sorted by score desc | `curl 'localhost:8080/suggest?q=new'` |
| POST | `/search` | Record a search; returns the dummy response | `curl -XPOST localhost:8080/search -d '{"query":"iphone"}'` |
| GET | `/trending?n=10` | Recency-aware trending queries | `curl 'localhost:8080/trending'` |
| GET | `/cache/debug?prefix=<p>` | Which cache node owns the key + hit/miss | `curl 'localhost:8080/cache/debug?prefix=new'` |
| GET | `/stats` | Cache hit rate, DB write counts, p95 latency, write reduction | `curl 'localhost:8080/stats'` |
| GET | `/admin/ranking?mode=count\|recency` | Toggle basic vs recency ranking (for the demo) | `curl 'localhost:8080/admin/ranking?mode=count'` |

`/suggest` handles empty / missing / mixed-case / no-match input gracefully
(returns `{"suggestions":[]}`). Suggestions always start with the typed prefix and
are sorted by count (basic) or by the recency-aware score (enhanced).

```jsonc
// GET /suggest?q=new
{"q":"new","suggestions":[
  {"query":"new relic","score":...,"total_count":264,"recent_count":0},
  {"query":"new york city","score":...,"total_count":211,"recent_count":0}
]}
```

## Configuration (environment variables)

| Var | Default | Meaning |
|---|---|---|
| `TA_PG_CONN` | `host=127.0.0.1 port=5432 dbname=typeahead` | Postgres connection |
| `TA_REDIS_NODES` | `127.0.0.1:6379,127.0.0.1:6380,127.0.0.1:6381` | Cache nodes |
| `TA_VNODES` | `150` | Virtual nodes per cache node on the ring |
| `TA_CACHE_TTL` | `60` | Suggestion cache TTL (seconds) |
| `TA_BATCH_SIZE` | `500` | Flush when the buffer reaches this many unique queries |
| `TA_FLUSH_MS` | `1000` | Flush at least this often |
| `TA_RANKING` | `recency` | `count` (basic) or `recency` (enhanced) |
| `TA_PORT` | `8080` | HTTP port |

## Benchmark

```bash
python3 scripts/bench.py            # latency p50/p95/p99, cache hit rate, write reduction
```

## Dataset

Real, public **Wikipedia Pageviews** dumps (`dumps.wikimedia.org/other/pageviews/`)
— no login or API key. English article titles become queries and hourly view
counts become popularity counts; `scripts/download_dataset.sh` filters, lowercases,
and writes `data/queries.csv` (header `query,count`). The loader and the whole
system are dataset-agnostic — any `query,count` CSV works.

## Repo layout

```
backend/    C++ server + core library + dataset loader (see src/)
frontend/   React + Vite UI
scripts/    start/stop services, dataset download, benchmark
docs/        architecture, performance report, screenshots
sql/         schema reference
```
