#!/usr/bin/env bash
# q6a-video-diag.sh v2 - diagnostics for hardware video ENCODE/scale bring-up on
# QCS6490/QCM6490 (SC7280 "kodiak") boards: Radxa Dragon Q6A / Q6B / CM-Q64, RB3 Gen2, etc.
#
#   sudo ./q6a-video-diag.sh 2>&1 | tee q6a-video-diag.txt            # read-only report
#   sudo ENCODE_TEST=1 ./q6a-video-diag.sh 2>&1 | tee q6a-encode.txt   # + run encode tests
#
# Everything is read-only EXCEPT the opt-in ENCODE_TEST=1 section, which runs the hardware
# encoder. A community report says encoding on the Q6A can hard-reboot the board in the
# default (TrustZone) boot mode, so that section is OFF by default, runs `sync` first and
# writes its log to disk before each step.  Env: FW_DIR (default /lib/firmware), DT, OUT.

FW_DIR="${FW_DIR:-/lib/firmware}"; DT="${DT:-/proc/device-tree}"; OUT="${OUT:-/tmp/q6a-diag}"
mkdir -p "$OUT" 2>/dev/null
hr() { printf '\n==================== %s ====================\n' "$*"; }

hr "1. System"
uname -a
echo "model: $(tr -d '\0' < "$DT/model" 2>/dev/null)"
echo "compatible: $(tr -d '\0' < "$DT/compatible" 2>/dev/null | tr '\n' ' ')"
. /etc/os-release 2>/dev/null && echo "distro: $PRETTY_NAME"
for p in linux-firmware firmware-qcom-soc firmware-qcom-hlosfw linux-firmware-qualcomm-misc radxa-firmware-qcs6490; do
    v=$(dpkg-query -W -f='${Version}' "$p" 2>/dev/null) && echo "pkg $p = $v"
done
echo "UEFI/board firmware: $(cat /sys/class/dmi/id/bios_version 2>/dev/null || echo n/a)"

hr "2. Boot mode: TrustZone (default) or EL2/hypervisor-override (KVM overlay)?"
if dmesg 2>/dev/null | grep -q "All CPU(s) started at EL2"; then
    echo "=> EL2: the kernel booted at EL2 (hypervisor override ON). Linux manages the video firmware SMMU."
elif dmesg 2>/dev/null | grep -q "All CPU(s) started at EL1"; then
    echo "=> EL1: default TrustZone mode (TZ authenticates + resets the video firmware)."
else
    echo "=> could not tell (dmesg rotated or unreadable); check: dmesg | grep 'started at EL'"
fi
[ -e "$DT/chosen/radxa,enable-kvm" ] && echo "   (chosen/radxa,enable-kvm is set by the Radxa KVM overlay)"
dmesg 2>/dev/null | grep -i -E "started at EL|kvm.*(VHE|Hyp) mode" | head -4

