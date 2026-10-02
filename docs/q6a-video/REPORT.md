# Research report — hardware video encode + scale on QCS6490 / Radxa Dragon Q6A

Status: research complete, **no on-device test results yet**. Everything below comes from reading source, git history, firmware binaries and PR text. Evidence grades used throughout:

| Grade | Meaning |
|---|---|
| **P** | Primary: code, commit or binary I inspected myself (re-checkable with the commands in `EVIDENCE.md`) |
| **Q** | Primary quote from a PR page, confirmed verbatim on a second fetch |
| **S** | Secondhand: text of a page I could not open (search-result snippet) |
| **U** | Unproven / inference |

## 1. Executive summary

**Goal:** use the VPU of the QCS6490 (Radxa Dragon Q6A) for H.264/HEVC **encoding** and **scaling**.

**Verdict**
1. The VPU can encode: both the Gen1 and Gen2 SC7280 firmware images contain full H.264 and HEVC encoders (**P**).
2. The best-supported way to use it is the kernel this repo builds — **Radxa `linux-7.0.11` with the Iris driver, the Gen2 firmware `vpu20_p1_gen2_s6.mbn`, and Hypervisor Override (EL2) enabled in UEFI**. Radxa's own merged PRs target exactly this for QCS6490 (**Q**), and EL2 sidesteps a documented secure-stream-ID crash class that affects the encode path in TrustZone mode (**P** mechanism, **S** for the reported reboot).
3. **Scaling:** the VPU has a hardware scaler (VPSS) but it is only proven on the *decode* side and neither Venus nor Iris exposes it to userspace (**P**). Encoder-side scaling through Iris is *plausible* (separate raw / crop / coded sizes reach the firmware, and Radxa claims it works) but **unproven (U)** — a safe probe is provided. Until proven, scale on CPU/GPU and use the VPU only to encode.
4. The two things most likely to be wrong on a system that "doesn't work": **(a)** the Gen2 firmware blob is missing or is the old MBNv7 blob, **(b)** the board is in default TrustZone mode and the encoder triggers a board reset.

**Confidence:** moderate. The mechanism and the configuration Radxa tests are well evidenced; whether *your* board's failure is (a), (b) or something else needs the on-device data this bundle collects.

## 2. Scope and method
- Read the exact kernel source this repo builds (`radxa/kernel` @ `a50eb5b71984`, 7.0.11), Radxa's `linux-6.18.2`, and `qualcomm-linux/kernel` main (7.3-rc5).
- Read git history of Venus, Iris, DTS, firmware loader in all three; fetched Qualcomm and Radxa PR heads over git (`refs/pull/N/head`).
- Downloaded and disassembled-by-strings the real firmware images (current and historical) from linux-firmware.
- Read public PR/issue pages. **Not reachable** from the research environment (network policy): forum.radxa.com, docs.radxa.com, lore.kernel.org, patchwork.kernel.org, patches.linaro.org, lkml.iu.edu, ratatoskr.run, patchew.org, mail-archive.com, mysupport.qualcomm.com. Items that depend on them are graded **S**.
- Limits: no board access, nothing was executed on hardware.

## 3. Background

### 3.1 Hardware and drivers
QCS6490/QCM6490 is the "SC7280 / kodiak" family. Its video block (VPU 2.0, "Iris2.1") is driven by firmware running on an embedded Xtensa CPU, talking to the kernel over **HFI** queues. Two kernel drivers exist for the same compatible string `qcom,sc7280-venus`:

| | Venus (old) | Iris (new) |
|---|---|---|
| HFI | Gen1 only | Gen1 **and** Gen2 |
| Firmware for SC7280 | `qcom/vpu-2.0/venus.mbn` (symlink to `qcom/vpu/vpu20_p1.mbn`) | Gen2 `qcom/vpu/vpu20_p1_gen2_s6.mbn` preferred, Gen1 `qcom/vpu/vpu20_p1.mbn` fallback |
| Upstream status | maintenance | the default for SC7280 since the "flip the switch" change (Dmitry Baryshkov, 55ee57c12 in Radxa 7.0.11) |

