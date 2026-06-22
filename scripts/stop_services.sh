#!/usr/bin/env bash
# Stops the local Postgres + Redis nodes started by start_services.sh.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN="$ROOT/.run"
PGBIN="/opt/homebrew/opt/postgresql@16/bin"

for i in 0 1 2; do
  redis-cli -p $((6379 + i)) shutdown nosave 2>/dev/null && echo "stopped redis :$((6379 + i))" || true
done

if "$PGBIN/pg_ctl" -D "$RUN/pgdata" status >/dev/null 2>&1; then
  "$PGBIN/pg_ctl" -D "$RUN/pgdata" -w stop >/dev/null && echo "stopped postgres"
fi
