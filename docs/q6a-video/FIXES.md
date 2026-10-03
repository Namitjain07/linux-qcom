# Source-level findings and the patch set

The first bundle (`REPORT.md`) concluded that **no kernel change is needed to *try* encoding on the Q6A** (firmware + EL2 + Iris). This document is the second pass you asked for: the Iris and Venus sources were read end to end for the failure classes you listed — **loading, memory/DMA, synchronisation/teardown, encoder, resolution, scaling** — and the defects found are fixed in a patch series.

> **Status — read first.**
> * The series is **compile-tested, statically checked and partly reproduced in user space. It has not been run on hardware.** Every statement about run-time behaviour on the board is an inference until the tests in §7 are run.
> * Code: pull request [`Namitjain07/kernel#1`](https://github.com/Namitjain07/kernel/pull/1) (draft), branch `claude/q6a-iris-venus-fixes`, base = Radxa `linux-7.0.11` @ `a50eb5b71` — the commit this repo's `src` submodule is pinned to. The same 25 commits are exported in [`patches/`](patches/).
> * Evidence grades as in `EVIDENCE.md`: **P** primary (read/compiled/ran here), **Q** quoted from upstream commit text, **I** inference, **U** unknown.

## 1. What is in the series (25 commits)

| # | Commit (`git log` subject) | Class | Origin |
|---|---|---|---|
| 1 | `7b921bda9` Fix bitmask test in `iris_allow_cmd()` | sync / encoder drain | upstream `0ac05c4d9f1` |
| 2 | `92af64010` state-change debug log printed the stale value | diagnostics | upstream `460d3257a6d` |
| 3 | `00948fd99` missing `hfi_id` in Gen1 `GOP_SIZE` cap | **encoder (Gen1)** | upstream `5eebacbc9a3` |
| 4 | `f8b2ad6af` distinct `bus_info` for encoder and decoder | enumeration | upstream `94ef75095d5` |
| 5–6 | `eeff39668`, `aa2abd940` Venus parser payload sizes | Venus loading | upstream `bd595b745eb`, `a51cea23e40` |
| 7–8 | `c1b507db0`, `a4c0f373e` Venus encoder frame/blur step 16→1 (v6, v4) | Venus encoder / resolution | upstream `c53e0550288`, `35428ae3a6a` |
| 9 | `3ca01ac3c` runtime-PM reference leaks in the power-domain helpers | memory/power | adapted from `f87d7eda07f` |
| 10 | `ec7dc2614` clock unwind in `iris_vpu_power_on_hw()` | loading | new (same unwind as Qualcomm main) |
| 11 | `f795a7e11` don't drop a PM reference that was never taken | sync/power | new |
| 12 | `3525d2dfd` a completely full HFI command queue is full | **memory (shared queue)** | new |
| 13 | `4dad56614` keep the IRQ thread off the registers once power is off | **sync** | new — replaces `b9c2215bded` |
| 14 | `c06d43d34` resume failure in core deinit | sync/teardown | adapted from `75d79879ec3` |
| 15 | `943bae279` bound the wait for system-error recovery | sync/teardown | new |
| 16 | `d41df45f6` return queued capture buffers on early `stop_streaming` | sync/teardown | new |
| 17 | `4f59b78d9` `open()` error handling, session limit → `-EBUSY` | loading/teardown | new |
| 18 | `b24262e2c` validate the encoder frame size in `try_fmt` | **resolution / scaling** | new |
| 19 | `d09cb252b` make encoder crop selection consistent | **resolution / scaling** | new |
| 20 | `adad2b26f` round the encoder frame rate | **encoder** | new |
| 21 | `e1d1ce3fe` no `memcpy` of a format onto itself | encoder hygiene | new |
| 22 | `65daae492` bound the firmware version scan | loading | new |
| 23 | `57d040a4c` say why the firmware could not be loaded | **loading** | new |
| 24 | `441d38fa7` publish the nodes only once the device is usable (+ remove order) | **loading** | new |
| 25 | `e33a87d7e` warn when the SC7280 node lacks `dma-coherent` | **DMA** | new |

## 2. Problems, root causes and the fix for each

Line numbers are for the pre-patch tree (`a50eb5b71`). "Evidence" says what was actually run.

### 2.1 Loading the driver
| Problem | Root cause | Fix | Evidence |
|---|---|---|---|
| A firmware that does not fit the carveout fails with only `firmware download failed -22` | `iris_firmware.c:164` returns `-EINVAL` silently when `qcom_mdt_get_size()` > reserved region. Gen2 needs `0x700000`, Gen1 `0x500000` (EVIDENCE §D) | #23 prints image, needed and available bytes, and which HFI generation was detected | **P** compiled. Sizes: **P** (binary inspection, earlier turn) |
| `/dev/video*` appear before the device is usable → boot-time open fails | `iris_probe.c:281` registers both nodes, then sets drvdata (`:293`), DMA mask and enables runtime PM (`:306`). `open()` needs all of it (`pm_runtime_resume_and_get`) | #24 moves node registration last; also frees the `video-firmware` platform device/IOMMU domain when a later probe step fails (leaked before) | **P** compiled; race **I** |
| `iris_remove()` brings the core back up / UAF on pending recovery work | `:204` deinit runs before the nodes are unregistered, so an `open()` in between re-inits; `sys_error_handler` is never cancelled | #24 unregisters nodes, `cancel_delayed_work_sync()`, then deinit | **P** compiled; **I** |
| Clock leak after a failed power-on | `iris_vpu_common.c:298` jumps to a label that disables the BSE clock that never started and skips `HW_AHB` | #10 jumps to the right label (identical to Qualcomm main) | **P** compiled; reading |
| Power-domain reference leaks → later power-ups fail with a *positive* "error" | `iris_resources.c:81` `pm_runtime_get_sync()` keeps the count on failure and returns `1` if already active (callers test `if (ret)`); `:96` returns before `put_sync` when `iris_opp_set_rate()` fails | #9 | **Q** (upstream message) + **P** callers audited (all test non-zero) |
| `open()` succeeds but the session is not registered | `iris_vidc.c:54` silently skips `list_add_tail` at the session limit; first `REQBUFS` then fails with the unrelated `-EINVAL` | #17 returns `-EBUSY` from `open()`; also frees the format structs and handles missing `kzalloc` checks in `iris_vdec_inst_init()` | **P** compiled |
| Out-of-bounds read while detecting Gen2 firmware | `iris_firmware.c:41` `strscpy()` of 63 bytes after the marker with no bound by the blob end | #22 | **P** user-space replay: faults before, correct after (`tests/fw-detect`) |
| Gen2 firmware but DT carveout is the stock 5 MiB; or auth fails | n/a — *diagnosed, not fixed* | #23 makes it visible. An automatic Gen2→Gen1 fallback was **not** implemented (§5) | — |

### 2.2 Memory and DMA
| Problem | Root cause | Fix | Evidence |
|---|---|---|---|
| Corrupted frames on a DT without `dma-coherent` look like an encoder bug | Qualcomm's fix for SC7280 corruption is the DT property (kernel-topics #1640/#1641). Radxa 7.0.11 `kodiak.dtsi` has it (`:4989`, venus node); 6.18.2 and boards with their own copy of the node may not | #25 warns at probe | **P** DTS read; **Q** Qualcomm's explanation |
| **Shared command queue: a packet that exactly fills the free space is accepted** | `iris_hfi_queue.c:24` tests `empty_space < packet_size`; the protocol uses `read_idx == write_idx` for "empty", so the write index lands on the read index and the firmware sees an empty queue; the next write overwrites unread packets. Venus uses `<=` (`hfi_venus.c:198`) | #12 | **P** user-space replay of the function (`tests/ring-queue`): before = 8th packet accepted with `write_idx==read_idx`; after = `-ENOSPC` |
| Internal buffer / queue allocation attributes | `DMA_ATTR_WRITE_COMBINE`, `NO_KERNEL_MAPPING`; DMA mask `0xe0000000-1` (`iris_platform_vpu2.c:95`); the IOVA reservation `iris_iova` is applied by the IOMMU core from `memory-region` | no change needed | **P** read; no defect found |

### 2.3 Synchronisation and teardown ("sync" — I read "sink" as sync/teardown)
| Problem | Root cause | Fix | Evidence |
|---|---|---|---|
| **IRQ thread reads the interrupt registers after power-off** → access to an unclocked block (external abort / bus hang) | `iris_hfi_common.c:133` thread does `mutex_lock(&core->lock)` + `iris_vpu_clear_interrupt()`; runtime suspend (`iris_hfi_pm_suspend`) powers off without that lock; nothing orders the two. Upstream's fix `b9c2215bded` swaps `disable_irq_nosync()` for `disable_irq()` at the *end* of `iris_vpu_power_off()` | #13: `hw_lock` spinlock + `hw_powered` flag; the thread touches registers only while the flag is set; power-off clears it **before** touching any clock | **P** locking model (`tests/irq-model`): current tree → register access with clocks off; upstream one-liner on this tree → **deadlock** (`iris_core_deinit` holds `core->lock` when it calls power-off, and the thread needs it) and still access after clocks off; this series → neither. The model is not kernel code. **Run-time behaviour U** |
| PM usage count underflow on resume failure | `iris_hfi_queue.c:135` `goto exit` after `pm_runtime_resume_and_get()` failed → `exit:` (`:149`) puts again | #11 | **P** compiled |
| Core deinit after a failed resume powers off an unpowered block | `iris_core.c:17` ignores the `pm_runtime_resume_and_get()` result; `iris_fw_unload()` does a `writel()` reset in EL2 mode | #14 (upstream `75d79879ec3` + guard the no-TZ reset with `hw_powered`) | **P** compiled |
| `close()`/`STREAMOFF` can hang forever after a firmware error | `iris_core.c:69` `wait_for_completion()` with no timeout (called from `iris_close` and `iris_vb2_stop_streaming` before any lock) | #15 five-second bound + `dev_err` | **P** compiled; **I** |
| vb2 warns / mem2mem list left stale on early `stop_streaming` | `iris_vb2.c:235` returns for CAPTURE in `IRIS_INST_INIT` without returning buffers | #16 | **P** compiled |
| Encoder drain/stop accepted in the wrong sub-state | `iris_state.c:275` `sub_state != IRIS_INST_SUB_DRAIN` on a bitmask | #1 | **Q** |

### 2.4 Encoder
| Problem | Root cause | Fix | Evidence |
|---|---|---|---|
| Gen1 GOP size never reaches the firmware | `GOP_SIZE` cap had no `hfi_id` | #3 | **Q** upstream |
| Bitrate/clock estimates 3–4 % off for 29.97 / 23.976 / 59.94 fps | `iris_venc.c:407` integer division; the value is sent as a Q16 whole number (`iris_hfi_gen*_command.c`) | #20 `DIV_ROUND_CLOSEST` | **P** compiled; arithmetic |
| Overlapping `memcpy()` | `iris_venc.c:243,303` copy `inst->fmt_dst` onto itself | #21 | **P** compiled |
| Time-delta rate control (Gen2) | already fixed in Radxa (`921f79c41`) | — | **P** |

### 2.5 Resolution and scaling (encoder)
| Problem | Root cause | Fix | Evidence |
|---|---|---|---|
| Any size accepted by `TRY_FMT`/`S_FMT`; failure only when streaming starts | `iris_venc.c:169` `iris_venc_try_fmt()` fixes only the pixel format; the checks later (`iris_vb2.c`) look at the raw size, never the encoded one | #18 clamps both queues to the platform limits (128…8192 here) | **P** compiled; behaviour check `tools/v4l2-enc-neg-test.c` (needs a node) |
| Coded size could be larger than the raw frame (an *upscale*) | same; the firmware has `HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED`, which the Gen1 handler only logs at debug level | #18 clamps CAPTURE ≤ raw | **P** read |
| `G_SELECTION` says one thing, `S_SELECTION` accepts another | bounds/default report the 32-aligned buffer (1088) (`iris_vidc.c:504`), `S_FMT` set crop to that (`iris_venc.c:295`), `S_SELECTION` rejects > visible (1080) (`:361`) | #19 | **P** compiled; `tools/v4l2-enc-neg-test.c` |
| `S_SELECTION` fails instead of adjusting; allowed while streaming | `:361` returns `-EINVAL`; no streaming check although the crop is programmed at stream-on | #19 | same |

**What is unchanged and why it matters for scaling:** the series makes the *negotiation* correct. It does **not** prove that the Gen1/Gen2 firmware scales while encoding. That remains open (EVIDENCE B35); the plan still starts with CPU scaling → VPU encode, then the streamed test (`PLAN.md` Phase 5). Note for Gen1 firmware: `crop` is not sent to the firmware at all (only `HFI_PROPERTY_PARAM_FRAME_SIZE` input = raw, output = scale), so on Gen1 a crop request is applied as a *resize of the whole frame* — the same behaviour Venus has. Gen2 sends crop offsets (`iris_hfi_gen2_command.c:227-279`).

## 3. Upstream commits deliberately not taken
See `HISTORY.md` §4 for the full disposition table. In short: `b9c2215bded` replaced (§2.3); `727a87c71b4` is dead code here; `8afb9283017` needs a Kconfig change absent in 7.0.11; the Venus NV12 padding drop and VP8 revert change behaviour for no benefit to the goal.

## 4. Verification actually performed
| Check | Result |
|---|---|
| Build, clang 18.1.3, `LLVM=1 ARCH=arm64 W=1`, Iris `=m`, **every commit** (rebase `--exec`) | no warnings, no errors |
| Build at the tip with `CONFIG_VIDEO_QCOM_IRIS=n` (Venus incl. `sc7280_res`) | clean |
| `scripts/checkpatch.pl --strict` per commit | no code findings; remaining: unknown upstream hashes, `Co-Authored-By` trailer form, missing `Signed-off-by` |
| clang static analyzer (`--analyze`) on the 11 touched Iris `.c` files | 0 findings; the same invocation flags a planted null dereference |
| `git format-patch` → `git am` on a clean base | identical tree hash |
| `tests/ring-queue`, `tests/fw-detect`, `tests/irq-model` | see `tests/RESULTS.txt` |
| Hardware | **not run** |

## 5. Considered and **not** changed (so you can overrule me)
| Item | Why left alone |
|---|---|
| Venus Radxa hacks: `decide_core` "HW is overloaded(?) Will run at max performance" instead of failing (`venus/pm_helpers.c`), `vdec_inst_init()` default `hfi_codec = HEVC` with `fmt_out = H264`, 10-bit format gating commented out, default `fps = VENUS_MAX_FPS` | Venus is not on the SC7280 path while Iris is enabled; reverting them changes behaviour I cannot test. Revert only if you build with `IRIS=n` and hit the symptom |
| Encoder clock and bandwidth vote | `iris_calc_bw()` uses the **decode** table; `iris_scale_clocks()` feeds the *raw* frame size (`data_size` of the OUTPUT buffer) into the VSP term of the formula; for 1080p-class frames that is about 1 GHz, above the highest OPP, so the encoder most likely runs at maximum clock (**I**: the OPP lookup falls back to the highest entry). Functional, costs power |
| Buffer timestamps are nanoseconds in Iris, microseconds in Venus | A unit mismatch could affect firmware rate control, but Qualcomm chose to disable time-delta rate control instead of changing units. **Test, don't guess:** measure output bitrate vs target (`GUIDE.md` G§13) |
| Automatic Gen2→Gen1 fallback on size/auth failure | would hide a mis-configured DT/firmware; #23 makes the cause visible instead |
| SC7280-specific frame-size caps (4096×2176) | 8K sizes are already rejected at stream-on by the core MBPF check; I have no authoritative encode limit for this SoC |
| `iris_venc_streamon_output()` returns without the `error:` cleanup on one path | cosmetic; the caller marks the instance errored |
| `iris_remove()` with open file descriptors | `core` is `devm`-allocated and freed at unbind while an fd may still exist — needs reference counting, larger than this series |

## 6. Risks of the series itself
| Commit | Could go wrong | Symptom / check |
|---|---|---|
| 13 | missed interrupts if `hw_powered` is false when it should be true; IRQ line left disabled | `received watchdog interrupt`, timeouts after idle, hang on first open after suspend |
| 14 | skipping the no-TZ reset when unpowered | stale firmware state after a failed resume (needs reboot) |
| 15 | recovery slower than 5 s → `close()` proceeds early | `timed out waiting for system error recovery` |
| 17 | `-EBUSY` where the old code "worked" | only with ≥ 16 open sessions |
| 18/19 | clients that relied on out-of-range sizes now get clamped values | `tools/v4l2-enc-neg-test` |
| 24 | probe order | both nodes present; no `iris_probe` error |
| 4 (upstream) | scripts keyed on the old `bus_info` string `platform:aa00000.video-codec` | nothing in this bundle depends on it |

## 7. How to test on the board (agent runbook → `GUIDE.md` G§13)
Build only the Iris/Venus modules out of tree where possible; installing a kernel or modules is **T3 (human)**. After the patched modules are in place:
1. `dmesg | grep -i iris` after first open: expect `firmware …: Gen2 HFI, … of … bytes of reserved memory` and **no** `dma-coherent` warning.
2. `tools/v4l2-enc-neg-test /dev/videoN` (encoder node) → all PASS.
3. `tools/encode-validate.sh` matrix (G§6) — corruption check; compare with the unpatched numbers.
4. 20× open/encode/close (G§9) — look for `timed out`, `watchdog`, SMMU faults.
5. Idle for > 3 s between two encodes (runtime suspend), then encode again — exercises #11, #13, #14.
6. Bitrate accuracy: encode 10 s at `-b:v 2M`; report the actual bitrate (the timestamp-unit question).
7. Record everything in the G§12 results file.

## 8. Rollback
Revert the single commit (they build independently, except that #14 uses the flag added in #13 — revert #14 first), or boot the unpatched kernel; no on-disk state is touched by the series.
