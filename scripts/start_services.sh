#!/usr/bin/env bash
# Starts the primary store (PostgreSQL) and the distributed cache (3 Redis
# nodes) locally — no Docker required. Data lives under ./.run so the project
# stays self-contained and nothing pollutes the system.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN="$ROOT/.run"
PGBIN="/opt/homebrew/opt/postgresql@16/bin"
PGDATA="$RUN/pgdata"
PGPORT="${PGPORT:-5432}"
mkdir -p "$RUN"

# --- PostgreSQL ------------------------------------------------------------
if [ ! -d "$PGDATA" ]; then
  echo "initializing postgres data dir..."
  "$PGBIN/initdb" -D "$PGDATA" -U "$USER" >/dev/null
fi
if ! "$PGBIN/pg_ctl" -D "$PGDATA" status >/dev/null 2>&1; then
  "$PGBIN/pg_ctl" -D "$PGDATA" -o "-p $PGPORT" -l "$RUN/pg.log" -w start
fi
if ! "$PGBIN/psql" -p "$PGPORT" -d postgres -tAc \
      "SELECT 1 FROM pg_database WHERE datname='typeahead'" | grep -q 1; then
  "$PGBIN/createdb" -p "$PGPORT" typeahead
  echo "created database 'typeahead'"
fi

# --- Redis x3 (logical cache nodes) ---------------------------------------
for i in 0 1 2; do
  port=$((6379 + i))
  dir="$RUN/redis-$i"
  mkdir -p "$dir"
  if ! redis-cli -p "$port" ping >/dev/null 2>&1; then
    redis-server --port "$port" --daemonize yes --save "" --appendonly no \
      --dir "$dir" --pidfile "$dir/redis.pid" --logfile "$dir/redis.log"
    echo "started redis node on :$port"
  fi
done

echo "services up -> postgres :$PGPORT | redis :6379 :6380 :6381"
