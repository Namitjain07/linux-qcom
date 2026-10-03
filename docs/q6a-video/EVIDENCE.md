# Evidence ledger and source index

Grades: **P** primary (code/commit/binary inspected) · **Q** PR-page quote verified verbatim on a second fetch · **S** secondhand (snippet of a page that could not be opened) · **U** unproven.

All line numbers refer to `radxa/kernel` @ `a50eb5b71984bd82b49cd5d31b1e2eb5505c6512` (branch `linux-7.0.11`, 2026-09-10) unless noted.

## A. Re-verification quick-start

```bash
# 1. the exact kernel this repo builds
git clone --filter=blob:none --no-checkout https://github.com/radxa/kernel radxa-kernel && cd radxa-kernel
git checkout a50eb5b71984bd82b49cd5d31b1e2eb5505c6512
# 2. Radxa's 6.18 and Qualcomm main for comparison
git fetch origin linux-6.18.2:refs/remotes/origin/linux-6.18.2
git clone --filter=blob:none https://github.com/qualcomm-linux/kernel qcom-kernel
# 3. PR heads over anonymous git (GitHub REST API is not needed)
git fetch origin refs/pull/593/head:pr593 refs/pull/607/head:pr607        # in radxa-kernel
git -C ../qcom-kernel remote add topics https://github.com/qualcomm-linux/kernel-topics
git -C ../qcom-kernel fetch topics refs/pull/1640/head:pr1640 refs/pull/1641/head:pr1641 refs/pull/1907/head:pr1907
# 4. firmware blobs (pinned)
curl -fLO https://gitlab.com/kernel-firmware/linux-firmware/-/raw/30a139cb656157cba7a27fd7727b5ffbd302ff60/qcom/vpu/vpu20_p1_gen2_s6.mbn
```

## B. Claims → evidence → how to re-check

