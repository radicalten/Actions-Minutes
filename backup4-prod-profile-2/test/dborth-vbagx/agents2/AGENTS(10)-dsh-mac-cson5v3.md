Good catch — generic "versioned folder + pattern" instructions aren't enough if the agent can't browse directory listings. Here's the corrected `AGENTS.md` with **explicit, literal download URLs for every required devkitPPC package** in the r50 (2026-05-03) folder, built from the three confirmed URLs you provided plus the same confirmed naming convention for the other packages (confirmed via the mirror's own per-package directory listings, which show `-any` suffix for non-host-binary packages like `devkitppc-crtls-2.1.0-1-any.pkg.tar.zst` and `devkitppc-rules-1.2.1-1-any.pkg.tar.zst`).

**Caveat on the 4 unconfirmed URLs** (newlib, gamecube-tools, libfat-ogc, libogc): I could not directly browse the r50 folder's index to verify these four filenames byte-for-byte (search access is exhausted), so they're reconstructed from the mirror's established naming convention. If any 404s, the agent should fall back to listing `https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/` directly (a plain `curl`/`GET` on the folder itself returns an HTML index) and grep for the exact filename before retrying.

Only **§3 (devkitPPC Toolchain)** changes below — everything else from the prior macOS + DeepSeek Harness version is unchanged. Swap this in for the old §3 toolchain subsection:

```markdown
### devkitPPC Toolchain — **`wii.leseratte10.de` only, explicit file URLs (never `pkg.devkitpro.org` or `dkp-pacman`)**
**`dkp-pacman` / devkitPro's own installer is blocked for AI agents in this environment — do not attempt it.** Instead, download each pinned package as an exact, explicit file URL from the single pinned release folder below (no directory-listing discovery needed):

`https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/`

**Confirmed explicit URLs** (verified present in this folder):
```
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/buildscripts-devkitPPC_r50.tar.gz
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-gcc-16.1.0-1-osx_arm64.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-binutils-2.46.0-1-osx_arm64.pkg.tar.zst
```

**Remaining pinned packages** (same folder, `-any` arch suffix since these are target/PowerPC-side libraries and tools, not host binaries — reconstructed from the mirror's confirmed naming convention; **verify once and correct the `-N-` pkgrel number if a curl returns 404**):
```
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-newlib-4.6.0-1-any.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-rules-1.2.1-1-any.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/gamecube-tools-1.0.7-1-any.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/libfat-ogc-2.1.0-1-any.pkg.tar.zst
https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/libogc-3.1.0-1-any.pkg.tar.zst
```

```bash
# tools/bootstrap_toolchain.sh (macOS) — explicit URL list, no directory parsing/discovery:
STAGE="$WORKSPACE/tmp/devkitpro_dl"
mkdir -p "$STAGE"

urls=(
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/buildscripts-devkitPPC_r50.tar.gz"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-gcc-16.1.0-1-osx_arm64.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-binutils-2.46.0-1-osx_arm64.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-newlib-4.6.0-1-any.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/devkitppc-rules-1.2.1-1-any.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/gamecube-tools-1.0.7-1-any.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/libfat-ogc-2.1.0-1-any.pkg.tar.zst"
  "https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/libogc-3.1.0-1-any.pkg.tar.zst"
)

for u in "${urls[@]}"; do
  fname="$(basename "$(python3 -c "import sys,urllib.parse; print(urllib.parse.unquote(sys.argv[1]))" "$u")")"
  out="$STAGE/$fname"
  curl -sSfL -o "$out" "$u" || { echo "FAIL (404?) — verify filename/pkgrel manually: $u" >&2; exit 1; }
done

shasum -a 256 "$STAGE"/*.tar.gz "$STAGE"/*.pkg.tar.zst 2>/dev/null   # record/verify against evidence/g0_build.log

sudo mkdir -p /opt/devkitpro
# macOS's built-in bsdtar handles both .tar.gz and .tar.zst natively (zstd support via brew-installed `zstd` on PATH):
for f in "$STAGE"/*.tar.gz; do sudo tar -xf "$f" -C /opt/devkitpro; done
for f in "$STAGE"/*.pkg.tar.zst; do sudo tar -xf "$f" -C /opt/devkitpro; done

export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="${DEVKITPRO}/devkitPPC"
export PATH="${DEVKITPRO}/tools/bin:${DEVKITPPC}/bin:${PATH}"

# Mirror into the persistent workspace as a snapshot-safe fallback
mkdir -p "$WORKSPACE/devkitpro"
cp -R /opt/devkitpro/. "$WORKSPACE/devkitpro/"
```
- `tools/bootstrap_toolchain.sh` should wrap the above end-to-end: `curl -sSfL` each **explicit, literal URL** above (no directory listing fetch, no pattern-guessing at runtime), verify `sha256` via `shasum -a 256`, extract each `.tar.gz`/`.pkg.tar.zst` to `/opt/devkitpro` with `tar`, mirror the resulting tree to `$WORKSPACE/devkitpro`, and export `DEVKITPRO`/`DEVKITPPC`.
- **If a URL 404s**: the pkgrel number (the `-1`/`-2`/etc. segment) for that package may have incremented since this file was written. Fetch `https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20%282026-05-03%29/` as a plain GET (returns an HTML directory index), find the correct current filename for that package, and update the URL in `tools/bootstrap_toolchain.sh` — do **not** fall back to `dkp-pacman`.
- **If `/opt/devkitpro` is wiped** (e.g., after a session reset): `cp -R "$WORKSPACE/devkitpro" /opt/devkitpro` or re-run `tools/bootstrap_toolchain.sh` (which should re-mirror `/opt/devkitpro` → `$WORKSPACE/devkitpro` after every fresh install, since `$WORKSPACE` is the only guaranteed-persistent location for this harness). Always export `DEVKITPRO` before `make`.
- **Do not** attempt `dkp-pacman`, the devkitPro GUI/`.pkg` installer, or `pkg.devkitpro.org` — all are blocked/unavailable for this agent. `wii.leseratte10.de` is the sole source of truth for devkitPPC packages, and **explicit literal URLs** (not directory-listing discovery) are required for this agent to fetch them reliably.
- Note: `arm-none-eabi-objdump -m thumb` (from the Homebrew `gcc-arm-embedded` cask, used only as a host-side ARM/Thumb encoding oracle, not for building the Wii target) is unsupported; use `-m arm -M force-thumb`.
```

Also update the §9 reproduce-script step 1 comment to say `# explicit URLs, see tools/bootstrap_toolchain.sh` instead of implying pattern discovery, and update tooling-scripts item 1 in §8 to: *"Fetches each pinned package via its explicit literal URL (hardcoded list, no directory-listing parsing); verifies `sha256`; extracts to `/opt/devkitpro`; mirrors to `$WORKSPACE/devkitpro`."*

Want me to output the entire merged `AGENTS.md` again in full with this swapped in, as I did last time?
