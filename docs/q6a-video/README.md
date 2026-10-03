# Dragon Q6A (QCS6490) — hardware video encode & scale: research bundle

Everything needed to diagnose and fix hardware video encoding on the Radxa Dragon Q6A, written so a human **or an AI agent with access to the board** can execute it.

**Start here → give your agent [`AGENT_BRIEF.md`](AGENT_BRIEF.md)** (bring-up on the stock kernel). To have it **verify, install and test the driver fix series**, give it [`AGENT_PROMPT_VERIFY_FIXES.md`](AGENT_PROMPT_VERIFY_FIXES.md) instead (or after) — it is self-contained and carries the full context. Both tell the agent what to read, what it may and may not do, and what to return.

## TL;DR
- **Recommended way:** Radxa `linux-7.0.11` (what this repo builds) + **Iris** driver + Gen2 firmware `qcom/vpu/vpu20_p1_gen2_s6.mbn` + **Hypervisor Override (EL2) enabled in UEFI**. This is the configuration Radxa's merged PRs #593/#607 target for QCS6490, and EL2 avoids a documented secure-stream-ID crash class that can reset the board during encode in TrustZone mode.
- **Most likely causes of failure on a system that "doesn't work":** (1) the Gen2 firmware blob is missing or is the old MBNv7 file; (2) the board is in default TrustZone mode and encode resets it; (3) on 6.18 + Venus, the missing `dma-coherent` property (corrupt output) and old Gen1 firmware.
- **Scaling:** the VPU's scaler is proven only on the decode side and neither driver exposes it. Encoder-side scaling through Iris is plausible but **unproven**; scale on CPU/GPU first, and test the hardware path with the supplied probe.
- **Source review + patches (new):** the Iris and Venus sources were read end to end and **25 commits of fixes** (loading, power/DMA, IRQ/teardown synchronisation, encoder, resolution/scaling) are in [`Namitjain07/kernel#1`](https://github.com/Namitjain07/kernel/pull/1) and exported in [`patches/`](patches/). They build clean and three bugs are reproduced/fixed in user space — see [`FIXES.md`](FIXES.md).
- **Status:** research and patches complete; **nothing has been run on hardware yet** (neither the original plan nor the patched kernel).

## Contents
| File | What it is |
|---|---|
| [`AGENT_BRIEF.md`](AGENT_BRIEF.md) | Paste-ready brief: role, rules of engagement, workflow, return format |
| [`AGENT_PROMPT_VERIFY_FIXES.md`](AGENT_PROMPT_VERIFY_FIXES.md) | **New.** Self-contained prompt + context to verify the 25-patch series, install the patched module safely, test it on the board and return a per-patch verdict |
| [`PLAN.md`](PLAN.md) | Phased plan with success criteria, authority tiers, gates, rollback, decision tree, risks |
| [`GUIDE.md`](GUIDE.md) | Copy-paste runbook (setup, firmware, EL2, probes, encode matrix, stability, scaling, symptom table, results template) |
| [`REPORT.md`](REPORT.md) | The research: findings, ranked causes, recommendation, corrections, open questions |
| [`EVIDENCE.md`](EVIDENCE.md) | Claim-by-claim ledger, commit/PR index, firmware fingerprints, sources, re-verification commands |
| [`FIXES.md`](FIXES.md) | **New.** Source-level problems → root cause → patch → evidence; what was deliberately not changed; risks; board test plan |
| [`HISTORY.md`](HISTORY.md) | **New.** Venus/Iris history across the three repos, timeline, Radxa-only commits, disposition of every missing upstream fix |
| [`patches/`](patches/) | **New.** The 25-commit series (`git format-patch`), applies to Radxa `linux-7.0.11` @ `a50eb5b71` |
| [`tests/`](tests/) | **New.** User-space reproductions (command queue, firmware detection, IRQ locking model) with captured output |
| [`data/`](data/) | **New.** Commit lists behind `HISTORY.md` (TSV) |
| [`tools/q6a-video-diag.sh`](tools/q6a-video-diag.sh) | Read-only diagnostics (+ opt-in encode tests) |
| [`tools/v4l2-enc-probe.c`](tools/v4l2-enc-probe.c) | Safe scaling-negotiation probe (no encode session) |
| [`tools/encode-validate.sh`](tools/encode-validate.sh) | Encode → decode → PSNR corruption check |
| [`tools/v4l2-enc-neg-test.c`](tools/v4l2-enc-neg-test.c) | **New.** Negotiation self-test for the *patched* encoder (PASS = patched behaviour); no streaming |

## What was and was not verified
- **Verified by me (primary):** kernel source and history (Radxa 7.0.11 and 6.18.2, Qualcomm main), PR/commit text, the real firmware binaries (strings, MBN headers, sizes, hashes), tool logic against software encoders and fake firmware trees.
- **Secondhand:** the forum report of board resets on encode in TrustZone mode; Qualcomm's statement about SID `0x2184` being an encode-specific secure buffer (both from search snippets of pages I could not open).
- **Done since:** every patch compiles (clang 18, arm64, `W=1`, per commit); checkpatch and the clang static analyzer are clean on the touched code; three bugs reproduced and shown fixed in user space.
- **Not done:** any on-device run (of the plan *or* the patched kernel); testing the `.zst` firmware-reading branch; GPU scaling; encoder-side scaling proof.

See `REPORT.md` §7 for the corrections I made along the way, and `EVIDENCE.md` §F for known gaps.

## If you want the patched driver
The patches are optional: the original recommendation needs no kernel change. They address real defects found in the source (see `FIXES.md` §2) and should be treated as a **candidate** until the tests in `FIXES.md` §7 / `GUIDE.md` G§13 have been run. Installing a kernel or modules is a human (T3) action.

## For the human
1. Decide whether you are willing to enable **Hypervisor Override** in UEFI (the plan needs it; it is reversible).
2. Make sure you can be present for the reboot steps and, ideally, attach a serial console.
3. Give the agent `AGENT_BRIEF.md`. It will stop and ask you at the gates listed there.

Prepared with Claude Code.