### 3.2 Which driver your kernel uses (**P**)
- **7.0.11 (this repo):** Venus's SC7280 and SM8250 match entries are compiled out whenever `CONFIG_VIDEO_QCOM_IRIS` is `m` or `y` (`drivers/media/platform/qcom/venus/core.c:1138-1141`, `#if (!IS_ENABLED(CONFIG_VIDEO_QCOM_IRIS))`). The build config has `CONFIG_VIDEO_QCOM_IRIS=m`. So **Iris is the only SC7280 driver**; there is no race.
- **6.18.2 (Radxa branch):** Venus, Gen1 firmware, Q6A DTS has only `&venus { status = "okay"; }`.

### 3.3 TrustZone (default) vs EL2 (Hypervisor Override)
- Default: the kernel runs at EL1; TrustZone (PAS id 9) authenticates the video firmware and owns resets and the secure SMMU streams.
- EL2: with Hypervisor Override enabled, Radxa's UEFI applies the `qcs6490-radxa-kvm.dtso` overlay. Iris then detects a `video-firmware` subnode and boots the firmware itself ("non-TZ" path: Linux maps the firmware in its own IOMMU domain and releases the Xtensa CPU via registers). The overlay also lists SMMU stream `0x2184` for the venus device and `0x21a2` for the firmware (**P**: `arch/arm64/boot/dts/qcom/qcs6490-radxa-kvm.dtso`).
- Upstream's long-term plan for SC7280 Iris is PAS/TZ only; non-TZ is "expected to use a different ABI" (Dmitry Baryshkov, `01022af2d21`, `ab4e6a16f97`). **Radxa's EL2 path is carried, not upstream (P).**

### 3.4 Firmware generations (**P**, binaries inspected)
| File | Embedded `QC_IMAGE_VERSION_STRING` | MBN header | Needs carveout | Notes |
|---|---|---|---|---|
| `vpu20_p1.mbn` 2021-05 (c7b11ed1) | `video-firmware.1.0-df9cb37c…` | v6 | 0x500000 | very old |
| `vpu20_p1.mbn` 2022-10 → 2024-08 (05df8e65, 36db650d; identical bytes) | `video-firmware.1.0-8fd4ba98…` | v6 | 0x500000 | **pre-fix Gen1** |
| `vpu20_p1.mbn` 2024-11-13 (aeede7af) | `video-firmware.1.0-ed457c18…` | v6 | 0x500000 | **current Gen1**; fixes encoder EOS handling, AVC High level, QP range, HEVC green-frame decode |
| `vpu20_p1_gen2.mbn` 2025-04 (51b35ac2) | `video-firmware.2.4.2-d7a3d538…` | **v7** | 0x700000 | **SC7280 rejects MBNv7**; replaced |
| `vpu20_p1_gen2_s6.mbn` 2026-02-20 (30a139cb) | `vfw-3.4:rel0059-6b205d37…` | v6 | **0x700000** | **correct Gen2**; `vpu20_p1_gen2.mbn` is now a WHENCE link to it |

Sizes are what `qcom_mdt_get_size()` returns (it rounds up to 4 KiB): Gen2 is **exactly 7 MiB**, Gen1 exactly 5 MiB.

## 4. Findings

### 4.1 Encoding is possible (**P**)
`strings -a` on both images: `venus_venc_codec_h264.c`, `venus_venc_codec_h265.c`, `venus_venc_rate_control.c`, `venus_venc_motion_estimate.c`. Gen2 additionally has its own HFI command layer (`venus_venc_c2_driver.c`, `venus_venc_swi.c`). Gen1 also has VP8 encode; Gen2 does not. Iris enumerates H.264/HEVC (CAPTURE) and NV12/QC08C (OUTPUT) on SC7280 because the encoder format tables in `iris_venc.c:82-90` are hard-coded and platform-independent.

