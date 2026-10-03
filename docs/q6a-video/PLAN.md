# Plan — get hardware encode (and scaling) working on the Dragon Q6A

Read `REPORT.md` first for the reasoning. Commands live in `GUIDE.md` (section numbers are referenced as **G§n**). This plan says **what to do, in what order, who may do it, and when to stop**.

## 0. Objective and success criteria

**Objective:** reliable H.264 and HEVC hardware encoding on the Q6A, plus a verified answer to "can the VPU scale?".

The work is **done** when all of these hold (record each in the results file, G§12):

| # | Criterion | Measured by |
|---|---|---|
| S1 | Video nodes appear and open without kernel errors | `q6a-video-diag.sh` sections 3, 6, 8 |
| S2 | H.264 and HEVC encode at 1280×720 **and** 1920×1080 pass the corruption check | `encode-validate.sh` exit 0, worst-frame PSNR ≥ 30 dB |
| S3 | Non-divisor frame rates work (25 and 29 fps) | same, `FPS=25`, `FPS=29` |
| S4 | 4K30 encode attempted and result recorded (pass/fail is data, not a blocker) | `W=3840 H=2160` |
| S5 | 20 consecutive open → encode → close cycles with **zero** `arm-smmu` faults and no `system error` | G§9 |
| S6 | 5-minute sustained 1080p30 encode without hang, drop or fault | G§9 |
| S7 | Scaling question answered with evidence: probe verdict + (if applicable) streamed result, **or** a working CPU/GPU-scale → VPU-encode pipeline | G§10 |
| S8 | A written results file returned to the user | G§12 |
| S9 | *(only if the patched kernel is installed)* negotiation self-test passes and output bitrate is within ±15 % of the target (first-guess threshold, adjust after the first measurement) | G§13 |

## 1. Rules of engagement (authority tiers)

| Tier | Meaning | Examples | Who may do it |
|---|---|---|---|
| **T0** | Read-only | `dmesg`, `ls`, reading `/proc/device-tree`, running the diag script **without** `ENCODE_TEST` | agent |
| **T1** | Reversible, low risk | Copying a verified firmware blob into `/lib/firmware/qcom/vpu/` after backing up; compiling/running `v4l2-enc-probe`; running `encode-validate.sh` with a **software** encoder | agent |
| **T2** | Can reset the board or lose unsaved work | Any **hardware** encode while the system is in TrustZone mode; repeated open/close stress | agent **only after** explicit human approval + log streaming + `sync` |
| **T3** | Changes boot/firmware/OS state | UEFI settings, kernel/DTB installation, package removal, reboots of a system others use, flashing anything | **human only** (agent prepares instructions) |

Hard rules for the agent:
1. Never modify UEFI/board firmware, partition tables or bootloader. Never run `rm` on anything under `/lib/firmware` except a file you created this session.
2. Back up before changing: copy any file you replace to `/var/backups/q6a-video/<name>.<timestamp>`.
3. Before every T2 action: start log streaming off-board (G§0) and run `sync`.
4. **Maximum 3 hardware-encode attempts per configuration.** If the board resets twice in a row, stop and report; do not loop.
5. Do not change the kernel config or DT on the board. Kernel work is Phase 6 and happens in the repo, not on the board.
6. Report failures verbatim (log lines, exit codes). Do not summarise away error text.

## 2. Phases

### Phase 0 — Prepare (T0/T1) · ~15 min
- Get the bundle: `git clone --depth 1 --branch claude/dazzling-fermat-u820ir https://github.com/namitjain07/linux-qcom` → `docs/q6a-video/`.
- Confirm access: ssh to the board, `sudo` works, note whether a **serial console** and **UEFI access** exist (ask the human; both are needed for Phase 3 and for post-reset evidence).
- Make logs survive a reset: persistent journal (G§0), and start off-board streaming `ssh board 'sudo dmesg -w'`.
- Create `/var/backups/q6a-video/`.
**Exit gate:** you can stream the kernel log off-board; backups dir exists; human confirmed tiers T2/T3 contacts.

### Phase 1 — Baseline diagnosis (T0) · ~10 min
Run `tools/q6a-video-diag.sh` (read-only, **no** `ENCODE_TEST`) → G§1. Capture the whole output.
Extract these facts (they drive every later decision):

| Fact | Where in output | Why it matters |
|---|---|---|
| Kernel version, distro, package versions | section 1 | 6.18 vs 7.0.11 path |
| Boot mode **EL1 / EL2** | section 2 | TZ vs EL2 |
| Bound driver (iris / venus) | section 3 | confirms 3.2 of the report |
| `firmware-name`, `dma-coherent`, `video-firmware`, `iommus`, carveout | section 4 | DT correctness |
| Firmware files present, fingerprint, MBN header, size | section 5 | cause #1 / #3 |
| Existing video/SMMU log lines | section 6 | prior failures |

**Route** (take the first matching row):

