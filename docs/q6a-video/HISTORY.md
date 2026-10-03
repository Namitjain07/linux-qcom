# History of the Venus / Iris drivers across the three repositories

What was read: the commit history touching `drivers/media/platform/qcom/{venus,iris}` (plus the SC7280 device-tree and firmware-loading code that decides whether they work), in the three repositories you named, and how each relates to the Dragon Q6A (QCS6490 = SC7280 "kodiak").

Dates are the commit-metadata dates in the lists under [`data/`](data/). Hashes are 9–11 hex digits as printed by `git log`. Everything here is **P** (read in the git objects) unless it says otherwise; the PR-level context is in [`EVIDENCE.md`](EVIDENCE.md) §C.

## 1. The three repositories

| Repo | What it is | State when read |
|---|---|---|
| `Namitjain07/linux-qcom` | Radxa's **Debian packaging** of the kernel (`debian/`, `Makefile`, templates). `src/` is a submodule → `radxa/kernel`, branch `linux-7.0.11`, **pinned to `a50eb5b71`**. `debian/patches/linux/` holds only two config patches (`radxa.config`, `radxa_custom.config`); neither touches video options. 51 commits, 14 release commits (`6.17.1-6 … 7.0.11-7`). | Full history. Branch `claude/dazzling-fermat-u820ir` = research bundle (PR #1) |
| `Namitjain07/kernel` | Fork of `radxa/kernel`, default branch `linux-7.0.11`, tip `a50eb5b71` "drm/msm/dp: filter unstable modes on radxa dragon q6b". This is exactly what the packaging builds, so it is where the driver fixes belong. | Depth-1 clone (the history of the files was read from the deeper `radxa/kernel` clone, `--shallow-since=2026-03-20`) |
| `Namitjain07/kernel-qualcomm-clone` | Clone of `qualcomm-linux/kernel` `main` at **Linux 7.3-rc5** (`72d3fcf80`) — Qualcomm's integration tree, used as the reference for "what upstream already fixed". | Depth-1 clone; the history list was taken from a deeper `qualcomm-linux/kernel` clone (247 215 commits reachable) |

Kernel the board runs (from the packaging): **7.0.11**, with `CONFIG_VIDEO_QCOM_IRIS=m` and `CONFIG_VIDEO_QCOM_VENUS=m` in `arch/arm64/configs/defconfig` (lines 921–922). With Iris enabled, Venus' SC7280/SM8250 entries are compiled out (`venus/core.c:1138-1141`), so **Iris is the only driver bound to `qcom,sc7280-venus`**.

## 2. Timeline (what changed for SC7280 video, in order)