### 4.2 Scaling
| Question | Answer | Grade |
|---|---|---|
| Does the VPU contain a scaler? | Yes: VPSS. Both images contain `Running VPSS in m2m mode`, `VCODEC_DMA_VPSS_MEM2MEM_*` registers, fixed downscale filters for ratios 1.25 / 1.5 / 2.0 / 3.0, and `Output2 upscale not supported` (downscale only) | P |
| Is the decoder's downscale reachable from V4L2? | **No**, in either driver. On HFI 3x+/6xx the multi-stream property is packed as only `{buffer_type, enable}` (Venus `hfi_cmds.c:1147-1155`; Iris `iris_hfi_gen1_defines.h:424`). Venus programs OUTPUT2 at `inst->width/height`, the stream size (`venus/vdec.c:754-755,826`). Iris's decoder `S_SELECTION` returns `-EINVAL` (`iris_vidc.c`) and `try_fmt` forces CAPTURE size back to the stream size | P |
| Encoder-side scaling in Venus? | No: input and output resolution are programmed identically (`venus/venc.c:1043-1049`) | P |
| Encoder-side scaling in Iris? | The driver sends raw size, crop offsets and bitstream size separately (Gen2 `HFI_PROP_*` in `iris_hfi_gen2_command.c` set_bitstream_resolution / set_crop_offsets; Gen1 `FRAME_SIZE` INPUT vs OUTPUT). Radxa PR #593 body says it fixed "hardware encoding, scaling…" on QCS6490. The Iris encoder also allocates a VPSS internal buffer (`iris_vpu_enc_vpss_size`) | P (plumbing) / Q (claim) |
| Does the SC7280 firmware actually scale during encode? | I could not find encoder scaling strings in the binaries, but absence of strings is not proof | **U** |

→ Treat hardware scaling during encode as **unproven**; the provided `v4l2-enc-probe` tells you whether the driver at least accepts a coded size smaller than the raw size. Decoder-side downscale would need a new kernel patch.

### 4.3 Ranked causes of "it doesn't work"

| # | Cause | Affects | Grade | Key evidence |
|---|---|---|---|---|
| 1 | **Gen2 firmware missing / old MBNv7 blob / pinned name bypasses fallback** | 7.0.11 Iris | P | linux-firmware `30a139cb`: "earlier upstreamed firmware (vpu20_p1_gen2) … incorrectly signed with an MBNv7 header. SC7280 only supports … MBNv6". Hash-segment `header_vsn` 7 vs 6 verified on both blobs. `radxa-firmware-qcs6490` ships **no** `qcom/vpu/*` (only `radxa-firmware-sc8280xp` does). The Q6A DTS sets `firmware-name = "qcom/vpu/vpu20_p1_gen2_s6.mbn"`; with a DT name set, `iris_detect_firmware()` never falls back to Gen1 |
| 2 | **TrustZone mode + encode use case → secure SID `0x2184`** → board reset | Venus & Iris in default mode | P (mechanism) / S (report) | Upstream `82066cdb176` (reviewed by Qualcomm): "the secure SID 0x2184 … (on some boards) we cannot touch that"; `ea2e2ea551a`: "Some SC7280-based boards crash when providing the 'secure_non_pixel' context bank". Qualcomm firmware note (LKML thread Oct 2024–Jan 2025, **S**): SID 0x2184 is "one of the secure internal buffer[s] specific to encode usecase"; firmware changed so the VPU generates only 0x2180 (the timing is consistent with the Gen1 update of 2024-11-13, but I did not confirm that this blob contains the change — **U**). Forum thread "Gstreamer encoder usage reboot the board – Q6A" (**S**): the board reboots on `v4l2h264enc`/`v4l2h265enc`; enabling Hypervisor Override fixes it |
| 3 | **Old Gen1 firmware** lacks Nov-2024 encoder fixes | Venus, Iris Gen1 | P | `aeede7af` message lists three encoder fixes (EOS handling, AVC High level, QP range). Old bytes shipped 2022-10 → 2024-08; distro `linux-firmware` snapshots from before Nov 2024 (e.g. bookworm- or noble-era) would carry them — inferred from dates, **U**; check the actual file with the diag script |
| 4 | **Missing `dma-coherent` on the venus node** → "wrong input data" faults, corrupted capture at higher resolutions | 6.18.2 + Venus | P | Qualcomm kernel-topics PR #1640/#1641 commits `007dd65`, `49de73e`, `Fixes: 37613aee2179` ("sc7280: Add venus DT node"). Radxa 6.18.2 `sc7280.dtsi` lacks it; Radxa 7.0.11 `kodiak.dtsi` has it (Radxa `631429e04`). **Earlier I wrongly called Radxa's property a risk — retracted.** |
| 5 | **GStreamer encode at 25/29 fps fails** ("internal data stream error") | Iris encoder | P | Qualcomm `a985eda` / Radxa `38befa2de` (already in 7.0.11): frame intervals were advertised stepwise 1/480 |
| 6 | **Gen2 does not fit a stock 5 MiB carveout; no fallback** | boards other than Radxa's | P | Gen2 needs exactly 0x700000. Stock `kodiak.dtsi` `video_mem` is 5 MiB; load fails with `-22` ("firmware download failed -22"). `iris_detect_firmware()` is identical in Radxa 7.0.11 and qcom 7.3-rc5, so this is an **upstream** defect. Q6A/Q6B/CM-Q64 DTS already use 7 MiB |
| 7 | **VP9 crashes Gen1 firmware** | 6.18 + Venus | P (error text) | kernel-topics issue #222: `no valid instance(pkt session_id:ff, pkt:21001)`, `System error has occurred, recovery failed to init HFI`, assertion at `vpx_decoder.c:5653`. Issue closed 2025-12-27 (reason not readable). Gen1 blob unchanged since 2024-11-13, so not a Gen1 firmware fix. Irrelevant to encode |
| 8 | Venus rejects sessions with "HW can't support this load" | 6.18 + Venus | P | `decide_core()` returns `-EINVAL` when estimated load exceeds the top OPP; Radxa's `9799c23c7` comments that out and also disables 10-bit `QC10C` gating |
| 9 | Open Qualcomm Iris bug: AHB bridge not reset before HW-mode switch | Iris (VPU2 shares `iris_vpu_set_hwmode`) | P / U | kernel-topics PR #1907, `Fixes: 95a337f92f0a` which Radxa carries (`6fb1a2768`). SC7280 impact unverified |
| 10 | Gen2 encode on VPU 2.0 sizes encoder buffers with the **VPU3.3** formulas | Iris Gen2 encode | U | `iris_vpu33_buf_size()` (decoder defers to VPU2 formulas, encoder uses VPU3.3 set); the firmware prints "Driver macro size … vs FW HFI macro size" checks. Would appear as a buffer-size error in SFR messages |

