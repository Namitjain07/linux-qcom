# Runbook — Dragon Q6A hardware encode / scale

Copy-paste procedure for the agent. Strategy and reasoning are in `PLAN.md` / `REPORT.md`. Section numbers (`G§n`) are referenced from the plan.

Conventions: `board$` = run on the board (needs `sudo`); `host$` = run on the machine the agent lives on. Tiers T0–T3 are defined in `PLAN.md` §1. **Do not skip the Safety box.**

> ### Safety
> * A community report says that on the Q6A in **default TrustZone (EL1) mode** a hardware encode can **reset the whole board** (**S**, not independently verified). Treat any hardware encode in EL1 as **T2**.
> * Before every T2 action: log stream running off-board, `sync`, and a human who knows you are about to do it.
> * ≤ 3 attempts per configuration. Two unexplained resets in a row → stop and report.
> * After any reset the SSH session drops. Wait for the board (`ping`), then **re-verify state** (`uptime`, `dmesg | grep "started at EL"`) before continuing; do not assume anything survived.

---

## G§0 Setup (T0/T1)

```bash
# board$ — tools the scripts need (Debian/Ubuntu names)
sudo apt-get update
sudo apt-get install -y ffmpeg v4l-utils gstreamer1.0-tools gstreamer1.0-plugins-base \
     gstreamer1.0-plugins-good gcc xxd zstd curl
sudo mkdir -p /var/backups/q6a-video

# board$ — make the kernel log survive a reset (persistent journal)
sudo mkdir -p /var/log/journal && sudo systemctl restart systemd-journald
journalctl --disk-usage          # should report a non-zero persistent size

# board$ — get the bundle
git clone --depth 1 --branch claude/dazzling-fermat-u820ir https://github.com/namitjain07/linux-qcom ~/linux-qcom-docs
cd ~/linux-qcom-docs/docs/q6a-video && ls
```

Off-board log stream (**start before any T2 action; leave it running**):

```bash
# host$ — survives a board reset up to the moment of the reset
ssh <board> 'sudo dmesg -w' | tee "board-dmesg-$(date +%s).log"
# optional second copy if a serial adapter is attached (baud is typically 115200; confirm with the human)
picocom -b 115200 /dev/ttyUSB0 | tee "board-serial-$(date +%s).log"
```

## G§1 Baseline diagnosis (T0)

```bash
# board$ — READ-ONLY (no encoder is started)
cd ~/linux-qcom-docs/docs/q6a-video/tools
sudo bash q6a-video-diag.sh 2>&1 | tee ~/diag-before.txt
```

How to read it:

| Section | Good | Bad → action |
|---|---|---|
| 2 Boot mode | `EL2` | `EL1` → G§3 (human) |
| 3 Driver | `qcom-iris` bound to `…video-codec@aa00000`; `VIDEO_QCOM_IRIS=m` | none bound → G§11 "No device" |
| 4 DT | `dma-coherent: PRESENT`; carveout size `0x700000`; `video-firmware` present in EL2 | `MISSING` dma-coherent → wrong kernel (6.18-era); carveout `0x500000` with Gen2 → will fail `-22` |
| 5 Firmware | `vpu20_p1_gen2_s6.mbn` → "Gen2 2026-02-20 … correct for SC7280", MBN hdr v6, `mdt_size=0x700000`; `vpu20_p1.mbn` → "CURRENT Gen1" | MISSING, or MBN hdr v7, or Gen1 `…8fd4ba98…` "PRE-FIX" → G§2 |
| 6 Kernel log | no `firmware download failed`, no `arm-smmu … fault` | any → G§11 |

## G§2 Firmware (T1)

Pinned to linux-firmware commit `30a139cb656157cba7a27fd7727b5ffbd302ff60` (Gen2) and `aeede7afb7a186b62f9e1f959c33fd5f2dea0f7a` (Gen1). The current `main` blobs were byte-identical to these when checked (linux-firmware main `50c28bbf…`, 2026-10-01). If the distro already ships `linux-firmware` from that commit or later, install that package **instead** and just verify the hashes below.

