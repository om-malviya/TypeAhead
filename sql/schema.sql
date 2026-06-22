-- Reference schema for the Search Typeahead primary store.
-- The server creates this automatically on startup (PostgresStore::ensureSchema);
-- this file is here for documentation and manual setup.

CREATE TABLE IF NOT EXISTS queries (
  query         TEXT PRIMARY KEY,
  total_count   BIGINT           NOT NULL DEFAULT 0,  -- all-time popularity (durable)
  recent_count  DOUBLE PRECISION NOT NULL DEFAULT 0,  -- time-decayed recent activity
  last_searched TIMESTAMPTZ
);

-- text_pattern_ops lets `WHERE query LIKE 'prefix%'` use the index
-- (the loader path and the Trie's SQL fallback alternative).
CREATE INDEX IF NOT EXISTS idx_queries_prefix
  ON queries (query text_pattern_ops);