### 4.4 Ruled out (**P**)
- Gen1/Gen2 auto-detect heuristic (`iris_detect_gen2_from_fwdata`): ran Radxa's and upstream's versions against all five real blobs — all classified correctly (`vfw…` and `video-firmware.N.*` with N≥2 → Gen2).
- Compressed firmware support: `qcom_module.config` has `FW_LOADER_COMPRESS=y` and `_ZSTD=y`; XZ defaults on.
- Double driver binding: impossible on 7.0.11 (see 3.2).
- UBWC config lookup: `qcom,qcm6490` is in `drivers/soc/qcom/ubwc_config.c`.
- SC7280 Iris platform data is byte-identical to upstream.
- Reserved-memory overlap for the 7 MiB carveout (`0x85b00000`–`0x86200000`): none.

### 4.5 What Radxa and Qualcomm are doing (**Q** unless noted)
- Radxa kernel **PR #593** (merged 2026-08-12), body, verbatim: *"Improve IRIS V4L2 M2M compatibility on SC8280XP and QCS6490. Fix hardware encoding, scaling, buffer handling, and error recovery. Enable reliable FFmpeg hardware transcoding and Jellyfin playback. Fix H.264/H.265 WebRTC streaming and keyframe recovery in One-KVM. Prevent IOMMU faults and crashes during codec teardown."* No test logs or board list in the PR.
- Radxa kernel **PR #607** (merged 2026-09-15), body: *"Required for working iris in EL2 on sc8280xp/qcs6490"* (ports Stephan Gerhold's non-TZ boot path from Venus).
- Qualcomm: kernel-topics PR #1567 adds the same EL2 `video-firmware` node for Kodiak (PENDING); PR #1651 moves Iris/Venus to the generic PAS API (open); PR #1907 AHB-reset fix (open); `ubuntu-qcom-kernel` PR #118 "Iris encoder feature enhancements" was tested on **SM8650**, not SC7280.
- Radxa `linux-7.0.11` already carries the Qualcomm DMA-coherency, power-off-ordering and frame-interval fixes plus Radxa's encoder hardening (`87c604bff` Gen1 joined headers, `08282c7c5` visible-vs-aligned frame size, `4fef0d63f` force keyframe, `2ac17acf5` padded NV12, `d655943bf` empty Gen2 drain, `038eff48c` SC8280XP header prepend).

