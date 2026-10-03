#!/usr/bin/env bash
# Replays iris_hfi_queue_write() from the kernel (before / after commit 3525d2dfd) in user space.
set -u; cd "$(dirname "$0")" || exit 2
for v in before after; do
  cc -O1 -w -DFNFILE="\"write_fn_$v.inc\"" -o /tmp/ring_$v harness.c || exit 2
  echo "=== $v ==="; /tmp/ring_$v; echo "exit=$?"; echo
done