| Finding | Go to |
|---|---|
| Kernel is **6.18.x** | Stop and ask the human whether to move to the 7.0.11 build (Phase 6/Option A). 6.18 + Venus is **not** a supported route (REPORT §5) |
| Gen2 blob missing, or `vpu20_p1_gen2.mbn` is MBNv7, or Gen1 fingerprint is `…8fd4ba98…` | **Phase 2** |
| Mode = EL1 (TrustZone) and firmware OK | **Phase 3** (human) |
| Mode = EL2 and firmware OK | **Phase 4** |
| Driver is not bound / no `/dev/video*` | G§11 "No device" row, then report |

### Phase 2 — Firmware (T1) · ~10 min
Install the pinned, hash-verified Gen2 blob and make sure a current Gen1 blob exists as fallback (G§2). Prefer a `linux-firmware` package built from **commit `30a139cb` or later** if the distro has one; otherwise place the files manually.
**Exit gate:** `sha256sum` of `/lib/firmware/qcom/vpu/vpu20_p1_gen2_s6.mbn` equals `f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f`, and the diag script's section 5 says "Gen2 2026-02-20 … correct for SC7280".
**Rollback:** restore from `/var/backups/q6a-video/`.
No reboot needed: Iris loads firmware on the next `open()` of a video node. (A reboot is still the cleanest way to clear a half-initialised core after earlier errors.)

### Phase 3 — Enable EL2 (T3, human) · ~10 min
The agent prepares and **hands over** these instructions (G§3); a human performs them:
1. Reboot into UEFI; set **Hypervisor Settings → Hypervisor Override → Enabled** (menu wording is from a forum report, **S**; the human knows the real UI).
2. Boot Linux.
3. Agent verifies: `dmesg | grep "started at EL"` shows **EL2**; `video-firmware` subnode present; diag section 2 says EL2; the Radxa overlay `qcs6490-radxa-kvm.dtbo` exists on disk (`find /boot /usr/lib -name '*radxa-kvm*'`).
**Exit gate:** EL2 confirmed.
**If the human declines EL2** → Phase 7a (TZ-mode test, T2, strict limits).
**Side effects to tell the human:** the overlay disables the GPU zap shader (Linux does it in EL2) and sets `qcom,broken-reset` on ADSP/CDSP remoteprocs. Other Radxa features may behave differently; check what else they rely on.

### Phase 4 — Functional verification (T1 → T2) · ~45 min
In EL2 the risk class drops, but **still** stream logs and `sync` first.
1. Safe probe: `v4l2-enc-probe` (G§5) — negotiation only.
2. Encode matrix with corruption check (G§6): H.264 and HEVC × {720p30, 1080p30, 1080p25, 1080p29, 4K30 (data only)}.
3. Client check: one GStreamer pipeline and one ffmpeg command each (G§6).
4. Stability: 20 open/encode/close cycles (G§9) then a 5-minute 1080p30 run.
5. After each step: `dmesg` grep for `arm-smmu`, `system error`, `SFR message`, `firmware download failed`, `Gen1 FW detected`.
**Exit gate:** criteria S1–S6 met → go to Phase 5.
**On any failure:** do not retry blindly — use the symptom table (G§11), apply at most one fix, re-run the failing test (≤ 3 attempts), then Phase 7 or report.

### Phase 5 — Scaling (T0/T1, then T2) · ~30–60 min
1. Read the probe verdict from Phase 4 step 1 (G§10.1).
2. **If the driver forces the coded size back to the raw size:** scaling during encode is not available; use a pre-scaler (G§10.3) and validate by encoding at the target size.
3. **If the driver keeps a smaller coded size:** this only proves V4L2 *accepts* it. Prove the hardware *does* it with a streamed test (G§10.2). This needs a small program that streams NV12 frames in at 1920×1080 and reads H.264 out at 1280×720; the supplied probe does **not** stream. Writing it is an optional work item (spec in G§10.2). Validate by decoding the output and checking `width×height` and PSNR against a software-scaled reference.
4. Always produce a CPU-scale → VPU-encode baseline so there is a working pipeline regardless (G§10.3). A GPU path (Vulkan `scale_vulkan`) is **untested on this board** — try only if CPU cost is a problem.
**Exit gate:** S7.

### Phase 6 — Kernel work (conditional; repo, not board) · hours
**Status:** the series exists — 25 commits, PR `Namitjain07/kernel#1`, exported in `patches/` (explained in `FIXES.md`). It is **compile-tested only**. Treat it as a candidate, not a fix.

Use it when Phase 4/7 shows a symptom the series addresses, or when the human wants the hardening anyway:
| Symptom | Patches (see `FIXES.md` §1) |
|---|---|
| Encoder/decoder hang at end of stream (drain/stop) | 1 |
| Abort / `Unhandled fault` right after idle, suspend or close; hang on first open after suspend | 13 (and 9, 11, 14) |
| Core never powers down; repeated open failures; negative PM usage count | 9, 11, 14 |
| Command silently never processed under heavy queueing | 12 |
| `firmware download failed -22` with no explanation; wrong HFI generation | 23 (diagnosis), 22 |
| `/dev/video*` present but first open fails at boot | 24 |
| `close()` / `STREAMOFF` stuck in `D` state after a firmware error | 15, 16 |
| Encoder: bitrate off at 29.97/23.976 fps; Gen1 GOP size ignored | 20, 3 |
| Sizes accepted then failing at stream start; crop bounds rejected; upscale requested | 18, 19 |
| Corrupted frames, DT without `dma-coherent` | 25 (warns; the fix is the DT property) |