### 4.6 Upstream Iris fixes NOT in Radxa 7.0.11 (candidates for Phase 6)
`0ac05c4d9f` `iris_allow_cmd()` bitmask test · `b9c2215bde` `disable_irq()` at power-off (stable) · `f87d7eda07` runtime-PM reference leaks (stable) · `75d79879ec` resume-failure handling in core deinit · `727a87c71b` duplicate `HFI_PROP_OPB_ENABLE` · `75126861e6` missing `break` (harmless today). Pending, unmerged: kernel-topics #1907.
Confirmed present in Radxa's tree: the `iris_allow_cmd` bug (`iris_state.c:273-277`) and `disable_irq_nosync` (`iris_vpu_common.c:240`).

## 5. Recommendation — "the one way"

**Radxa `linux-7.0.11` + Iris + Gen2 `vpu20_p1_gen2_s6.mbn` + EL2 (Hypervisor Override on). Encode through V4L2 stateful clients (ffmpeg `*_v4l2m2m`, GStreamer `v4l2h264enc`/`v4l2h265enc`). Scale before the encoder unless the probe + a streamed test prove encoder-side scaling.**

Why:
1. It is the configuration Radxa itself says it fixed encode/scaling for on QCS6490 (**Q**).
2. EL2 removes the documented TZ secure-SID crash class (**P**/**S**).
3. 7.0.11 already carries every fix Qualcomm published for corruption, power-off and GStreamer negotiation (**P**).
4. It needs **no kernel change** — only firmware, a UEFI setting and verification.

Alternatives considered:
| Option | Verdict |
|---|---|
| Iris + Gen2 in default TZ mode | Possible, **untested**; encode may reset the board. Only try with human approval and log capture, and only after firmware is verified |
| Venus + Gen1 (≥ 2024-11-13) in TZ, build with `CONFIG_VIDEO_QCOM_IRIS=n`, drop DT `firmware-name` | Plausible, not Radxa-supported; FP5 enablement commit says encoder/decoder "will start working" but I saw no test log. Keep as fallback if Iris encode fails in EL2 |
| Stay on 6.18.2 + Venus | **Rejected**: missing `dma-coherent`, VP9 firmware crash, Radxa carries load-check hacks |

## 6. Open questions (what the on-device data must answer)
1. Which boot mode and which firmware generation is the system in now?
2. In EL2 + Gen2: does H.264/HEVC encode pass `encode-validate.sh` at 720p/1080p/25 fps/29 fps?
3. Does `v4l2-enc-probe` show the coded size staying smaller than the raw size? If yes, does a *streamed* test actually produce a scaled stream? (needs a streaming extension — Plan Phase 5)
4. Do repeated open/close cycles produce SMMU faults (Radxa PR #593 mentions teardown IOMMU faults)?
5. Is the forum-reported reboot reproducible in TZ mode with correct firmware? (S → P)

## 7. Corrections made during the research (so you can trust the rest)
| Earlier statement | Status |
|---|---|
| Radxa's `dma-coherent` property may be a hazard | **Retracted** — Qualcomm's own fix adds it |
| Gen2 image is "7 MiB minus 4 KiB" | **Corrected** to exactly 0x700000 |
| "The encoder path has no scaler" | **Weakened** to unproven (absence of strings ≠ proof) |
| A search summary said the Qualcomm series "adds `dma_sync` calls" | **Wrong**; the commits only add the `dma-coherent` property |
| A first PSNR check scored a perfect encode at ~29 dB | **Fixed**: frames were paired by timestamp; now by index (see `tools/encode-validate.sh`) |

See `EVIDENCE.md` for the claim-by-claim ledger and re-check commands, `PLAN.md` for the plan and `GUIDE.md` for the runbook.
