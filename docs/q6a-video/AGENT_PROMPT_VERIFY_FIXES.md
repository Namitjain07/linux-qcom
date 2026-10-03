# Agent prompt — verify and implement the Iris/Venus fixes on the Dragon Q6A

This file is **self-contained**. Give it to an AI agent that runs on your system and can reach the Radxa Dragon Q6A (SSH, ideally also a serial console). It tells the agent what was found, what was built, what is and is not proven, and exactly how to verify the fixes, install them safely, test them on the board and report back.

It is the *second* brief. The first one, [`AGENT_BRIEF.md`](AGENT_BRIEF.md), covers bringing hardware encoding up on the stock kernel (firmware, EL2, encode matrix). This one starts after that, or in parallel, and is about the **driver patch series**. Where the two overlap, the rules in `PLAN.md` §1 (authority tiers) still apply.

---

## 0. The prompt (paste this part if you only paste one thing)

```text
You are helping me get reliable hardware video ENCODING (and a verified answer on SCALING) working on a
Radxa Dragon Q6A (Qualcomm QCS6490 / SC7280). You can reach the board over SSH.

A previous research session read the Iris and Venus V4L2 drivers end to end and produced a 25-commit
fix series. It has been compiled and statically checked but NEVER RUN ON HARDWARE. Your job: independently
verify it, install it safely, test it on the board, and tell me per patch whether to KEEP, DROP or FIX it.
Treat every claim in the documents as a hypothesis until you have checked it yourself; if your measurement
contradicts the documents, believe your measurement and tell me.

1. Get the material:
     git clone --depth 1 --branch claude/dazzling-fermat-u820ir https://github.com/namitjain07/linux-qcom
     cd linux-qcom/docs/q6a-video
   Read, in this order: AGENT_PROMPT_VERIFY_FIXES.md (this file, in full) -> FIXES.md -> PLAN.md (section 1,
   authority tiers, and Phase 6) -> GUIDE.md (G§0, G§6, G§9, G§12, G§13) -> REPORT.md -> HISTORY.md.
2. Follow the workflow in section 5 of AGENT_PROMPT_VERIFY_FIXES.md: A verify offline -> B baseline on the board
   -> C build -> D install (needs my approval) -> E test -> F report.
3. Hard rules: never touch UEFI/bootloader/partitions; back up before replacing anything; keep a way back at
   every step; quote errors verbatim; stop and ask at every gate in section 6; at most 3 hardware-encode
   attempts per configuration; two unexplained resets in a row = stop and report.
4. Return the report in section 7, with all raw logs attached.
```

---

## 1. Goal and success criteria

**Goal.** Reliable H.264 and HEVC hardware encoding on the Q6A (Radxa Linux 7.0.11, Iris driver), a verified answer to "can the VPU scale while encoding?", and a decision on each patch of the fix series.

**You are done when:**

| # | Criterion | Evidence |
|---|---|---|
| V1 | The patch series is verified offline: correct base, 25 commits, builds, checkpatch/analysis clean, tests reproduce | §5 Phase A output |
| V2 | A **baseline** of the *unpatched* driver exists on this board (same tests as below) | Phase B logs |
| V3 | The patched Iris module is installed with a documented way back, and the patched driver is confirmed running | `dmesg` line "Gen2 HFI, … reserved memory", `modinfo` |
| V4 | `tools/v4l2-enc-neg-test` passes on the encoder node | its output |
| V5 | Encode matrix (`GUIDE.md` G§6) passes the corruption check at least as well as the baseline | `encode-validate.sh` results |
| V6 | Open/encode/close ×20 and 5 idle-then-encode cycles with **no** `timed out`, `watchdog`, `arm-smmu`, `system error`, `Unhandled`, `queue full` lines | G§9, G§13.3 |
| V7 | Bitrate accuracy measured (target 2 Mbit/s) and reported, whatever the number | G§13.4 |
| V8 | Scaling question answered with evidence (probe verdict + streamed test, or a working CPU-scale → VPU-encode pipeline) | G§10 |
| V9 | A per-patch verdict table (KEEP / DROP / FIX) with evidence, and the results file | §7 |

---

## 2. Context — what was done so far

