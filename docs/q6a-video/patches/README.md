# Patch series (exported copy)

25 commits on top of Radxa `linux-7.0.11` @ `a50eb5b71` (the commit the `src` submodule of this repo is pinned to).
**The canonical copy is the branch `claude/q6a-iris-venus-fixes` of `Namitjain07/kernel`** (and its pull request);
these files are the same commits exported with `git format-patch`, kept here so the series travels with the research.

`../FIXES.md` explains every patch. **Nothing in this series has been run on hardware.**

## Apply to a kernel tree
```bash
cd <kernel checkout at a50eb5b71 / radxa linux-7.0.11>
git am --3way $(ls /path/to/docs/q6a-video/patches/0*.patch | grep -v cover-letter)
```
Cherry-picked upstream commits keep their original author and carry a `(cherry picked from commit …)` line.

## Use with the Radxa packaging in this repo (not activated)
`debian/patches/linux/*.patch` use paths prefixed with `src/`. To produce that flavour from the kernel branch:
```bash
cd <kernel checkout>
git format-patch --src-prefix=a/src/ --dst-prefix=b/src/ -N -o <linux-qcom>/debian/patches/linux a50eb5b71..claude/q6a-iris-venus-fixes
# then append the file names (without the cover letter) to <linux-qcom>/debian/patches/series and `make deb`
```
These were deliberately **not** added to `debian/patches/series`: building a package from an untested series is a decision for the human.

## Build check used
```bash
make -C <kernel> O=<out> ARCH=arm64 LLVM=1 W=1 -j"$(nproc)" drivers/media/platform/qcom/iris/ drivers/media/platform/qcom/venus/
```
with `CONFIG_VIDEO_QCOM_IRIS=m` (Radxa `defconfig` default) for every commit, and with `CONFIG_VIDEO_QCOM_IRIS=n` at the tip so the SC7280 Venus resources compile too.
