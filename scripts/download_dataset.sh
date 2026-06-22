#!/usr/bin/env bash
# Downloads a real, public, no-auth dataset: one hour of Wikipedia Pageviews
# (https://dumps.wikimedia.org/other/pageviews/). English article titles
# become search queries and their view counts become the popularity counts.
# Output: data/queries.csv  (header: query,count)  — easily 100k+ rows.
#
# Override the hour via env: YEAR=2024 MONTH=06 DAY=01 HOUR=12 ./download_dataset.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="${1:-$ROOT/data/queries.csv}"
YEAR="${YEAR:-2024}"; MONTH="${MONTH:-06}"; DAY="${DAY:-01}"; HOUR="${HOUR:-12}"
URL="https://dumps.wikimedia.org/other/pageviews/$YEAR/$YEAR-$MONTH/pageviews-$YEAR$MONTH$DAY-${HOUR}0000.gz"
TMP="$ROOT/data/pageviews.gz"
mkdir -p "$ROOT/data"

echo "downloading $URL"
curl -fL "$URL" -o "$TMP"

# Raw line format: "<project> <title> <view_count> <bytes>".
# Titles use underscores (no spaces), so awk fields are stable. Keep English
# (project "en"), drop namespaced titles (contain ':'), require count > 1,
# turn underscores into spaces and lowercase the title.
echo "query,count" > "$OUT"
gunzip -c "$TMP" | awk '$1=="en" && ($3+0)>1 {
    t=$2;
    if (index(t,":")>0) next;
    gsub(/_/," ",t);
    print tolower(t) "," $3;
}' >> "$OUT"

rows=$(( $(wc -l < "$OUT") - 1 ))
echo "wrote $rows rows to $OUT"
echo "next: ./backend/build/load_dataset $OUT"
