# Agent brief — paste this into your agent (or point it at this file)

You are helping bring up **hardware video encoding (and scaling)** on a **Radxa Dragon Q6A** (Qualcomm QCS6490, "SC7280/kodiak"). You can reach the board over SSH (and possibly a serial console). A research bundle with the diagnosis, plan, runbook and tools is in this repository:

```
git clone --depth 1 --branch claude/dazzling-fermat-u820ir https://github.com/namitjain07/linux-qcom
cd linux-qcom/docs/q6a-video
```

Clone it on **your own system** to read the documents. `GUIDE.md` §0 clones a second copy **on the board** (to `~/linux-qcom-docs`) so the scripts under `tools/` can run there; commands marked `board$` run on the board (via SSH), `host$` on your system.

Read in this order: `README.md` → `PLAN.md` → `GUIDE.md` → `REPORT.md` (reasoning) → `EVIDENCE.md` (sources). Then execute `PLAN.md` using the commands in `GUIDE.md`.

## What is already established (do not re-derive; verify only what the plan asks)
- The kernel is Radxa `linux-7.0.11`; on it **Iris** is the only driver for this SoC. Radxa's own merged PRs (#593, #607) say encode/scaling were fixed for QCS6490 in **EL2** with Iris.
- Both firmware images contain H.264 + HEVC encoders. The correct Gen2 blob is `qcom/vpu/vpu20_p1_gen2_s6.mbn` (sha256 `f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f`); an older Gen2 file with an MBNv7 header is rejected by this SoC.
- In **default TrustZone (EL1) mode** a hardware encode is reported (secondhand) to **reset the board**. The recommended route is **EL2 (UEFI Hypervisor Override enabled)**.
- The VPU's scaler is proven only on the decode side and neither driver exposes it. Encoder-side scaling is **unproven**: treat "VPU can scale while encoding" as a hypothesis to test, not a fact.

## What is NOT established (say so in your report; do not paper over it)
No on-device test has been run. The TZ-mode reboot report, Gen2 encode correctness on this hardware, and encoder-side scaling are all open. Report what you observe, verbatim.

## Authority tiers (full table in `PLAN.md` §1)
- **T0 read-only** and **T1 reversible/low-risk** → you may do these.
- **T2 can reset the board** (any hardware encode while in EL1; stress loops) → only with explicit human approval, with an off-board `dmesg -w` stream running, after `sync`, max 3 attempts per configuration, stop after two unexplained resets.
- **T3 changes boot/firmware/OS state** (UEFI settings, kernel/DTB install, package removal, rebooting a shared system, flashing) → **the human does it**; you prepare exact instructions.

## Hard rules
1. Never touch UEFI/board firmware, partitions or the bootloader. Never delete anything under `/lib/firmware` that you did not create this session. Back up before replacing (`/var/backups/q6a-video/`).
2. Verify every downloaded firmware file against the sha256 in `GUIDE.md` §2 **before** installing. Mismatch → stop.
3. Do not edit kernel config, DT or packages on the board. Kernel changes are Phase 6, made in the repo, and installed only with human approval.
4. After any reset or SSH drop: wait for the board, re-check `uptime` and `dmesg | grep "started at EL"`, and re-establish the log stream before doing anything else.
5. Quote errors verbatim. Do not summarise away log lines, exit codes or SFR messages.
6. If something contradicts `REPORT.md`, believe your measurement, record it, and flag the contradiction.

## Workflow (details in PLAN.md / GUIDE.md)
0. Setup: tools, persistent journal, off-board log stream (G§0).
1. Baseline (read-only): `tools/q6a-video-diag.sh` (G§1). Decide the route from the table in PLAN Phase 1.
2. Firmware: pinned, hash-verified install if needed (G§2).
3. EL2: ask the human to enable Hypervisor Override; verify `All CPU(s) started at EL2` (G§3).
4. Verify: safe probe (G§5) → encode matrix with the corruption check `tools/encode-validate.sh` (G§6) → stability (G§9).
5. Scaling: probe verdict → streamed test only if needed → always a working CPU-scale → VPU-encode pipeline (G§10).
6. If something fails: use the symptom table (G§11), apply **one** fix at a time, ≤ 3 attempts, then fallbacks (PLAN Phase 7) or report.
7. Return the results file (G§12) with all raw logs.

## Talking to the human — ask at these gates
- Before any T2 action (state exactly what you will run and what could happen).
- Before/after the UEFI change (G§3) — you cannot do it yourself.
- If the kernel is 6.18.x (the plan says this route is not supported).
- If two resets occur, or the evidence points to needing a kernel rebuild.
Keep messages short: *what I did, what I saw (verbatim), what I propose, what I need from you.*

## Definition of done
Criteria S1–S8 in `PLAN.md` §0, each marked PASS / FAIL / NOT RUN with evidence, plus the unresolved items and your recommended next step.

## Tools in `tools/`
| File | Purpose | Risk |
|---|---|---|
| `q6a-video-diag.sh` | Read-only system/firmware/DT/log report; `ENCODE_TEST=1` additionally runs encoders (opt-in) | T0 (T2 with `ENCODE_TEST=1`) |
| `v4l2-enc-probe.c` | Negotiation-only check: can the coded size differ from the raw size? Starts no encode session | T1 |
| `encode-validate.sh` | Encode a deterministic pattern, decode it, compare PSNR frame-by-frame; detects corruption. Verified with software encoders; **never run on hardware yet** | T1 with `ENC=libx264`; T2 in EL1 with a hardware encoder |