hr "3. Which driver is bound (Venus or Iris)?"
lsmod | grep -E '^(qcom_iris|iris|venus_core|venus_dec|venus_enc)' || echo "(no iris/venus module loaded)"
for d in /sys/bus/platform/devices/*video-codec*; do
    [ -e "$d" ] || continue
    echo "device : $d"
    echo "driver : $(readlink -f "$d/driver" 2>/dev/null || echo '** NONE BOUND **')"
done
(grep -E 'VIDEO_QCOM_(IRIS|VENUS)=' "/boot/config-$(uname -r)" 2>/dev/null || zgrep -E 'VIDEO_QCOM_(IRIS|VENUS)=' /proc/config.gz 2>/dev/null) || echo "(kernel config not readable)"

hr "4. Device-tree facts that decide whether encode can work"
N=$(find "$DT" -type d -name 'video-codec@*' 2>/dev/null | head -1)
if [ -n "$N" ]; then
    echo "node            : $N"
    echo "firmware-name   : $(tr -d '\0' < "$N/firmware-name" 2>/dev/null)   (blank => driver default)"
    if [ -e "$N/dma-coherent" ]; then echo "dma-coherent    : PRESENT  (needed: Qualcomm fix for faults/corruption on SC7280 video)";
    else echo "dma-coherent    : ** MISSING **  -> input-data faults / corrupt output at higher resolutions (Qualcomm kernel-topics PR #1641)"; fi
    [ -d "$N/video-firmware" ] && echo "video-firmware  : present (non-TZ boot path)" || echo "video-firmware  : absent (TZ boot path)"
    echo "iommus (hex)    : $(xxd -p "$N/iommus" 2>/dev/null | tr -d '\n')   [..] 0x2180 mask 0x20 = non-secure; an entry for 0x2184 is only present in EL2/Chrome DTs"
else echo "no video-codec node found"; fi
for r in "$DT"/reserved-memory/video*; do [ -e "$r/reg" ] && echo "carveout $(basename "$r"): reg=$(xxd -p "$r/reg" | tr -d '\n')  (Gen2 firmware needs 0x700000, Gen1 0x500000)"; done

hr "5. Firmware identification (fingerprints from linux-firmware history)"
python3 - "$FW_DIR" <<'PY'
import os, struct, sys, lzma, subprocess, re, hashlib
fwdir = sys.argv[1]
# embedded QC_IMAGE_VERSION_STRING hash prefix -> meaning (verified against linux-firmware commits)
KNOWN = {
 "video-firmware.1.0-df9cb37c": "Gen1 2021-05 (c7b11ed1) - very old, known encoder bugs",
 "video-firmware.1.0-8fd4ba98": "Gen1 2022-10..2024-08 (05df8e65/36db650d) - PRE-FIX: lacks Nov-2024 encoder fixes (EOS handling, AVC High level, QP range)",
 "video-firmware.1.0-ed457c18": "Gen1 2024-11-13 (aeede7af) VIDEO.VPU.2.0-00055-PROD-1 - CURRENT Gen1, has the encoder fixes",
 "video-firmware.2.4.2-d7a3d538": "Gen2 2025-04 original 'vpu20_p1_gen2.mbn' - MBNv7, SC7280 TZ REJECTS it",
 "vfw-3.4:rel0059": "Gen2 2026-02-20 (30a139cb) 'vpu20_p1_gen2_s6.mbn' - MBNv6, correct for SC7280",
}
names = ["qcom/vpu/vpu20_p1_gen2_s6.mbn","qcom/vpu/vpu20_p1_gen2.mbn","qcom/vpu/vpu20_p1.mbn",
         "qcom/vpu-2.0/venus.mbn","qcom/qcm6490/venus.mbn"]
def read_any(path):
    for suf, op in (("", lambda p: open(p,'rb').read()),
                    (".zst", lambda p: subprocess.run(["zstd","-dc",p],capture_output=True,check=True).stdout),
                    (".xz",  lambda p: lzma.open(p).read())):
        if os.path.lexists(path+suf):
            try: return path+suf, op(path+suf)
            except Exception as e: return path+suf, e
    return None, None
def analyse(d):
    phoff = struct.unpack_from('<I', d, 0x1c)[0]; phes, phn = struct.unpack_from('<HH', d, 0x2a)
    lo, hi, hv = None, 0, None
    for i in range(phn):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<8I', d, phoff+i*phes)
        if ((fl >> 24) & 7) == 2 and fsz >= 8: hv = struct.unpack_from('<I', d, off+4)[0]
        elif t == 1 and msz: lo = pa if lo is None else min(lo, pa); hi = max(hi, (pa+msz+0xfff)&~0xfff)
    m = re.search(rb'QC_IMAGE_VERSION_STRING=([ -~]*)', d)
    return hv, hi-(lo or 0), (m.group(1).decode() if m else None)
for n in names:
    p, d = read_any(os.path.join(fwdir, n))
    if p is None: print(f"MISSING  {n}"); continue
    if isinstance(d, Exception): print(f"UNREADABLE {n}: {d}"); continue
    try: hv, sz, ver = analyse(d)
    except Exception as e: print(f"{n}: not an MDT/ELF image ({e})"); continue
    meaning = next((v for k, v in KNOWN.items() if ver and ver.startswith(k)), "unrecognised build")
    print(f"PRESENT  {n} -> {os.path.realpath(p)}")
    print(f"         version : {ver}")
    print(f"         meaning : {meaning}")
    print(f"         MBN hdr v{hv}{'  ** SC7280 needs v6 **' if hv and hv>=7 else ''}; mdt_size={sz:#x} ({sz/1048576:.2f} MiB); sha256={hashlib.sha256(d).hexdigest()[:16]}")
PY

hr "6. Kernel log: video / firmware / SMMU faults (before any test)"
dmesg 2>/dev/null | grep -i -E 'iris|venus|vpu|video-codec|aa00000|mdt|Direct firmware|qcom_scm|SFR|arm-smmu.*(fault|sid)|Unhandled context fault|global fault' | tail -n 60

if [ -n "$ENCODE_TEST" ]; then
    hr "7. ENCODE / SCALE TEST (opt-in). Saving state to disk first."
    sync; dmesg > "$OUT/dmesg.before" 2>/dev/null; echo "(dmesg saved to $OUT/dmesg.before; if the board reboots, send that file + 'journalctl -b -1 -k')"
    for dev in /dev/video*; do
        info=$(v4l2-ctl -d "$dev" --info 2>/dev/null | tr '\n' ' ')
        case "$info" in *iris*|*venus*|*Iris*|*Venus*) echo "$dev : $(echo "$info" | sed -n 's/.*Card type *: *\([^ ]*[^:]*\) *Bus info.*/\1/p')"; v4l2-ctl -d "$dev" --list-formats-out 2>&1 | grep -E "\[[0-9]\]" ;; esac
    done
    # Safest check first: negotiation only (no encode session). Can the coded size differ from the raw size?
    PROBE_SRC="$(dirname "$0")/v4l2-enc-probe.c"
    if [ -f "$PROBE_SRC" ] && command -v gcc >/dev/null && gcc -O2 -o "$OUT/v4l2-enc-probe" "$PROBE_SRC" 2>/dev/null; then
        for dev in /dev/video*; do
            v4l2-ctl -d "$dev" --info 2>/dev/null | grep -qi "encoder" || continue
            echo; echo "---- scale-negotiation probe on $dev (no encode is started)"; sync
            timeout 30 "$OUT/v4l2-enc-probe" "$dev" 2>&1
        done
    else
        echo "(scale probe skipped: need gcc and v4l2-enc-probe.c next to this script)"
    fi
    run() { echo; echo "---- $*"; sync; timeout 60 "$@" 2>&1 | tail -n 8; echo "exit=${PIPESTATUS[0]} (124 = timed out)"; dmesg | tail -n 15 > "$OUT/dmesg.last"; sync; }
    if command -v gst-launch-1.0 >/dev/null; then
        run gst-launch-1.0 -e videotestsrc num-buffers=120 ! video/x-raw,width=1280,height=720,framerate=30/1 ! v4l2h264enc ! h264parse ! fakesink
        run gst-launch-1.0 -e videotestsrc num-buffers=120 ! video/x-raw,width=1280,height=720,framerate=30/1 ! v4l2h265enc ! h265parse ! fakesink
        # "scale": VPU cannot scale while encoding, so scale on CPU then encode on VPU
        run gst-launch-1.0 -e videotestsrc num-buffers=120 ! video/x-raw,width=1920,height=1080,framerate=30/1 ! videoscale ! video/x-raw,width=1280,height=720 ! v4l2h264enc ! h264parse ! fakesink
        run gst-launch-1.0 -e videotestsrc num-buffers=120 ! video/x-raw,width=1920,height=1080,framerate=25/1 ! v4l2h264enc ! h264parse ! fakesink
    fi
    if command -v ffmpeg >/dev/null; then
        run ffmpeg -hide_banner -loglevel warning -f lavfi -i testsrc2=size=1280x720:rate=30 -frames:v 120 -c:v h264_v4l2m2m -b:v 4M -f null -
        run ffmpeg -hide_banner -loglevel warning -f lavfi -i testsrc2=size=1920x1080:rate=30 -frames:v 120 -vf scale=1280:720 -c:v h264_v4l2m2m -b:v 4M -f null -
    fi
    hr "8. Kernel log AFTER encode tests"
    dmesg 2>/dev/null | grep -i -E 'iris|venus|vpu|video-codec|aa00000|SFR|firmware download|auth and reset|arm-smmu.*(fault|sid)|Unhandled context fault|global fault|system error|Gen1 FW' | tail -n 80
else
    hr "7. Encode test skipped"; echo "Run again with ENCODE_TEST=1 (see header warning) to exercise the encoder."
fi
echo; echo "Done. Please send the full output."
