#!/usr/bin/env python3
"""Performance harness for the Search Typeahead backend.

Measures the three numbers the assignment asks for:
  1. Suggestion latency (avg / p50 / p95 / p99).
  2. Cache hit rate (cold pass vs warm pass).
  3. Write reduction from batching (searches submitted vs SQL writes issued).

Pure stdlib, no external deps:  python3 scripts/bench.py [--base URL]
"""
import argparse
import json
import random
import string
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

def get(base, path):
    with urllib.request.urlopen(base + path) as r:
        return json.loads(r.read())

def post(base, path, body):
    req = urllib.request.Request(
        base + path, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read())

def pct(xs, p):
    return sorted(xs)[min(len(xs) - 1, int(len(xs) * p))]

def time_suggest(base, prefix):
    t = time.perf_counter()
    urllib.request.urlopen(f"{base}/suggest?q={prefix}").read()
    return (time.perf_counter() - t) * 1000.0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="http://localhost:8080")
    ap.add_argument("--requests", type=int, default=5000)
    ap.add_argument("--concurrency", type=int, default=16)
    ap.add_argument("--searches", type=int, default=5000)
    args = ap.parse_args()
    base = args.base

    # A realistic-ish prefix set: lots of 2-3 char prefixes (where typeahead
    # traffic concentrates) drawn from a small alphabet, so keys repeat and the
    # cache gets meaningful reuse.
    twos = [a + b for a in string.ascii_lowercase for b in "aeiourstln"]
    threes = [a + b + c for a in "stcpbmade" for b in "aeiou" for c in "nrtl"]
    prefixes = twos + threes
    random.seed(7)

    print(f"== Search Typeahead benchmark ({base}) ==\n")

    before = get(base, "/stats")

    # --- 1 & 2: latency + cache hit rate -----------------------------------
    work = [random.choice(prefixes) for _ in range(args.requests)]
    lat = []
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        for ms in ex.map(lambda p: time_suggest(base, p), work):
            lat.append(ms)
    wall = time.perf_counter() - t0

    print(f"[latency]  {len(lat)} /suggest requests @ concurrency {args.concurrency}")
    print(f"           avg={sum(lat)/len(lat):.3f}ms  p50={pct(lat,.50):.3f}ms  "
          f"p95={pct(lat,.95):.3f}ms  p99={pct(lat,.99):.3f}ms  max={max(lat):.3f}ms")
    print(f"           throughput ~= {len(lat)/wall:,.0f} req/s\n")

    after = get(base, "/stats")
    dh = after["cache_hits"] - before["cache_hits"]
    dm = after["cache_misses"] - before["cache_misses"]
    rate = dh / (dh + dm) if (dh + dm) else 0
    print(f"[cache]    hits={dh}  misses={dm}  hit_rate={rate:.1%}")
    print(f"           (misses computed from the Trie; hits served from Redis)\n")

    # --- 3: write reduction via batching -----------------------------------
    # Submit many searches over a small set of terms -> they coalesce + batch.
    terms = ["python", "java", "new york city", "the matrix", "iphone",
             "climate change", "world cup", "taylor swift"]
    s_before = get(base, "/stats")
    def submit(_):
        post(base, "/search", {"query": random.choice(terms)})
    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        list(ex.map(submit, range(args.searches)))
    time.sleep(2.0)  # let the final batch flush
    s_after = get(base, "/stats")

    recv = s_after["searches_received"] - s_before["searches_received"]
    stmts = s_after["db_write_statements"] - s_before["db_write_statements"]
    rows = s_after["rows_written"] - s_before["rows_written"]
    flushes = s_after["flush_ops"] - s_before["flush_ops"]
    print(f"[batching] searches submitted = {recv}")
    print(f"           flushes = {flushes}, SQL write statements = {stmts}, "
          f"rows upserted = {rows}")
    if stmts:
        print(f"           write reduction = {recv/stmts:.1f}x fewer SQL writes "
              f"than 1-write-per-search")
    print()
    print("[server /stats]")
    print(json.dumps(get(base, "/stats"), indent=2))

if __name__ == "__main__":
    main()