### 2.1 The situation
* Board: **Radxa Dragon Q6A**, SoC **QCS6490** (= SC7280 "kodiak"), VPU 2.0 ("Iris 2.1") running Xtensa firmware over HFI shared queues.
* The user reported that the old **Venus** driver (Linux 6.18) has problems on this SoC and the new **Iris** driver "does not work". They want hardware **encoding and scaling**, on any kernel version that works.
* The Debian packaging repo `Namitjain07/linux-qcom` builds Radxa **linux-7.0.11** (its `src` submodule is pinned to `radxa/kernel` branch `linux-7.0.11` @ `a50eb5b71984bd82b49cd5d31b1e2eb5505c6512`). `defconfig` has `CONFIG_VIDEO_QCOM_IRIS=m` and `CONFIG_VIDEO_QCOM_VENUS=m`. With Iris enabled, Venus' SC7280 entries are compiled out, so **Iris is the only driver bound to `qcom,sc7280-venus`** on this kernel.

### 2.2 Research findings (round 1 — full detail in `REPORT.md`, ledger in `EVIDENCE.md`)
1. Both firmware generations contain H.264 and HEVC encoders. The correct Gen2 image is `qcom/vpu/vpu20_p1_gen2_s6.mbn` (sha256 `f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f`, MBN v6, linux-firmware commit `30a139cb656157cba7a27fd7727b5ffbd302ff60`). An older Gen2 file with an MBN **v7** header (`3c21c8ca…`) is rejected by this SoC. Current Gen1 is `qcom/vpu/vpu20_p1.mbn` (sha256 `607545ead3f23134680a1330cd569f2a6353f8b83dbabb924c68ac573f9d06c1`, linux-firmware `aeede7af…`). A Gen2 image needs `0x700000` bytes of reserved memory, Gen1 `0x500000`.
2. **Boot mode matters.** In default **TrustZone (EL1)** mode a hardware encode is *reported* (secondhand, forum) to reset the whole board; the documented mechanism is a secure stream ID (`0x2184`, "secure non-pixel") crash class. In **EL2** (UEFI "Hypervisor Override" on) Radxa's overlay maps the SIDs and Radxa's merged PRs #593/#607 say encode/scaling were fixed for QCS6490. Recommended route: Radxa 7.0.11 + Iris + Gen2 firmware + EL2.
3. `dma-coherent` on the venus node is the correct Qualcomm fix for corrupted frames (kernel-topics #1640/#1641). Radxa 7.0.11 has it (`kodiak.dtsi`); Radxa 6.18.2 does not.
4. **Scaling:** the hardware scaler (VPSS) is proven only on the *decode* side; neither driver exposes decoder downscale. Iris' encoder carries a "scaling case" (coded size ≠ raw size → `enc_scale_*`), but whether the firmware scales while encoding is **unproven**.
5. Nothing had been run on the board; the TZ-mode reset report is secondhand.

### 2.3 Round 2 — the source review (what you are verifying)
The Iris and Venus sources were read end to end (Radxa 7.0.11 vs Qualcomm `main` at 7.3-rc5, plus both repos' history) for: **loading, memory/DMA, synchronisation/teardown ("sink issues" was read as sync/teardown), encoder, resolution, scaling**. Real defects were found and fixed in a **25-commit series on top of `a50eb5b71`**:

* Code: **`Namitjain07/kernel` pull request #1** (draft), branch `claude/q6a-iris-venus-fixes`, tip `e33a87d7e74d46cd0b4ccca4554cf485b60bd2e3`, **tree hash `b53710f2156eb02473d4f7fdb45ae636f7de2482`**.
* The same commits exported in [`patches/`](patches/) (`git format-patch`); applying them to a clean base reproduces that exact tree hash.
* Explanation, root causes with file:line, evidence, risks and what was deliberately **not** changed: [`FIXES.md`](FIXES.md). History of the three repos: [`HISTORY.md`](HISTORY.md).

| # | Commit | What it fixes | Symptom it would remove |
|---|---|---|---|
| 1 | `7b921bda9` | `iris_allow_cmd()` bitmask test (upstream) | hang/odd state at end of stream (drain/stop) |
| 2 | `92af64010` | state-change debug log (upstream) | useless log only |
| 3 | `00948fd99` | Gen1 `GOP_SIZE` had no `hfi_id` (upstream) | Gen1 encoder ignores GOP size |
| 4 | `f8b2ad6af` | distinct `bus_info` for encoder/decoder (upstream) | **changes** `bus_info` string (scripts keyed on it) |
| 5–6 | `eeff39668` `aa2abd940` | Venus parser payload sizes (upstream) | Venus only |
| 7–8 | `c1b507db0` `a4c0f373e` | Venus encoder step 16→1 (upstream) | Venus only |
| 9 | `3ca01ac3c` | runtime-PM reference leaks in power-domain helpers | power-up failures after a failed power-down |
| 10 | `ec7dc2614` | clock unwind in `iris_vpu_power_on_hw()` | clock leak on failed power-on |
| 11 | `f795a7e11` | PM reference dropped twice on resume failure | negative PM usage count |
| 12 | `3525d2dfd` | a command that exactly fills the HFI queue was lost | commands silently never processed |
| 13 | `4dad56614` | **IRQ thread could touch registers after power-off** (replaces upstream `b9c2215bded`, which can deadlock here) | abort/hang at idle, suspend or close |
| 14 | `c06d43d34` | resume failure in `iris_core_deinit()` | PM imbalance / stale state after failed resume |
| 15 | `943bae279` | unbounded wait for system-error recovery | `close()`/STREAMOFF stuck in `D` state |
| 16 | `d41df45f6` | buffers not returned on early `stop_streaming` | vb2 warning |
| 17 | `4f59b78d9` | `open()` error handling; session limit → `-EBUSY` | confusing `-22` later |
| 18 | `b24262e2c` | encoder `try_fmt` clamps size; no upscale | late failure at stream start |
| 19 | `d09cb252b` | crop bounds / `S_SELECTION` consistent; `-EBUSY` while streaming | crop = bounds rejected |
| 20 | `adad2b26f` | frame rate rounded (30000/1001 → 30, was 29) | bitrate 3–4 % off |
| 21 | `e1d1ce3fe` | no self-`memcpy` | hygiene |
| 22 | `65daae492` | bounded firmware version scan | OOB read on corrupt blob |
| 23 | `57d040a4c` | says why firmware does not fit; prints detected generation | `firmware download failed -22` unexplained |
| 24 | `441d38fa7` | nodes published only after runtime PM/DMA setup; remove ordering | first open at boot fails |
| 25 | `e33a87d7e` | warns if the SC7280 node lacks `dma-coherent` | corruption cause visible |

### 2.4 What is and is not established
| Established (checked by me, **P**) | **Not** established |
|---|---|
| Base commit, series content, per-commit clean build (clang 18, arm64, `W=1`; Iris `=m`; tip also with Iris off) | Anything about run-time behaviour on the board |
| `checkpatch --strict` has no code findings; clang static analyzer: 0 findings on touched files | Whether patch 13 (IRQ gate) misses an interrupt or leaves the IRQ line disabled |
| User-space replays: queue-full bug (patch 12), firmware-scan bounds (22), IRQ locking **model** (13; not kernel code) | Whether Gen2 encode is correct on this VPU; whether encoder-side scaling works |
| `git am` of `patches/` on the base reproduces the tree hash above | Whether Iris' nanosecond timestamps (Venus uses µs) matter for rate control |
| | The TrustZone-mode board reset (secondhand report) |

**Deliberately not changed** (decide for yourself if evidence says otherwise — `FIXES.md` §5): Venus' Radxa workarounds, the encoder clock/bandwidth vote (decode table, raw-size input), automatic Gen2→Gen1 fallback, SC7280-specific size caps, `iris_remove()` with open file descriptors.

---

## 3. Material and where it lives

| What | Where |
|---|---|
| Research bundle, docs, tools, tests, patches | `https://github.com/Namitjain07/linux-qcom` branch `claude/dazzling-fermat-u820ir` → `docs/q6a-video/` (PR #1) |
| Kernel source + fix series | `https://github.com/Namitjain07/kernel` branch `claude/q6a-iris-venus-fixes` (PR #1, draft); base branch `linux-7.0.11` |
| Qualcomm reference tree | `https://github.com/qualcomm-linux/kernel` (`main`, 7.3-rc5); upstream commit hashes are quoted in `HISTORY.md` §4 |
| Firmware (pinned) | linux-firmware `30a139cb…` (Gen2 `_s6`), `aeede7af…` (Gen1) — hashes in `EVIDENCE.md` §D |
| Tools | `tools/q6a-video-diag.sh` (read-only), `tools/v4l2-enc-probe.c` (scaling negotiation), **`tools/v4l2-enc-neg-test.c`** (new, patched behaviour), `tools/encode-validate.sh` (PSNR corruption check) |
| User-space reproductions | `tests/ring-queue`, `tests/fw-detect`, `tests/irq-model` (+ `tests/RESULTS.txt`) |

---

## 4. Rules of engagement

Use the tiers in `PLAN.md` §1 (**T0** read-only, **T1** reversible/low risk, **T2** can reset the board, **T3** changes boot/firmware/OS state). Additions for this task:

| Action | Tier | Condition |
|---|---|---|
| Clone, read, build off-board or in a scratch directory, run `tests/` | T0/T1 | free |
| Baseline tests on the unpatched driver | T1 (EL2) / **T2 (EL1)** | EL1 hardware encode needs approval + off-board log stream + `sync` |
| Copy the patched module into `/lib/modules/$(uname -r)/updates/` and run `depmod` | **T3** | **Gate B** (§6) — keep the original, record how to undo |
| Reboot the board | **T3** | **Gate C** — human approves each reboot unless told otherwise |
| Install a full kernel / change bootloader or DT | **T3, human only** | prepare instructions; do not do it |
| Anything touching UEFI, partitions, the bootloader | forbidden | — |

Always: back up to `/var/backups/q6a-video/<name>.<timestamp>` before replacing a file; verify any downloaded firmware against its sha256 before installing; start an off-board `dmesg -w` stream and `sync` before every T2 action; after any reset or SSH drop, wait for the board, then re-check `uptime` and `dmesg | grep "started at EL"` before doing anything else; quote errors verbatim; ≤ 3 hardware-encode attempts per configuration; two unexplained resets in a row → stop.

---

## 5. Workflow

### Phase A — Verify the series offline (no board; T0/T1)
```bash
# (all Phase A and C paths below are relative to ONE working directory that contains the clones: ./kernel-fixes, ./linux-qcom)
# A1 — get the kernel branch (≈ 30 commits of history is enough)
git clone --depth 30 --branch claude/q6a-iris-venus-fixes https://github.com/Namitjain07/kernel kernel-fixes
cd kernel-fixes
git rev-parse HEAD                 # expect e33a87d7e74d46cd0b4ccca4554cf485b60bd2e3
git rev-parse 'HEAD^{tree}'        # expect b53710f2156eb02473d4f7fdb45ae636f7de2482
git log --oneline a50eb5b71..HEAD | wc -l    # expect 25  (fetch more depth if a50eb5b71 is not found)

# A2 — the exported patches must reproduce the same tree from the base (run from a clean checkout of a50eb5b71)
git checkout -q a50eb5b71 && git am --3way $(ls <linux-qcom>/docs/q6a-video/patches/0*.patch | grep -v cover-letter)
git rev-parse 'HEAD^{tree}'        # expect the same tree hash as A1

# A3 — build with clang (cross toolchain, or natively on the board); the tree should produce no warnings at W=1
make O=/tmp/o ARCH=arm64 LLVM=1 defconfig
make O=/tmp/o ARCH=arm64 LLVM=1 W=1 -j"$(nproc)" drivers/media/platform/qcom/iris/ drivers/media/platform/qcom/venus/
#   expect: no "warning:" / "error:". Repeat with CONFIG_VIDEO_QCOM_IRIS=n for the Venus SC7280 path.

# A4 — style
for c in $(git rev-list --reverse a50eb5b71..HEAD); do ./scripts/checkpatch.pl --no-tree --strict -q -g $c; done
#   expected noise only: "Unknown commit id" (shallow clone), Co-Authored-By trailer form, missing Signed-off-by.

# A5 — the user-space reproductions (need only cc)
<linux-qcom>/docs/q6a-video/tests/ring-queue/run.sh   # before: BUG ... after: OK
<linux-qcom>/docs/q6a-video/tests/fw-detect/run.sh    # before: segfault on the last cases ... after: all six correct
<linux-qcom>/docs/q6a-video/tests/irq-model/run.sh    # four lines; see tests/README.md
```
**A6 — independent review (do not skip, this is the point of verification).** Read the diff of the riskiest commits yourself and try to break them. Look specifically at:
* **13** (`iris_hfi_common.c`, `iris_vpu_common.c`, `iris_core.h`): is `hw_powered` set/cleared on *every* power-on/off path (`iris_core_init` error paths, `iris_hfi_pm_suspend/resume`, `iris_core_deinit`)? Can the IRQ line end up disabled with nothing to re-enable it (count `enable_irq`/`disable_irq(_nosync)` on each path, including the watchdog case)? Can a response be lost when the thread skips because `hw_powered` is false?
* **14** (+ `iris_firmware.c`): is it right to skip the no-TZ CPU reset when not powered?
* **17** (`iris_vidc.c`, `iris_vdec.c`, `iris_venc.c`): unwind order in `iris_open()`; any double free of `fmt_src/fmt_dst`.
* **18/19** (`iris_venc.c`): ordering assumptions between OUTPUT and CAPTURE `S_FMT`; behaviour with `ffmpeg`/GStreamer call order.
* **24** (`iris_probe.c`): probe/remove ordering, `devm` actions, error unwind.
Record anything you disagree with as a finding with the exact lines.

**Exit gate A:** V1 holds, or you list exactly what differs. Do not proceed to install if A1/A2/A3 fail.

### Phase B — Baseline on the board, unpatched (T0/T1; T2 in EL1)
Follow `GUIDE.md` G§0–G§6 (setup, `q6a-video-diag.sh`, firmware check, EL2 status, driver/DT sanity, safe probe, encode matrix). Gate on boot mode exactly as `PLAN.md` Phases 1–4 do. Record, as the comparison baseline:
* `uname -r`, `uname -v`, `zcat /proc/config.gz | grep -E 'VIDEO_QCOM_(IRIS|VENUS)|MODULE_COMPRESS|LOCALVERSION'`, `modinfo -n qcom-iris`, `modinfo -F srcversion qcom-iris`, `modinfo -F vermagic qcom-iris`
* `dmesg | grep -iE 'iris|venus|firmware|started at EL'`, DT check `ls /proc/device-tree/soc@0/video-codec@aa00000/dma-coherent`
* `v4l2-enc-probe` verdict, and `encode-validate.sh` results (PSNR avg/worst) for 720p30 and 1080p30 with `h264_v4l2m2m` and `hevc_v4l2m2m`
* the baseline output of `tools/v4l2-enc-neg-test` — **expected to FAIL several lines on the unpatched driver**; that is information, not a problem. (It is T1: it opens the node, no streaming.)
* G§13.4 bitrate accuracy (that one starts a hardware encode → T1 in EL2, T2 in EL1).

### Phase C — Build the patched Iris module (T1)
**Path 1 (preferred, reversible): out-of-tree module against the running kernel's headers.** Iris is `=m` and not needed for boot, so a bad module fails to load instead of bricking the board.
```bash
# C1 — build the UNPATCHED base first and compare it with what is installed: equal srcversion = identical source
sudo apt install linux-headers-$(uname -r) build-essential clang lld 2>/dev/null || true     # package names vary; need headers that match `uname -r`
git -C kernel-fixes worktree add ../iris-base a50eb5b71 2>/dev/null || true                   # or a second checkout of the base
mkdir -p /tmp/iris-build-base && cp -r iris-base/drivers/media/platform/qcom/iris/. /tmp/iris-build-base/
make -C /lib/modules/$(uname -r)/build M=/tmp/iris-build-base CONFIG_VIDEO_QCOM_IRIS=m modules
modinfo -F srcversion /tmp/iris-build-base/qcom-iris.ko        # compare with: modinfo -F srcversion qcom-iris
modinfo -F vermagic   /tmp/iris-build-base/qcom-iris.ko        # must equal the installed module's vermagic
#   If srcversion differs, the board is NOT running a50eb5b71 sources: STOP and report which source it runs.
#   If vermagic differs, fix the build environment (same compiler family / LOCALVERSION) — do not install.

# C2 — build the PATCHED module the same way
mkdir -p /tmp/iris-build-fixed && cp -r kernel-fixes/drivers/media/platform/qcom/iris/. /tmp/iris-build-fixed/
make -C /lib/modules/$(uname -r)/build M=/tmp/iris-build-fixed CONFIG_VIDEO_QCOM_IRIS=m modules
sha256sum /tmp/iris-build-fixed/qcom-iris.ko; modinfo /tmp/iris-build-fixed/qcom-iris.ko | head
```
This external-module route has **not** been tried by me (I only cross-built inside the kernel tree). If the headers/vermagic/symbol-CRC checks above fail, fall back to **Path 2**: a full kernel built from the PR branch with the packaging in the `linux-qcom` repo (`make deb`) — that is **human-only (T3)**: prepare the `.deb`, hand it over with exact install and rollback instructions, and wait.

### Phase D — Install the patched module (T3: **Gate B**, then **Gate C**)
Ask the human, then:
```bash
# D1 — back up and install into updates/ (depmod prefers updates/ over kernel/); nothing under kernel/ is touched
sudo mkdir -p /var/backups/q6a-video /lib/modules/$(uname -r)/updates
sudo cp -a "$(modinfo -n qcom-iris)" /var/backups/q6a-video/qcom-iris.ko.orig.$(date +%s)
sudo install -m 0644 /tmp/iris-build-fixed/qcom-iris.ko /lib/modules/$(uname -r)/updates/qcom-iris.ko
sudo depmod -a
modinfo -n qcom-iris                                          # must now print the updates/ path
# D2 — activate: reboot (Gate C). Do NOT rmmod the old module first: its remove path is one of the things being fixed.
# D3 — rollback (keep this ready): sudo rm /lib/modules/$(uname -r)/updates/qcom-iris.ko && sudo depmod -a && reboot
```
After the reboot: re-establish the log stream, re-check `uptime`, EL mode and `dmesg`, then confirm the patched driver is running (`dmesg` shows `firmware qcom/vpu/…mbn: Gen2 HFI, <n> of <m> bytes of reserved memory`; `modinfo -F srcversion qcom-iris` equals the patched build's).

### Phase E — Test the patched driver (T1 in EL2 / T2 in EL1)
1. `GUIDE.md` **G§13.0–13.1**: confirm patched build; run `tools/v4l2-enc-neg-test` → every line `PASS`. Quote any `FAIL`.
2. Repeat the **G§6** encode matrix with the same parameters as Phase B; compare PSNR avg/worst. A regression versus the baseline is a finding for the *specific patch* you suspect.
3. **G§9** open/encode/close ×20, then **G§13.3** (5× encode, `sleep 5`, so runtime suspend happens in between). After each, `dmesg | grep -iE 'timed out|watchdog|arm-smmu|system error|Unhandled|synchronous external abort|queue full'` → expect nothing.
4. **Remove/probe path (patch 24) — only with the human's OK (Gate D; treat as T2):** `sudo modprobe -r qcom-iris && sudo modprobe qcom-iris`, then check both `/dev/video*` nodes return and `dmesg` has no errors; repeat 3×. A hang here is a high-value finding (stop, collect `dmesg`, report; do not retry more than twice).
5. **Session limit (patch 17):** optional — open > 16 sessions, expect `-EBUSY` on the 17th. Skip if awkward.
6. **G§13.4** bitrate accuracy again; compare with the baseline number (answers the nanosecond/microsecond timestamp question — see `FIXES.md` §5).
7. **Scaling** per `GUIDE.md` G§10: `v4l2-enc-probe` verdict, then, **only if** it says the coded size can stay smaller than the raw size, the streamed test; always also produce the CPU-scale → VPU-encode pipeline so a working path exists whatever the answer.
8. If something fails: apply **one** change at a time, ≤ 3 attempts, per the symptom table (`GUIDE.md` G§11) and the symptom→patch table in §2.3. To isolate a patch, build a module from the tree with that single commit reverted (`git revert --no-commit <sha>` in a scratch worktree; remember **14 depends on 13**) and re-run only the failing test.

### Phase F — Report (§7)

---

## 6. Gates — stop and ask the human

| Gate | When | What to tell them |
|---|---|---|
| **A** | Phase A fails (hash/base mismatch, build error, a real defect found in review) | what differs, exact lines, whether you recommend continuing |
| **B** | Before copying the patched module to `updates/` | the sha256 of the module, the original's backup path, the one-line rollback, the exact reboot you need |
| **C** | Before every reboot | why, what you will check afterwards |
| **D** | Before any T2 action (hardware encode in EL1; remove/probe loop; stress) | the exact command and what could happen |
| **E** | Two unexplained resets, or evidence that a kernel rebuild/install is needed | state, logs, recommendation |

Keep messages short: *what I did, what I saw (verbatim), what I propose, what I need from you.*

---

## 7. What to return

1. **Results file** — the template in `GUIDE.md` G§12 (it already has the S9 rows), fully filled, with all raw logs attached (`diag-before.txt`, `diag-after.txt`, `probe.txt`, `matrix-baseline.txt`, `matrix-patched.txt`, `neg-test-baseline.txt`, `neg-test-patched.txt`, `stability.txt`, `board-dmesg-*.log`, `journalctl -b -1` if a reset happened).
2. **Per-patch verdict table:**

   | Patch | Verified how | Result on board | Verdict (KEEP / DROP / FIX) | Evidence |
   |---|---|---|---|---|
   | 1 … 25 | offline / on board / not exercised | | | file + exact log line |

   "Not exercised" is an acceptable and honest answer for most of 1–8, 10, 21–22; say so.
3. **Findings you disagree with** from Phase A6, and anything the documents got wrong (with your evidence).
4. **Open questions closed or still open**, using `REPORT.md` §6 plus the two new ones: does patch 13 behave (no missed interrupts, no stuck IRQ line), and what is the bitrate accuracy with the Gen2 (and, if tried, Gen1) firmware?
5. **A recommendation** for the human: keep the series, keep a subset (name it), or drop it; and what to do about the draft PR (mark ready, add `Signed-off-by`, or close). **Do not push, merge, or mark anything ready yourself.**

---

## 8. Quick reference

```text
Board      Radxa Dragon Q6A, QCS6490 (SC7280 / kodiak), VPU 2.0, Iris driver (qcom-iris.ko)
Kernel     Radxa linux-7.0.11, base a50eb5b71984bd82b49cd5d31b1e2eb5505c6512
Series     Namitjain07/kernel branch claude/q6a-iris-venus-fixes  tip e33a87d7e74d46cd0b4ccca4554cf485b60bd2e3
           tree b53710f2156eb02473d4f7fdb45ae636f7de2482   (25 commits)
Gen2 fw    qcom/vpu/vpu20_p1_gen2_s6.mbn  f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f   needs 0x700000 carveout
Gen1 fw    qcom/vpu/vpu20_p1.mbn          607545ead3f23134680a1330cd569f2a6353f8b83dbabb924c68ac573f9d06c1   needs 0x500000 carveout
Rejected   old Gen2 (MBN v7)              3c21c8ca9daf60e5d21cd6d3c1af0420790ea2c260bfd73511255a01aae3e744
EL2 check  dmesg | grep "All CPU(s) started at EL2"      (UEFI: Hypervisor Override on; human only)
Nodes      "Iris Encoder" / "Iris Decoder": for d in /dev/video*; do v4l2-ctl -d $d --info | grep -q 'Iris Encoder' && echo $d; done
Symptoms   GUIDE.md G§11 (exact log strings)         Tiers  PLAN.md §1          Fix details  FIXES.md
Rollback   rm /lib/modules/$(uname -r)/updates/qcom-iris.ko && depmod -a && reboot
```

*Prepared with Claude Code. Everything in this file about run-time behaviour on the board is unverified until you have run the tests.*