Procedure (human does the install — T3):
1. Build off-board: `make -C <kernel> O=<out> ARCH=arm64 LLVM=1 -j"$(nproc)"` on the PR branch (or `git am` the files in `patches/`), or add the patches to `debian/patches/linux/` with `src/` paths (see `patches/README.md`) and `make deb`.
2. Install the kernel/modules on the board, reboot.
3. Run G§13 and compare with the unpatched numbers from Phase 4.
4. If any criterion regresses, revert to the previous kernel and report which commit the symptom points to (`FIXES.md` §6).

Not implemented, on purpose: automatic Gen2→Gen1 fallback (hides misconfiguration; #23 makes the cause visible); AHB-bridge reset before HW mode (kernel-topics PR #1907, unmerged, unverified for VPU2).

### Phase 7 — Fallbacks (only if Phase 4 fails after fixes)
- **7a. TZ-mode test (T2).** Only with human approval, verified firmware, log streaming and ≤ 3 attempts. Purpose: learn whether the reboot reproduces with correct firmware (turns S into P).
- **7b. Venus + Gen1 route.** Build with `CONFIG_VIDEO_QCOM_IRIS=n` (Venus then re-enables its SC7280 entry), remove `firmware-name` from the Q6A DTS `&venus` node so Venus uses `qcom/vpu-2.0/venus.mbn`, ensure Gen1 ≥ `aeede7af`. Needs a kernel rebuild (T3). Not Radxa-supported.
- **7c. Report and stop** with the evidence collected.

### Phase 8 — Hand-off (T0)
Fill the results file (G§12), attach all raw logs, state the outcome of S1–S8, list anything unresolved. Send to the human.

## 3. Decision tree after a failed encode

```
encode failed / board reset / corrupt output
├─ board RESET (no clean error)
│    ├─ mode EL1 → expected hazard; do NOT repeat. Go EL2 (Phase 3) or report. Evidence: journalctl -k -b -1, pstore, off-board dmesg stream
│    └─ mode EL2 → capture serial log; check `arm-smmu` / SID in the last lines; report (new finding)
├─ "Direct firmware load … -2" / "firmware download failed -2" → firmware missing/misnamed → Phase 2
├─ "firmware download failed -22" → carveout < 0x700000 for Gen2 → DT carveout (Radxa DTS has 7 MiB; confirm), or use a Gen1 firmware (the patched driver, Phase 6 #23, prints the needed and available sizes)
├─ "auth and reset failed: …" or "error … initializing firmware" → bad/unsigned blob (MBNv7?) → Phase 2, verify hash
├─ "Gen1 FW detected in <gen2 file>" → impossible for the real blob; means the file is not the real Gen2 → re-verify hash
├─ "core init failed" / "error booting up iris firmware" / "invalid setting for uc_region" → firmware boot failure → check mode, memory-region, firmware; capture all lines before it
├─ "SFR message from FW: …" → firmware's own fatal text; quote it verbatim in the report
├─ "Unhandled context fault" / SMMU sid in log → IOMMU mapping problem; record SID (0x2184 ⇒ TZ/EL2 mismatch)
├─ output decodes but PSNR < 30 → corruption → compare with software baseline at same size/bitrate; check `dma-coherent` present (diag §4); check firmware generation
├─ GStreamer "internal data stream error" at 25/29 fps → missing frame-interval fix (Radxa 38befa2de) → wrong kernel build
└─ hang (timeout 124) → collect SFR + `dmesg`; Phase 6 patches 1, 13, 14 (`FIXES.md` §1)
```

## 4. Risk register
| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Board hard-resets during hardware encode in TZ mode | medium (reported, S) | lost session/logs | Prefer EL2; off-board log stream; persistent journal; `sync`; ≤ 3 attempts |
| Wrong/old firmware silently used | medium | confusing failures | hash-verify; diag section 5 |
| EL2 changes behaviour of other peripherals | low–medium | regressions elsewhere | tell the human; revert UEFI setting to roll back |
| Gen2 encode on VPU 2.0 is buggy (VPU3.3 buffer formulas, **U**) | unknown | encode fails with firmware buffer-size error | SFR messages; fall back to Gen1 under Iris (remove DT `firmware-name`, needs rebuild) or Venus route |
| Hardware scaling does not exist for encode | medium | need pre-scaler | CPU/GPU scaling baseline (G§10.3) |
| PSNR threshold false-fails a healthy hardware encoder | low–medium | false alarm | compare against `ENC=libx264` at same settings before concluding |

## 5. What the agent must return
The completed results file (G§12) + raw logs: diag output (before and after), probe output, every `encode-validate.sh` run, the off-board dmesg stream, `journalctl -b -1 -k` if a reset happened, and the exact commands used. State plainly which of S1–S8 passed, failed, or was not attempted and why.
