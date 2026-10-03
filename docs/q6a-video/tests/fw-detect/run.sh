#!/usr/bin/env bash
# Replays iris_detect_gen2_from_fwdata() (before / after commit 65daae492) with the blob placed against a guard page.
set -u; cd "$(dirname "$0")" || exit 2
for v in before after; do
  cc -O0 -g -w -DFN="\"fn_$v.inc\"" -o /tmp/fwdetect_$v t.c || exit 2
  echo "=== $v ==="; stdbuf -o0 /tmp/fwdetect_$v; echo "exit=$?"; echo
done