```bash
# board$ (or download on the host and scp the two files)
cd /tmp
curl -fLO https://gitlab.com/kernel-firmware/linux-firmware/-/raw/30a139cb656157cba7a27fd7727b5ffbd302ff60/qcom/vpu/vpu20_p1_gen2_s6.mbn
curl -fLO https://gitlab.com/kernel-firmware/linux-firmware/-/raw/aeede7afb7a186b62f9e1f959c33fd5f2dea0f7a/qcom/vpu/vpu20_p1.mbn

# MUST print "OK" twice — otherwise STOP, do not install
echo "f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f  vpu20_p1_gen2_s6.mbn" | sha256sum -c -
echo "607545ead3f23134680a1330cd569f2a6353f8b83dbabb924c68ac573f9d06c1  vpu20_p1.mbn"        | sha256sum -c -

# back up whatever is there, then install
for f in vpu20_p1_gen2_s6.mbn vpu20_p1.mbn; do
  for suf in "" .zst .xz; do
    [ -e /lib/firmware/qcom/vpu/$f$suf ] && sudo cp -a /lib/firmware/qcom/vpu/$f$suf /var/backups/q6a-video/$f$suf.$(date +%s)
  done
  sudo install -D -m 0644 $f /lib/firmware/qcom/vpu/$f
done
ls -l /lib/firmware/qcom/vpu/
```

Notes
* The kernel tries the raw `.mbn` first and only falls back to `.zst`/`.xz`, so this overrides a packaged compressed copy without touching the package. A later package upgrade may replace it — re-run the diag script after upgrades.
* Leave `vpu20_p1_gen2.mbn` alone (old MBNv7 name). The Radxa DTS requests the `_s6` name; do not delete or rename package files.
* No reboot needed — firmware loads on the next `open()` of a video node. Reboot anyway if a previous attempt left the core in an error state.
* Re-run `sudo bash tools/q6a-video-diag.sh | sed -n '/5. Firmware/,/6. Kernel/p'`. Expected: Gen2 `…rel0059…` "correct for SC7280", Gen1 `…ed457c18…` "CURRENT Gen1".

## G§3 Enable EL2 / Hypervisor Override (T3 — **human**)

Hand the human these instructions; the agent does not do this.

