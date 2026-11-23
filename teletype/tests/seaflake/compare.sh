#!/bin/bash
# Runs every patch through the JS engine and the C port; any difference fails.
# usage: tests/seaflake/compare.sh [frames]
cd "$(dirname "$0")"
# the Orca Seaflake repository: this folder's parent when it is the teletype/
# folder of orca_seaflake, else SEAFLAKE_REPO or ~/orcastuffnew/orca-seaflake
UP="$(cd "$(dirname "$0")/../../.." && pwd)"
if [ -d "$UP/terminal/src/core" ]; then DEF="$UP"; else DEF="$HOME/orcastuffnew/orca-seaflake"; fi
REPO=${SEAFLAKE_REPO:-$DEF}
FRAMES=${1:-300}
cc -std=gnu99 -O1 -w -I../../src -o run_c run_c.c ../../src/seaflake/sf_core.c ../../src/seaflake/sf_config.c || exit 1
fail=0
for p in patches/*.orca "$REPO"/patches/*.orca; do
  node run_js.js "$REPO" "$p" "$FRAMES" 80 40 > /tmp/sf_js.txt
  ./run_c "$p" "$FRAMES" 80 40 > /tmp/sf_c.txt
  if cmp -s /tmp/sf_js.txt /tmp/sf_c.txt; then
    echo "SAME  $(basename "$p")  ($FRAMES frames)"
  else
    fail=1
    echo "DIFF  $(basename "$p")"
    diff /tmp/sf_js.txt /tmp/sf_c.txt | head -30
    first=$(diff <(cat /tmp/sf_js.txt) <(cat /tmp/sf_c.txt) | head -1)
    echo "  first difference: $first"
  fi
done
exit $fail