| When | Where | Commit | What / why it matters here |
|---|---|---|---|
| 2023-12-01 | Qualcomm | `c2a8653c197` Luca Weiss | Venus: secure memory ranges for SC7280 (first SC7280 enablement; FP5 bring-up per `EVIDENCE.md` §C) |
| 2024-12-30 | Qualcomm | `687bfbba5a1`, `354846c3e9c` Bryan O'Donoghue | Venus: static encoder/decoder declarations and node names |
| 2025-02-07 | Qualcomm | `79865252acb` … Dikshita Agarwal | **Iris introduced** (SM8250, decoder only): firmware load, boot, core state, HFI queues |
| 2025-08-06 | Radxa | `395349af3` Stephan Gerhold | Iris: port the **no-TZ/PAS firmware loading** from Venus (needed for EL2 boot; not in Qualcomm main) |
| 2025-08-25 | Qualcomm | `787c535a9ff` … Dikshita Agarwal | **Iris encoder device**: S/G/TRY_FMT, selection, parm, frame sizes/intervals, drain, internal buffers |
| 2025-10-28 | Qualcomm | `542e3540ddd` Dmitry Baryshkov | **Iris: enable SC7280** (+ rename SM8250 platform file to "gen1") |
| 2025-11-14 | Qualcomm | `121d6d7a351`, `0708f305d6f`, `d9967fa37ca` Wangao Wang | Encoder **scale support**, crop-offset handling, format alignment — the "scaling case" in `iris_venc.c` |
| 2026-03-13 | Qualcomm | `95a337f92f0` Vikash Garodia | Iris: switch to hardware mode after firmware boot |
| 2026-03-27 | Qualcomm | `55ee57c12`→`8f100f5896`, `3e0b2053751` Dmitry Baryshkov | **Venus/Iris switch flipped** (Iris wins for SM8250/SC7280); `H265D_MAX_SLICE` fix for SC7280 |
| 2026-03-29 | Qualcomm | `2c7fe9db118`, `53a5e095636`, `96360a89419` | Firmware description split from platform data (basis of Gen1/Gen2 selection) |
| 2026-05-12 | Qualcomm | `e4d067d5e99`, `6870381285b`, `e0507d45b3c`, `89797288272` Wangao Wang | Encoder: B-frames, long-term reference, hierarchical coding, Gen1 intra refresh |
| 2026-05-29 / 06-10 | Radxa / Qualcomm | `9c962d3aa` / `412a2e5955e` Dikshita Agarwal | **Gen2 firmware autodetect and fallback** (what `iris_detect_firmware()` implements) |
| 2026-06-08 | Radxa | `631429e04` Xilin Wu; `b1446e9db` | kodiak: **`dma-coherent`** for Venus/Iris; Q6A switches to Gen2 firmware with a 7 MiB carveout |
| 2026-07-10 | Qualcomm / Radxa | `6eec0f9b5c3` / `921f79c41` Gourav Kumar | Gen2 encoder: **disable time-delta-based rate control** (VBR came out ~5× the configured bitrate) |
| 2026-08-02…12 | Radxa | `87c604bff` `08282c7c5` `4fef0d63f` `2ac17acf5` `d655943bf` `d5cdbbdaa` `6198771c7` `4e94efbb5` `96982befa` `a5ca5d13e` `fe41e5e55` `b71f2586a` Junhao Xie | Encoder + teardown hardening (Radxa PR #593): headers, visible-vs-aligned size, force-keyframe, padded NV12, serialized system-error teardown, `synchronize_irq()` before freeing queues |
| 2026-08-18 | Radxa | `38befa2de`, `f50b9f96c` Vishnu Reddy | Frame-interval enumeration fix (GStreamer); power-off ordering fix |
| 2026-09-08 | Radxa | `cb13f7a5f` `db2bdd1b1` `3934e935b` Xilin Wu | Gen2 decoder features (deblock filter, buffer pools, decode order) |
| 2026-03 → 08 | Qualcomm | see §4 | Stability fixes **after** the 7.0.11 base — not in Radxa's tree |

### Venus on the old kernel (6.18.2 branch, for context)
`9799c23c7` (2025-10-09, Xilin Wu): Radxa commented out the Venus "HW can't support this load" error and the 10-bit gating. Those two hacks are **still present in 7.0.11's Venus** (`pm_helpers.c`, `vdec.c`); with Iris enabled they are not on the SC7280 path. They are catalogued in `FIXES.md` §5, deliberately not changed.

## 3. What Radxa carries that Qualcomm main does not
27 of the 74 Iris/Venus commits in Radxa's tree have no same-subject commit in Qualcomm main (one of the 27 is a merge commit). Reworded or backported-from-elsewhere commits would also show here, so read it as "Radxa-side". Full data: [`data/radxa-7.0.11-iris-venus-commits.tsv`](data/radxa-7.0.11-iris-venus-commits.tsv) (74) and [`data/radxa-7.0.11-video-related-commits.tsv`](data/radxa-7.0.11-video-related-commits.tsv) (104, includes DT/SCM/IOMMU).

| Theme | Commits |
|---|---|
| EL2 / no-TZ boot | `395349af3` |
| Encoder correctness (Gen1 + Gen2) | `87c604bff` joined headers · `08282c7c5` visible vs aligned size · `4fef0d63f` force keyframe · `2ac17acf5` padded NV12 selection · `d655943bf` empty drain packet · `038eff48c` SC8280XP headers |
| Teardown / sync (**the code the new patches adapt**) | `6198771c7` wait for teardown · `4e94efbb5` serialize system-error teardown · `96982befa` harden core init state · `a5ca5d13e` `synchronize_irq()` before freeing queues · `d5cdbbdaa` failed-session cleanup · `fe41e5e55` defer DPB destruction · `b71f2586a` timestamp index wrap |
| Power | `f50b9f96c` power-domain-after-clocks ordering · `fb6b5b4d8` FPS / VPP overhead in the clock formula |
| Enumeration | `38befa2de` frame intervals for non-divisor rates · `55e5ed133` bit-depth validation |
| Decoder features | `cb13f7a5f` `db2bdd1b1` `3934e935b` `5d3934d3d` `84032bd73` `63d9ee1ce` |
| Diagnostics | `595c088c7` print the firmware SFR message on system error |
| Other SoC | `764b9bb72` SC8280XP platform data |

## 4. Qualcomm main: fixes after the 7.0.11 base and what happened to each
The 25 rows of [`data/qcom-main-fixes-missing-in-radxa-7.0.11.tsv`](data/qcom-main-fixes-missing-in-radxa-7.0.11.tsv) are every `Fixes:`-tagged commit in Qualcomm main that touches the video/SC7280 neighbourhood and is not in Radxa's tree. The query is path-based, so some rows are unrelated (NXP, IMX678, sun4i, PCIe, ICE) — they are listed in the data file for completeness and ignored below.

| Upstream commit | Subject | Disposition in the patch series |
|---|---|---|
| `0ac05c4d9f1` | Fix bitmask test in `iris_allow_cmd()` | **Applied** as-is (patch 1) |
| `460d3257a6d` | state-change debug log printed the stale value | **Applied** as-is (2) |
| `5eebacbc9a3` | missing `hfi_id` in Gen1 `GOP_SIZE` cap | **Applied** as-is (3) — matters for Gen1 encoding |
| `94ef75095d5` | distinct `bus_info` for encoder and decoder | **Applied** as-is (4) |
| `bd595b745eb`, `a51cea23e40` | Venus parser payload sizes | **Applied** as-is (5, 6) |
| `c53e0550288`, `35428ae3a6a` | Venus encoder frame/blur step 16 → 1 (v6 / v4) | **Applied** as-is (7, 8) |
| `f87d7eda07f` | runtime-PM reference leaks | **Adapted** (9): the 7.0.11 context differs |
| `75d79879ec3` | resume failure in core deinit | **Adapted** (14) and extended (the no-TZ CPU reset is a register write) |
| `b9c2215bded` | `disable_irq()` at power-off | **Not applied; replaced** by patch 13. On this tree it calls `disable_irq()` with `core->lock` held while the IRQ thread needs that lock (deadlock), and it still waits only after the clocks are off. Evidence: [`tests/irq-model`](tests/irq-model/irq_model.c) |
| `727a87c71b4` | duplicate `HFI_PROP_OPB_ENABLE` in a dispatch table | Not applied: in 7.0.11 the loop `break`s at the first match, the second entry is dead code (cosmetic) |
| `8afb9283017` | Kconfig guard for `QCOM_UBWC_CONFIG` | Not applicable: depends on a Kconfig change that is not in 7.0.11 |
| `e1c9adabb26` | drop extra NV12 padding (Venus) | Not applied: changes buffer sizes, Venus only, not needed for the goal |
| `3eb9ba0da0a` | revert "correct supported codecs for sc7280" (re-enable VP8) | Not applied: Venus only; VP8 is not part of the goal |
| `6eec0f9b5c3` | disable time-delta rate control (Gen2 encoder) | Already in Radxa as `921f79c41` |
| SCM probe fixes `9941fe8a04f` `b697b20cea4` `966d23c7e68` | tzmem / reserved memory / IRQ-before-publish | Adjacent (secure-world driver), not video; not applied |

## 5. Repository-wide search results that shaped the diagnosis
* Iris on SC7280 is **PAS-only upstream** (`01022af2d21`, `ab4e6a16f97`); Radxa's no-TZ port (`395349af3`) is what makes EL2 work.
* The SID `0x2184` "secure non-pixel" stream (`82066cdb176`, `ea2e2ea551a`) is the documented TrustZone-mode hazard for encode; Radxa's EL2 overlay (`qcs6490-radxa-kvm.dtso`) adds SIDs `0x2180`+`0x2184` and the firmware SID `0x21a2`.
* Iris hands the firmware the buffer timestamp in **nanoseconds** (`iris_common.c:24`, Radxa 7.0.11), Venus hands it **microseconds** (`venus/helpers.c:509-510`, Qualcomm main). A subject search of the three histories found no commit that changes this. Recorded as an open question in `FIXES.md` §5.

## 6. Data files
| File | Rows | Content |
|---|---|---|
| [`data/qcom-main-iris-venus-commits.tsv`](data/qcom-main-iris-venus-commits.tsv) | 286 | Every Venus/Iris commit in Qualcomm main back to 2023-12 (sha, date, author, subject) |
| [`data/radxa-7.0.11-iris-venus-commits.tsv`](data/radxa-7.0.11-iris-venus-commits.tsv) | 74 | Iris/Venus commits in Radxa's tree since 2026-03-20 |
| [`data/radxa-7.0.11-video-related-commits.tsv`](data/radxa-7.0.11-video-related-commits.tsv) | 104 | Same plus SC7280 DT / SCM / IOMMU commits |
| [`data/qcom-main-fixes-missing-in-radxa-7.0.11.tsv`](data/qcom-main-fixes-missing-in-radxa-7.0.11.tsv) | 25 | `Fixes:`-tagged Qualcomm commits not in Radxa's tree, with the commit each one fixes and whether it is `Cc: stable` |

Reproduce: `git log --format='%h|%ad|%an|%s' --date=short -- drivers/media/platform/qcom/iris drivers/media/platform/qcom/venus` in each tree.