1. Reboot; enter UEFI setup (key per Radxa's Q6A documentation).
2. Find **Hypervisor Settings → Hypervisor Override** and set it to **Enabled** (wording from a forum report; the real menu may differ slightly).
3. Save, boot Linux.
4. To roll back later, set it back to the previous value.

Tell the human about the side effects in the Radxa overlay (`qcs6490-radxa-kvm.dtso`): GPU zap shader disabled (Linux does it in EL2), `qcom,broken-reset` on the ADSP/CDSP remoteprocs, and an SCM shm-bridge change. Other features may behave differently.

Agent verification after the reboot:

```bash
# board$
uptime
dmesg | grep -E "All CPU\(s\) started at EL"        # must say EL2
find /boot /usr/lib -name '*radxa-kvm*' 2>/dev/null   # the overlay should exist on disk
find /proc/device-tree -type d -name 'video-firmware' # present => Iris takes the non-TZ boot path
sudo bash ~/linux-qcom-docs/docs/q6a-video/tools/q6a-video-diag.sh 2>&1 | sed -n '/2. Boot mode/,/3. Which/p'
```
If it still says EL1: stop; tell the human (setting not saved, or overlay not applied). Do not edit boot files yourself.

## G§4 Driver / DT sanity (T0)

```bash
# board$
N=$(find /proc/device-tree -type d -name 'video-codec@*' | head -1); echo "$N"
tr -d '\0' < $N/compatible; echo                     # qcom,sc7280-venus
tr -d '\0' < $N/firmware-name 2>/dev/null; echo      # qcom/vpu/vpu20_p1_gen2_s6.mbn on the Radxa DTS (blank elsewhere)
[ -e $N/dma-coherent ] && echo dma-coherent=yes || echo dma-coherent=NO
xxd -p $N/iommus                                     # EL1: one entry (0x2180 mask 0x20); EL2: also 0x2184
lsmod | grep -E 'iris|venus'
ls -l /sys/bus/platform/devices/*video-codec*/driver
v4l2-ctl --list-devices
```
Expected on 7.0.11: driver `qcom-iris`; devices "Iris Decoder" and "Iris Encoder". (Venus would say "Qualcomm Venus video encoder".)

## G§5 Safe scaling/format probe (T1)

Negotiation only — no encode session. Opening the node loads and boots the firmware, so do this **after** G§2 (and G§3 if going EL2).

```bash
# board$
cd ~/linux-qcom-docs/docs/q6a-video/tools
gcc -O2 -Wall -o /tmp/v4l2-enc-probe v4l2-enc-probe.c
for d in /dev/video*; do v4l2-ctl -d $d --info 2>/dev/null | grep -qi encoder && echo "$d"; done   # find the encoder node
/tmp/v4l2-enc-probe /dev/videoN 1920 1080 1280 720 2>&1 | tee ~/probe.txt
dmesg | tail -n 40
```
Read the `RESULT:` line:
* `FORCED BACK` → the driver will not keep a smaller coded size → no encoder-side scaling through V4L2. Use a pre-scaler (G§10.3).
* `KEPT` → V4L2 *accepts* it. That alone does **not** prove the hardware scales; do G§10.2.
Also record the printed format lists and the H.264/HEVC frame-size limits.

## G§6 Encode matrix with corruption check (T1 in EL2 / **T2 in EL1**)

Software baseline first (safe, proves the harness):

```bash
cd ~/linux-qcom-docs/docs/q6a-video/tools
ENC=libx264 W=1280 H=720 N=60 ./encode-validate.sh        # expect PASS: measured avg 47.8 / worst 46.3 dB (1080p30 8M: 48.1 / 46.3; 1080p25 8M: 50.2 / 48.3)
```

Hardware matrix (stop on the first reset; ≤ 3 attempts per row):

```bash
cd ~/linux-qcom-docs/docs/q6a-video/tools
for cfg in \
  "h264_v4l2m2m 1280 720 30"  "hevc_v4l2m2m 1280 720 30" \
  "h264_v4l2m2m 1920 1080 30" "hevc_v4l2m2m 1920 1080 30" \
  "h264_v4l2m2m 1920 1080 25" "h264_v4l2m2m 1920 1080 29" "hevc_v4l2m2m 1920 1080 25"; do
  set -- $cfg
  sync; sleep 3
  ENC=$1 W=$2 H=$3 FPS=$4 ./encode-validate.sh 2>&1 | tee -a ~/matrix.txt
  rc=${PIPESTATUS[0]}
  echo "RESULT enc=$1 ${2}x${3}@$4 exit=$rc" | tee -a ~/matrix.txt
  dmesg | grep -iE 'arm-smmu|system error|SFR message|firmware download|Unhandled' | tail -5 | tee -a ~/matrix.txt
done
# 4K30 — data only, a failure here is a finding not a blocker
ENC=h264_v4l2m2m W=3840 H=2160 FPS=30 N=30 BITRATE=20M TIMEOUT=120 ./encode-validate.sh 2>&1 | tee -a ~/matrix.txt
```

Exit codes: `0` PASS · `1` FAIL (wrong size, too few frames, hang, or worst-frame PSNR < 30 dB) · `2` encoder could not run. If a hardware run lands between ~30 dB and the x264 numbers, run the same row with `ENC=libx264` and compare before calling it a bug (hardware rate control scores lower than x264).

Client checks (real-world apps):

```bash
# GStreamer (NV12 in; note 25 fps — this used to fail with "internal data stream error")
gst-launch-1.0 -e videotestsrc num-buffers=300 ! video/x-raw,format=NV12,width=1920,height=1080,framerate=25/1 \
  ! v4l2h264enc ! h264parse ! mp4mux ! filesink location=/tmp/gst-h264.mp4
gst-launch-1.0 -e videotestsrc num-buffers=300 ! video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 \
  ! v4l2h265enc ! h265parse ! mp4mux ! filesink location=/tmp/gst-h265.mp4
ffprobe -v error -show_entries stream=codec_name,width,height,nb_frames -of default=nw=1 /tmp/gst-h264.mp4 /tmp/gst-h265.mp4

# ffmpeg transcode of a real file (software decode here; measures encode only)
ffmpeg -hide_banner -i input.mp4 -c:v h264_v4l2m2m -b:v 8M -c:a copy /tmp/out.mp4
```

## G§7 Evidence after a reset or hang (T0)

```bash
# board$ after it comes back
uptime; last -x | head -5
journalctl -k -b -1 --no-pager | tail -n 200        # previous boot's kernel log (needs the persistent journal from G§0)
ls -l /sys/fs/pstore/ 2>/dev/null; sudo cat /sys/fs/pstore/* 2>/dev/null | tail -n 100
journalctl -b -1 --no-pager | grep -iE 'arm-smmu|iris|venus|sid|fault' | tail -n 50
# host$ — the last lines of the off-board stream are often the most useful
tail -n 80 board-dmesg-*.log
```
Report: the last 50 kernel lines before the reset, whether `arm-smmu` / a stream ID (look for `0x2184`) appears, the boot mode, the firmware fingerprint, and which exact command was running.

## G§8 Special procedures (T2/T3)

**8a — one controlled hardware encode in TrustZone mode (T2).** Only with explicit human approval, after G§2 verified, with the log stream running:
```bash
sync; cd ~/linux-qcom-docs/docs/q6a-video/tools
ENC=h264_v4l2m2m W=1280 H=720 N=30 TIMEOUT=60 ./encode-validate.sh
```
One attempt. If the board resets: G§7, report, stop. If it passes: record it (this converts the forum report from **S** to a data point), then continue the matrix.

**8b — Venus route (T3, needs a kernel rebuild).** Only if Iris encode fails in EL2 after fixes.
1. Append `CONFIG_VIDEO_QCOM_IRIS=n` to the config fragment (patch `0002-feat-Radxa-custom-kernel-config.patch`, file `src/arch/arm64/configs/radxa_custom.config`). Venus's `qcom,sc7280-venus` entry reappears automatically (`venus/core.c:1138`).
2. In the Q6A DTS (`qcs6490-radxa-dragon-q6a.dts`, `&venus {}`), remove `firmware-name = …` so Venus uses `qcom/vpu-2.0/venus.mbn` (a symlink to `../vpu/vpu20_p1.mbn`); ensure that file is the current Gen1 (G§2). Do the same for Q6B / CM-Q64 if used.
3. Rebuild per `README.md` (`make deb`), install with human approval.
Untested; not Radxa-supported.

## G§9 Stability (T2 in EL1, T1 in EL2)

```bash
# 20 open/encode/close cycles — looks for teardown IOMMU faults (Radxa PR #593 mentions them)
mkdir -p /tmp/stab; cd ~/linux-qcom-docs/docs/q6a-video/tools
for i in $(seq 1 20); do
  sync; ENC=h264_v4l2m2m W=1280 H=720 N=30 TIMEOUT=60 OUT=/tmp/stab ./encode-validate.sh >/tmp/stab/run_$i.log 2>&1
  echo "cycle $i exit=$?"; sleep 2
done | tee ~/stability.txt
dmesg | grep -ciE 'arm-smmu.*fault|Unhandled context fault|system error'     # must be 0
```
Sustained run (no PSNR — encode throughput and stability only):
```bash
ffmpeg -hide_banner -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 300 -c:v h264_v4l2m2m -b:v 8M -f null - 2>&1 | tail -n 5
# want: no errors, final "speed=" >= ~1.0x. Then re-check the dmesg grep above.
```

## G§10 Scaling

**10.1 — Probe verdict.** From G§5.

**10.2 — Prove (or disprove) encoder-side scaling with a streamed test (optional work item).** `v4l2-ctl` cannot be used: it applies the CAPTURE format *before* the OUTPUT format, and Iris resets CAPTURE to the raw size when OUTPUT is set. Write a small C program (extend `v4l2-enc-probe.c`) that:
1. `S_FMT(OUTPUT NV12 1920×1080)` **then** `S_FMT(CAPTURE H264 1280×720)`;
2. `REQBUFS` + `mmap` both queues (MMAP, 1 plane each), queue all CAPTURE buffers;
3. fill NV12 frames (e.g. the pattern from `ffmpeg -f lavfi -i testsrc2=size=1920x1080 -pix_fmt nv12 -f rawvideo`), `QBUF` to OUTPUT, `STREAMON` both;
4. `DQBUF` coded buffers and append to a file; after ~60 frames send `VIDIOC_ENCODER_CMD(V4L2_ENC_CMD_STOP)` and wait for the buffer flagged `V4L2_BUF_FLAG_LAST`.
Then judge the result:
```bash
ffprobe -v error -show_entries stream=width,height -of default=nw=1 out.h264      # 1280x720 => the VPU produced a scaled stream
# fidelity vs a software-scaled reference (frames paired by index, as in encode-validate.sh)
ffmpeg -i out.h264 -f lavfi -i "testsrc2=size=1920x1080:rate=30,scale=1280:720,format=yuv420p" -frames:v 60 \
  -lavfi "[0:v]settb=1/30,setpts=N,format=yuv420p[a];[1:v]settb=1/30,setpts=N,format=yuv420p[b];[a][b]psnr" -f null -
```
If the stream comes out at 1920×1080 or errors, hardware scaling during encode is not available → use 10.3. **Run only in EL2 (or with T2 approval).**

**10.3 — Working pre-scale → VPU-encode pipelines (use these regardless).**
```bash
# ffmpeg: CPU scale, VPU encode
ffmpeg -i in.mp4 -vf "scale=1280:720,format=nv12" -c:v h264_v4l2m2m -b:v 4M out.mp4
# GStreamer: CPU scale, VPU encode
gst-launch-1.0 -e filesrc location=in.mp4 ! decodebin ! videoscale ! video/x-raw,width=1280,height=720 ! videoconvert \
  ! video/x-raw,format=NV12 ! v4l2h264enc ! h264parse ! mp4mux ! filesink location=out.mp4
```
Validate by encoding at the target size with `encode-validate.sh` (`W=1280 H=720`).
GPU scaling (`scale_vulkan` / `libplacebo` through Mesa Turnip) is **untested on this board**; only try it if CPU load is a problem, and treat it as a separate experiment.

**10.4 — Decoder-side downscale.** The firmware can downscale in the decoder (VPSS, ratios 1.25/1.5/2/3, no upscale) but neither driver exposes it; it would need a new kernel patch (`S_FMT(CAPTURE)`/OUTPUT2 size plumbing). Out of scope unless the human asks.

## G§11 Symptom table (exact strings from the 7.0.11 source)

| What you see (kernel log / behaviour) | Meaning | Action |
|---|---|---|
| `Direct firmware load for qcom/vpu/vpu20_p1_gen2_s6.mbn failed with error -2` then `firmware download failed -2` | Gen2 blob not found (DT pins the name, so no Gen1 fallback) | G§2 |
| `firmware download failed -22` | Firmware larger than the carveout (Gen2 needs exactly 0x700000) or bad MDT | Check diag §4 carveout; Radxa DTS has 7 MiB; stock 5 MiB boards cannot load Gen2 |
| `error -22 initializing firmware …` / `auth and reset failed: …` (from `qcom_mdt_load` / Iris) | TZ rejected the image (MBNv7 / wrong signature) | G§2, verify sha256 |
| `no hash segment found in …` / `error … reading firmware … metadata` | Not a valid MDT/MBN file (truncated download, wrong file) | re-download, verify hash |
| `Gen1 FW detected in <name>` | Driver classified the file as Gen1. Normal for `vpu20_p1.mbn`; **never** valid for the real `_s6` Gen2 blob | verify hash |
| `core init failed` (on open) | Core could not start; look at the lines just before | read the preceding `iris` lines |
| `invalid setting for uc_region` / `error booting up iris firmware` | Firmware did not come up | check boot mode, memory-region, firmware; capture log |
| `received watchdog interrupt` / `cpu watchdog error received` | Firmware watchdog fired | record SFR text; stop that test |
| `SFR message from FW: …` | The firmware's own fatal text | **quote verbatim** in the report |
| `received system error of type …` / `sys error (type: …)` | Firmware reported a fatal error | record, then G§7 |
| `no valid instance(pkt session_id:…, pkt:…)` | Firmware message for an unknown session (seen with the VP9 crash on 6.18 Venus) | record; unrelated to encode if on 6.18 |
| `arm-smmu … Unhandled context fault … sid` / `Unexpected global fault` | IOMMU fault; note the SID. `0x2184` ⇒ secure-stream mismatch (TZ vs EL2) | note SID; EL2 expected to avoid; report |
| Board resets with no error | The reported TrustZone-mode encode hazard | EL1: do not repeat → G§3; G§7 |
| Output plays but looks wrong / PSNR < 30 dB | Corruption | confirm `dma-coherent` present (G§4); compare with `ENC=libx264`; check firmware generation |
| GStreamer: `internal data stream error` at 25/29 fps | Kernel lacks the frame-interval fix | wrong kernel build (needs Radxa `38befa2de`) |
| Hang until timeout (exit 124) | Encoder stalled | collect SFR + dmesg; Plan Phase 6 candidates |
| "No device": no `/dev/video*` or no driver bound | Driver not loaded / probe failed / node disabled | `lsmod`, `dmesg \| grep -iE 'iris\|venus\|probe'`; check DT `status`; report |

## G§12 Results file template (return this)

```markdown
# Q6A video results — <date> — <board serial/hostname>
Kernel: <uname -r>   Distro: <pretty name>   Boot mode: <EL1|EL2>   Driver: <iris|venus>
Firmware: gen2 <present/sha256/MBN>, gen1 <fingerprint>   dma-coherent: <yes/no>   carveout: <0x…>

| Criterion | Result (PASS/FAIL/NOT RUN) | Evidence (file / exact log line) |
|---|---|---|
| S1 nodes open, no errors | | |
| S2 H264 720p / 1080p corruption check | | |
| S2 HEVC 720p / 1080p corruption check | | |
| S3 25 fps and 29 fps | | |
| S4 4K30 (data only) | | |
| S5 20 open/encode/close cycles, 0 faults | | |
| S6 5-min 1080p30 | | |
| S7 scaling: probe verdict | KEPT / FORCED BACK | probe.txt |
| S7 scaling: streamed test or pre-scale pipeline | | |
| S8 this file | | |

## Resets / hangs
<time, command, log tail, boot mode>

## Deviations from the plan
<anything you did that was not in the plan, and why>

## Unresolved / suggested next step
<…>

Attachments: diag-before.txt, diag-after.txt, probe.txt, matrix.txt, stability.txt, board-dmesg-*.log, journalctl -b -1 (if a reset happened)
```
