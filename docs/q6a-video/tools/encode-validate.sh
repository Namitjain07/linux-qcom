#!/usr/bin/env bash
# encode-validate.sh - objective check that a hardware encoder produces CORRECT output.
#
# Why: Qualcomm's own diagnosis of the SC7280 video bugs is "wrong input data / corrupted
# output", which can look fine for a few frames and still be garbage. This encodes a
# deterministic test pattern, decodes it again, and compares it with the same pattern
# (PSNR) - a corrupted encode scores very low.
#
# Usage:
#   ./encode-validate.sh                         # h264_v4l2m2m, 1280x720@30, 60 frames
#   ENC=hevc_v4l2m2m W=1920 H=1080 FPS=25 ./encode-validate.sh
#   ENC=libx264 ./encode-validate.sh             # software sanity check of this script itself
#
# Calibration (software libx264, 640x360, this synthetic source; measured, not assumed):
#   healthy:  1 Mbit/s avg 44 / worst 42 dB, 4 Mbit/s avg 58 / worst 54 dB, 20 Mbit/s avg 70 dB
#   720p30 4M avg 47.8 / worst 46.3 dB; 1080p30 8M avg 48.1 / worst 46.3; 1080p25 8M avg 50.2 / worst 48.3
#   corrupt:  a stream with 3 trashed byte ranges -> avg 28 / worst 24 dB
# So THRESH=30 (worst frame) separates "garbage" from "ordinary quality loss". Hardware encoders
# with their own rate control will score lower than x264; if a hardware run lands between ~30 and
# the x264 numbers, compare against ENC=libx264 at the same W/H/BITRATE before calling it a bug.
#
# Exit codes: 0 = PASS (min PSNR >= THRESH), 1 = FAIL (corrupt / wrong size / no output),
#             2 = encoder could not run.  A hung hardware encoder is killed after TIMEOUT s.
# WARNING: on a Q6A in default TrustZone mode, hardware encoding has been reported to reset
# the whole board. Do `sync` first and read GUIDE.md section "Safety" before running with
# a hardware encoder.

ENC="${ENC:-h264_v4l2m2m}"; W="${W:-1280}"; H="${H:-720}"; FPS="${FPS:-30}"; N="${N:-60}"
BITRATE="${BITRATE:-4M}"; THRESH="${THRESH:-30}"; TIMEOUT="${TIMEOUT:-90}"
OUT="${OUT:-/tmp/q6a-validate}"; mkdir -p "$OUT"
# Native yuv420p source so the encoded path and the reference path see identical pixels.
SRC="testsrc2=size=${W}x${H}:rate=${FPS},format=yuv420p"
STREAM="$OUT/${ENC}_${W}x${H}_${FPS}.mkv"
rm -f "$STREAM"

sync
echo "== encode: $ENC ${W}x${H}@${FPS} x${N} frames -> $STREAM"
timeout "$TIMEOUT" ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -frames:v "$N" \
    -pix_fmt nv12 -c:v "$ENC" -b:v "$BITRATE" "$STREAM"
rc=$?
sync
if [ $rc -eq 124 ]; then echo "FAIL: encoder hung (timeout ${TIMEOUT}s) - check dmesg for 'system error' / SFR"; exit 1; fi
if [ $rc -ne 0 ] || [ ! -s "$STREAM" ]; then echo "ERROR: encoder could not run (ffmpeg exit $rc)"; exit 2; fi

# 1) stream properties
probe=$(ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=width,height,nb_read_frames -of default=nw=1 "$STREAM")
gw=$(echo "$probe" | sed -n 's/^width=//p'); gh=$(echo "$probe" | sed -n 's/^height=//p')
frames=$(echo "$probe" | sed -n 's/^nb_read_frames=//p')
echo "== decoded stream: ${gw}x${gh}, ${frames} frames (expected ${W}x${H}, ${N})"
[ "$gw" = "$W" ] && [ "$gh" = "$H" ] || { echo "FAIL: wrong resolution"; exit 1; }
[ "${frames:-0}" -ge $((N - 2)) ] || { echo "FAIL: frame count too low (dropped frames / early EOS)"; exit 1; }

# 2) PSNR against the identical regenerated source (software decode of the stream)
# Frames are paired by INDEX (settb+setpts=N). Pairing by timestamp is wrong: Matroska rounds
# PTS to 1 ms, which silently mispaired frames and capped PSNR at ~25-30 dB even for a perfect encode.
psnr=$(ffmpeg -hide_banner -nostats -err_detect ignore_err -i "$STREAM" -f lavfi -i "$SRC" -frames:v "$N" \
    -lavfi "[0:v]settb=1/${FPS},setpts=N,format=yuv420p[a];[1:v]settb=1/${FPS},setpts=N,format=yuv420p[b];[a][b]psnr=stats_file=$OUT/psnr.log" \
    -f null - 2>&1 | grep -o 'average:[0-9.inf]*' | tail -1 | cut -d: -f2)
min=$(awk -F'psnr_avg:' 'NF>1{split($2,a," "); v=a[1]; if(min==""||v+0<min+0)min=v} END{print min}' "$OUT/psnr.log")
echo "== PSNR avg=${psnr:-n/a} dB   worst frame=${min:-n/a} dB   (threshold ${THRESH} dB)"
if [ -z "$min" ]; then echo "FAIL: PSNR could not be computed"; exit 1; fi
awk -v m="$min" -v t="$THRESH" 'BEGIN{exit !(m+0 >= t+0)}' && { echo "PASS"; exit 0; }
echo "FAIL: corrupted output (worst frame below threshold) - see GUIDE.md 'Symptom table'"; exit 1
