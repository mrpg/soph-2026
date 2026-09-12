#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

K=${1:?usage: compute.sh <digits>}

N=$(python3 ./nterms.py "$K")

echo "K=$K digits, N=$N terms" >&2

SUM=$(./soph "$N" "$K")
LEN=${#SUM}
echo "${SUM:0:$((LEN - K))}.${SUM:$((LEN - K))}"