| ID | Claim | Grade | Evidence | Re-check |
|---|---|---|---|---|
| B1 | This repo is a Debian packaging wrapper; `src` = radxa/kernel `linux-7.0.11` @ `a50eb5b…` | P | `.gitmodules`; `git ls-tree HEAD src` | `git -C <repo> ls-tree HEAD src` |
| B2 | Build uses `defconfig qcom_module.config radxa.config radxa_custom.config`; `VIDEO_QCOM_IRIS=m` | P | `.github/local/Makefile.local`; `arch/arm64/configs/defconfig:921`, `qcom_module.config:1992` | `grep -n VIDEO_QCOM arch/arm64/configs/*` |
| B3 | On 7.0.11 Iris is the only SC7280 driver | P | `drivers/media/platform/qcom/venus/core.c:1138-1141` `#if (!IS_ENABLED(CONFIG_VIDEO_QCOM_IRIS))` wraps the `sc7280-venus`/`sm8250-venus` entries; Iris match at `iris/iris_probe.c:373` | read those lines |
| B4 | Iris SC7280 platform data: Gen1 `qcom/vpu/vpu20_p1.mbn`, Gen2 `qcom/vpu/vpu20_p1_gen2_s6.mbn`; identical to upstream | P | `iris/iris_platform_vpu2.c:20-30,79-113`; `diff` vs qcom main shows only unrelated additions | `diff` the two files |
| B5 | Firmware loads on first `open()`, not at boot | P | `iris/iris_vidc.c` `iris_open()` → `iris_core_init()` | read `iris_open` |
| B6 | With DT `firmware-name` set there is no Gen2→Gen1 fallback; without it, fallback happens only if `request_firmware` fails (not on size/auth failure) | P | `iris/iris_firmware.c` `iris_detect_firmware()`, `iris_load_fw_to_memory()`; function is identical in qcom 7.3-rc5 | `diff` of `iris_detect_firmware` between the two trees |
| B7 | Q6A DTS pins Gen2 name, 7 MiB carveout @ `0x85b00000`, `iris_iova` reserved IOVA `[0,0x25800000)` | P | `qcs6490-radxa-dragon-q6a.dts:205,250,1168-1173`; same in Q6B and CM-Q64 | read lines |
| B8 | Radxa 6.18.2 Q6A: carveout `0x8fe00000` 5 MiB, `&venus { status = "okay"; }` only, no `dma-coherent` | P | `git show origin/linux-6.18.2:arch/arm64/boot/dts/qcom/qcs6490-radxa-dragon-q6a.dts`, `…/sc7280.dtsi` | as shown |
| B9 | Radxa KVM overlay maps SIDs `0x2180`+`0x2184` and a `video-firmware` subnode `0x21a2`; disables GPU zap; sets `radxa,enable-kvm`, `qcom,broken-reset` on ADSP/CDSP | P | `arch/arm64/boot/dts/qcom/qcs6490-radxa-kvm.dtso` | read file |
| B10 | Non-TZ path exists in Iris: `video-firmware` subnode ⇒ `use_tz = false`; IOMMU map + register reset | P | `iris/iris_firmware.c` `iris_fw_init()`, `iris_reset_cpu_no_tz()`; Radxa PR #607 / `395349af3` (Stephan Gerhold) | read file |
| B11 | Gen2 image needs exactly `0x700000`, Gen1 `0x500000` | P | `qcom_mdt_get_size()` (`drivers/soc/qcom/mdt_loader.c`) rounds `max_addr` up to 4 KiB; computed from ELF program headers of the real blobs | diag script §5 prints it |
| B12 | The Gen1/Gen2 autodetect heuristic is correct for all real blobs | P | ran Radxa's and upstream's `iris_detect_gen2_from_fwdata` in userspace on 5 blobs | embedded version strings: `vfw-3.4:…` and `video-firmware.2.x` ⇒ Gen2; `video-firmware.1.0` ⇒ Gen1 |
| B13 | Old Gen2 blob is MBNv7, fixed blob MBNv6 | P | hash segment `header_vsn` = 7 (blob `3c21c8ca…`) vs 6 (`f061733f…`); linux-firmware `30a139cb` message | diag script §5 |
| B14 | Both firmwares contain H.264/HEVC encoders | P | `strings -a` shows `venus_venc_codec_h264.c`, `venus_venc_codec_h265.c`, `venus_venc_rate_control.c`, `venus_venc_motion_estimate.c` | `strings -a <blob> \| grep venus_venc_codec` |
| B15 | Gen1 has VP8 encode, Gen2 does not | P | `venus_venc_codec_vpx.c` present only in Gen1 | same |
| B16 | VPSS scaler exists; decode downscale only (1.25/1.5/2/3, no upscale) | P | strings: `Running VPSS in m2m mode`, `Output2 upscale not supported`, `DS_Ratio:1.25/1.5/2.0/3.0` | `strings -a <blob> \| grep -iE 'vpss\|downscal\|DS_Ratio'` |
| B17 | Neither driver exposes decoder downscale | P | Venus `hfi_cmds.c:1147-1155` (3x pack = type+enable), `vdec.c:754-755,826`; Iris `iris_vidc.c` decoder `S_SELECTION` → `-EINVAL`, `iris_vdec.c` forces CAPTURE size | read lines |
| B18 | Venus encoder programs equal in/out size | P | `venus/venc.c:1043-1049` | read lines |
| B19 | Iris encoder sends raw, crop and bitstream sizes separately | P | `iris_venc.c` `iris_venc_s_fmt_input/_output`, `iris_venc_s_selection`; `iris_hfi_gen2_command.c` `set_bitstream_resolution`, `set_crop_offsets`; `iris_hfi_gen1_command.c:795-815` | read lines |
| B20 | Iris exposes H264/HEVC encode on SC7280 | P | `iris_venc.c:82-90` (platform-independent tables) | read lines |
| B21 | Iris ICC bandwidth for encoders comes from the *decode* table | P | `iris_power.c:28` `bw_tbl_dec` | read line |
| B22 | Gen2 descriptor sizes decoder buffers with VPU2 formulas and encoder buffers with VPU3.3 formulas | P | `iris_vpu_buffer.c` `iris_vpu33_buf_size()` | read function |
| B23 | `iris_allow_cmd()` bitmask bug and `disable_irq_nosync` at power-off are present in Radxa 7.0.11 and fixed upstream | P | `iris_state.c:273-277`; `iris_vpu_common.c:240`; fixes `0ac05c4d9f`, `b9c2215bde` | read lines + commits |
| B24 | `dma-coherent` on the SC7280 venus node is the Qualcomm fix for faults/corruption | P | kernel-topics PR #1640/#1641 commits `007dd65` (binding), `49de73e` (kodiak.dtsi), both `Fixes: 37613aee2179`, `Cc: stable`. Radxa 7.0.11 has it via `631429e04`; 6.18.2 does not (B8) | `git show` the commits |
| B25 | Secure SID `0x2184` cannot be touched on some boards | P | upstream `82066cdb176` (Luca Weiss, 2023-12-01, reviewed by Vikash Garodia/Bryan O'Donoghue); `ea2e2ea551a` (2024-04-12) "Some SC7280-based boards crash when providing the 'secure_non_pixel' context bank" | `git show` in qcom main (history back to 2023-11) |
| B26 | Upstream Iris on SC7280 is PAS/TZ only; non-TZ is future ABI | P | `01022af2d21` (2026-01-31), `ab4e6a16f97` (2026-03-27) | `git show` |
| B27 | `radxa-firmware-qcs6490` ships no `qcom/vpu/*`; `radxa-firmware-sc8280xp` ships `vpu20_p4_gen2_s6.mbn` | P | `git ls-tree -r HEAD` of `radxa-pkg/radxa-firmware` @ `f19f2c4` (v0.2.42, 2026-09-17); its `debian/control` depends on distro `firmware-qcom-*`/`linux-firmware-qualcomm-misc` | clone and `ls-tree` |
| B28 | Gen1 firmware 2024-11-13 fixed encoder EOS handling, AVC High level, QP range (+ HEVC green frames) | P | linux-firmware `aeede7af` commit message | `curl https://gitlab.com/api/v4/projects/kernel-firmware%2Flinux-firmware/repository/commits/aeede7af` |
| B29 | Gen1 blob unchanged between 2022-10 and 2024-08 | P | sha256 `4b48c6f9…` for both `05df8e65` and `36db650d` | download both |
| B30 | Radxa PR #593 body | Q | verbatim in `REPORT.md` §4.5 (fetched twice) | open the PR |
| B31 | Radxa PR #607 body "Required for working iris in EL2 on sc8280xp/qcs6490" | Q | fetched twice | open the PR |
| B32 | Q6A encode reboots in TZ mode; Hypervisor Override fixes it | **S** | search snippet of forum thread "Gstreamer encoder usage reboot the board – Q6A" | page blocked from the research environment |
| B33 | Qualcomm: SID `0x2184` is a secure internal buffer for the encode use case; firmware changed so only `0x2180` is generated | **S** | search snippet of the Oct 2024–Jan 2025 LKML thread on "arm64: dts: qcom: sc7280: enable venus node" | page blocked |
| B34 | Gen2 encode on VPU 2.0 works | **U** | no test evidence found | on-device |
| B35 | Encoder-side hardware scaling works on SC7280 | **U** | plumbing exists (B19); Radxa claims (B30); no firmware proof | `v4l2-enc-probe` + streamed test |
| B36 | HFI command queue accepts a packet that exactly fills it (`iris_hfi_queue.c:24`); Venus rejects (`hfi_venus.c:198`) | P | code + `tests/ring-queue` replay | `tests/ring-queue/run.sh` |
| B37 | `iris_hfi_queue_cmd_write()` puts the PM reference twice when the resume fails (`:135`/`:149`) | P | code; `pm_runtime_resume_and_get()` drops the count itself | read lines |
| B38 | IRQ thread reads interrupt registers under `core->lock` with no ordering against runtime-suspend power-off (`iris_hfi_common.c:133`) | P | code; locking model (`tests/irq-model`) — kernel run-time behaviour **U** | read; run model |
| B39 | Upstream `b9c2215bde` would call `disable_irq()` with `core->lock` held in `iris_core_deinit()` while the thread needs the lock | P (code) / model | `iris_core.c:17-25`, `iris_hfi_common.c:133` | `tests/irq-model` case 2 |
| B40 | A firmware larger than the carveout fails with no message (`iris_firmware.c:164`) | P | code | read |
| B41 | Nodes are registered before drvdata/DMA mask/runtime PM (`iris_probe.c:281` vs `:293`, `:306`) | P | code | read |
| B42 | Encoder `G_SELECTION` bounds (aligned 1088) ≠ `S_SELECTION` limit (visible 1080) (`iris_vidc.c:504`, `iris_venc.c:295,361`) | P | code | read; `tools/v4l2-enc-neg-test` on a node |
| B43 | `iris_venc_try_fmt()` accepts any size and an upscale (`iris_venc.c:169`) | P | code; Gen1 error code `HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED` logged at debug only (`iris_hfi_gen1_response.c:239-245`) | read |
| B44 | Gen1 never sends a crop to the firmware; a crop request becomes a resize of the whole frame | P | `grep -i crop iris_hfi_gen1*.c` shows only decode-side extradata | grep |
| B45 | Iris sends buffer timestamps in nanoseconds, Venus in microseconds | P | `iris_common.c:24`, `venus/helpers.c:509-510` | read; effect **U** |
| B46 | The 25-commit series builds clean per commit (clang 18, arm64, W=1), tip also with IRIS=n; checkpatch/analyzer clean on code | P | build logs in session; `git rebase --exec` | rerun `patches/README.md` build line |
| B47 | `fw-detect` replay: unterminated marker at blob end faults before the fix | P | `tests/fw-detect` | run |

## C. Commit / PR index

| Where | ID | Date | Author | Subject / meaning |
|---|---|---|---|---|
| radxa/kernel | `631429e04` | 2026-06-08 | Xilin Wu | kodiak: mark Venus/Iris as dma-coherent |
| radxa/kernel | `b1446e9db` | 2026-06-08 | Xilin Wu | Q6A: switch to Iris HFI Gen2 firmware (7 MiB carveout, `iris_iova`) — "larger than the original video firmware carveout" |
| radxa/kernel | `395349af3` | 2025-08-06 | Stephan Gerhold | iris: port firmware loading without TZ/PAS from venus |
| radxa/kernel | `9c962d3aa` | 2026-05-29 | Dikshita Agarwal | iris: Gen2 firmware autodetect and fallback |
| radxa/kernel | `38befa2de`, `f50b9f96c` | 2026-08-18 | Vishnu Reddy | frame-interval fix (GStreamer encode); power-off ordering fix |
| radxa/kernel | `87c604bff` `08282c7c5` `4fef0d63f` `2ac17acf5` `d655943bf` `038eff48c` `d5cdbbdaa` `6198771c7` `4e94efbb5` `96982befa` `a5ca5d13e` `fe41e5e55` | 2026-08-02…12 | Junhao Xie | encoder + teardown hardening (PR #593) |
| radxa/kernel | `9799c23c7` | 2025-10-09 (6.18) | Xilin Wu | venus: comment out "HW can't support this load" + 10-bit gating workaround |
| Namitjain07/kernel | PR #1 (`claude/q6a-iris-venus-fixes`) | 2026-10-03 | — | the 25-commit Iris/Venus fix series (draft, untested on hardware) |
| radxa/kernel | PR #593 / #607 / #600 | 2026-08-12 / 09-15 / — | BigfootACA / strongtz / nascs | see B30/B31; #600 "default decoder output to linear NV12" |
| qualcomm-linux/kernel | `55ee57c12`→`8f100f5896` | 2026-03-27 | Dmitry Baryshkov | venus: flip the venus/iris switch |
| qualcomm-linux/kernel | `412a2e5955` | 2026-06-10 | Dikshita Agarwal | iris: Gen2 firmware autodetect and fallback |
| qualcomm-linux/kernel | `0ac05c4d9f` `b9c2215bde` `f87d7eda07` `75d79879ec` `727a87c71b` `75126861e6` | 2026-05…06 | Dikshita Agarwal / Hungyu Lin | Iris stability fixes missing from Radxa 7.0.11 |
| qualcomm-linux/kernel | `c53e055028` | 2026-03-31 | Renjiang Han | venus: relax encoder frame/blur step on v6 (Venus path only) |
| qualcomm-linux/kernel | `82066cdb176` `2aa72de2fc9` `ea2e2ea551a` | 2023-12 / 2024-04 | Luca Weiss | sc7280: move video-firmware + SID 0x2184 to chrome-common; FP5 venus enable; allow one IOMMU |
| qualcomm-linux/kernel | `01022af2d21` `ab4e6a16f97` | 2026-01-31 / 03-27 | Dmitry Baryshkov | drop non-PAS for SC7280 (Chrome disabled; binding) |
| kernel-topics | PR #1640 (`a985eda`, `007dd65`), #1641 (`49de73e`) | 2026-08 | Vishnu Reddy | Iris frame interval; `dma-coherent` binding + DTS |
| kernel-topics | PR #1907 (`25cd9a2`) | 2026-09-24 | Vikash Garodia | reset AHB bridge before HW mode (open) |
| kernel-topics | PR #1567 (`edd5356`) | 2026-07-17 | Vishnu Reddy | kodiak-el2: venus firmware subnode (PENDING) |
| kernel-topics | PR #1651 | 2026-08-06 | pgujjula | Iris/Venus to generic PAS API (open) |
| kernel-topics | issue #222 | 2025-10-19 → closed 2025-12-27 | HeyMeco | VP9 with Venus fails (QCS6490, 6.18); closing reason unreadable |
| ubuntu-qcom-kernel | PR #118 | 2026-09-17 | WangaoW | Iris encoder features; tested on SM8650-HDK only |
| linux-firmware | `51b35ac2` / `30a139cb` / `aeede7af` / `36db650d` / `05df8e65` / `c7b11ed1` | 2025-04-21 / 2026-02-20 / 2024-11-13 / 2024-08-09 / 2022-10-25 / 2021-05-03 | Dikshita Agarwal et al. | Gen2 added (MBNv7) / Gen2 fixed `_s6` MBNv6 / Gen1 current / path rename / Gen1 2022 / Gen1 2021 |

## D. Firmware fingerprints

| File @ commit | Version string | sha256 | MBN | Verdict |
|---|---|---|---|---|
| `qcom/vpu/vpu20_p1_gen2_s6.mbn` @ `30a139cb656157cba7a27fd7727b5ffbd302ff60` | `vfw-3.4:rel0059-6b205d3781e0dbd192a3ba6fb1ef00733bac58a8` | `f061733f2f0c644281b3455eb75b654932fe11083e4dfa9257dba72294a5f37f` | v6 | correct Gen2 |
| `qcom/vpu/vpu20_p1.mbn` @ `aeede7afb7a186b62f9e1f959c33fd5f2dea0f7a` | `video-firmware.1.0-ed457c183307eff1737608763ca0f23656c95b53` | `607545ead3f23134680a1330cd569f2a6353f8b83dbabb924c68ac573f9d06c1` | v6 | current Gen1 |
| `qcom/vpu/vpu20_p1.mbn` @ `36db650d` (= `05df8e65`) | `video-firmware.1.0-8fd4ba980f726e389c6c07fbae6bebdfe904e3c8` | `4b48c6f93f6678dbb4de56419a930497afd37f459ee1c1827f24b1979070854e` | v6 | pre-fix Gen1 |
| `qcom/vpu/vpu20_p1_gen2.mbn` @ `51b35ac23ee7ca7ccea70afeb66840ee204ee185` | `video-firmware.2.4.2-d7a3d5386743efb16b828e08695bea7722cafadd` | `3c21c8ca9daf60e5d21cd6d3c1af0420790ea2c260bfd73511255a01aae3e744` | **v7** | rejected by SC7280 |

## E. Sources actually read

Read directly (P/Q): `radxa/kernel` (branches `linux-7.0.11`, `linux-6.18.2`, PR heads 593/607/600); `qualcomm-linux/kernel` main (history from 2023-11); `qualcomm-linux/kernel-topics` PR heads 1567/1640/1641/1651/1842/1907/1936; `radxa-pkg/radxa-firmware` @ `f19f2c4`; linux-firmware via the GitLab API and raw blobs; Radxa PR pages #593, #607; `github.com/qualcomm-linux/kernel-topics/issues/222` (opening post only — comments load lazily); `github.com/qualcomm-linux/ubuntu-qcom-kernel/pull/118`; `github.com/radxa-pkg/libva-v4l2/issues/6` (SC7180 HEVC VAAPI extradata — not about Q6A).

Secondhand (S): forum.radxa.com thread 29828; the Qualcomm support-forum post on iris encode for sc7280 (said upstream Iris then exposed no encoder formats on SC7280 — stale vs 7.0.11, see B20); Oct 2024–Jan 2025 LKML "sc7280: enable venus node" thread; Armbian PR #10162 (Q6A on 7.1: only mentions Iris SFR diagnostics, no encode data).

**Blocked by network policy** (unreadable here): forum.radxa.com, docs.radxa.com, lore.kernel.org, patchwork.kernel.org, patches.linaro.org, lkml.iu.edu, ratatoskr.run, patchew.org, mail-archive.com, mysupport.qualcomm.com. If your agent can reach them, the highest-value reads are: the forum thread 29828 (what firmware/OS the reporter had), the Oct 2024–Jan 2025 LKML thread (exact wording on SID 0x2184 and which firmware fixed it), and Radxa's Q6A docs pages on video/EL2.

## F. Known gaps
No on-device data; the TZ-mode reboot mechanism is documented upstream (B25) but its application to *encode on this board* rests on S-grade reports (B32, B33); Gen2 encode correctness on VPU 2.0 (B34) and encoder-side scaling (B35) are open; the `.zst` branch of the diag script's firmware reader was not exercised (no `zstd` in the research sandbox; `.xz` and plain were); nothing in `tools/` was run against real hardware; the patch series is compiled and statically checked but **never run on hardware**, and the clang static analyzer only exercises the touched files in isolation.
