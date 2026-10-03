# User-space reproductions

These replay *copies of kernel functions* (taken verbatim from the tree before and after the fix) in user space, so a defect can be shown without hardware. They prove the logic of the function in isolation, not run-time behaviour on the board. Captured output: [`RESULTS.txt`](RESULTS.txt).

| Directory | Reproduces | Fixed by | Result |
|---|---|---|---|
| `ring-queue/` | `iris_hfi_queue_write()` accepts a packet that exactly fills the shared command queue → `write_idx == read_idx` (= "empty" for the firmware) | patch 12 (`3525d2dfd`) | before: 8th packet accepted and vanishes; after: `-ENOSPC` |
| `fw-detect/` | `iris_detect_gen2_from_fwdata()` copies 63 bytes after the version marker with no bound by the blob end (blob placed against a guard page) | patch 22 (`65daae492`) | before: faults on an unterminated marker at the end; after: correct on all six cases |
| `irq-model/` | A **locking model** (not kernel code) of the IRQ thread vs power-off: current tree, upstream `b9c2215bded` on this tree, and this series | patch 13 (`4dad56614`) | current: register access with clocks off; upstream one-liner: deadlock; this series: neither |

Run: `./ring-queue/run.sh`, `./fw-detect/run.sh`, `./irq-model/run.sh` (need only `cc`; `fw-detect` also uses `stdbuf`).
The `.inc` files are the extracted functions; to re-extract from a kernel tree:
`awk '/^static int iris_hfi_queue_write\(/{f=1} f{print} f&&/^}/{exit}' drivers/media/platform/qcom/iris/iris_hfi_queue.c`.
