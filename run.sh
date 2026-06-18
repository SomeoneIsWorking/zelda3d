#!/usr/bin/env bash
# Boot SoH3D into Kakariko Village (OoT3D geometry) on the current desktop.
#
# Run from a terminal in your graphical session (DISPLAY/XAUTHORITY are inherited):
#   ./run.sh              # the game
#   ./run.sh tool         # the N64-vs-3DS character compare tool (charcompare)
#   ./run.sh tool /actor/zelda_zl4.zar   # ...starting on a specific character
#
# The decrypted OoT3D .3ds is provided via env SOH3D_3DS_ROM (never hardcoded — repo
# rule). Either export it, drop a `.env` next to this script setting it, or just drop
# the ROM file (any name) into the repo dir. The N64 .z64 (for first-run extraction)
# is picked up the same way. Override the warp target with SOH3D_ENTRANCE=<decimal>.
#
# On a fresh checkout this checks out the Shipwright engine submodule (+ its own submodules) and
# configures the build dir — so `git clone <soh3d> && ./run.sh` is all you need (no --recursive
# required; run.sh inits the submodules for you). On every run it brings the engine in line with the
# commit THIS repo pins (the submodule gitlink), reporting what it did or why it skipped — but never
# clobbering local engine work (commits ahead of the pin, or uncommitted changes). So
# `git pull && ./run.sh` stays current. Skip the check with SOH3D_NOUPDATE=1; force a hard re-sync
# to the pinned commit (discarding local engine state) with SOH3D_FORCE_ENGINE=1.
set -eu
REPO="$(cd "$(dirname "$0")" && pwd)"
BUILD="$REPO/Shipwright/build-cmake"
SOH="$BUILD/soh"
# The soh target's output name is set per-platform in soh/CMakeLists.txt.
case "$(uname)" in
    Darwin) SOH_BIN="soh-macos" ;;
    *)      SOH_BIN="soh.elf" ;;
esac
# Parallel job count — nproc is GNU-only (absent on macOS); fall back to sysctl, then 4.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# The engine is the `Shipwright` submodule of this repo (url = our fork, branch = develop), and it
# in turn has libultraship as a submodule. libultraship's recorded commit only exists in OUR fork
# while Shipwright's .gitmodules still points libultraship at upstream, so we override that nested
# URL when checking out (keeps the fork's .gitmodules unchanged for clean upstream merges).
LIBULTRA_FORK="https://github.com/SomeoneIsWorking/libultraship.git"

# Sync ONE submodule to the commit its parent repo pins (the gitlink at the parent's HEAD), and —
# crucially — report exactly what it decided, so a stale engine is never silent. Never clobbers
# local work: a checkout with commits AHEAD of the pin (my dev WIP) or uncommitted changes is left
# alone with an explanation. SOH3D_FORCE_ENGINE=1 overrides that and hard-resets to the pin.
# Args: <parent_repo_dir> <submodule_path> <label>.
sync_submodule_to_pin() {
    local parent="$1" subpath="$2" label="$3"
    local sub="$parent/$subpath" want cur
    want="$(git -C "$parent" rev-parse --verify --quiet "HEAD:$subpath" 2>/dev/null || true)"
    cur="$(git -C "$sub" rev-parse --verify --quiet HEAD 2>/dev/null || true)"
    if [ -z "$want" ] || [ -z "$cur" ]; then
        echo "  $label: cannot resolve pinned/current commit — skipping update" >&2
        return 0
    fi
    if [ "$cur" = "$want" ]; then
        echo "  $label: up to date (${want:0:9})" >&2
        return 0
    fi
    # The parent's `git pull` may have advanced the gitlink to a commit this checkout hasn't fetched.
    git -C "$sub" cat-file -e "${want}^{commit}" 2>/dev/null || git -C "$sub" fetch --quiet --all 2>/dev/null || true

    if [ -n "${SOH3D_FORCE_ENGINE:-}" ]; then
        echo "  $label: forcing ${cur:0:9} -> pinned ${want:0:9} (SOH3D_FORCE_ENGINE)…" >&2
        git -C "$sub" reset --hard "$want" >&2 2>/dev/null || true
        git -C "$parent" submodule update --init --force "$subpath" >&2 || \
            echo "  $label: ERROR force-updating to ${want:0:9}" >&2
        return 0
    fi
    # Commits BEYOND the pin = work in progress on this machine — never clobber.
    if git -C "$sub" merge-base --is-ancestor "$want" "$cur" 2>/dev/null; then
        echo "  $label: at ${cur:0:9}, AHEAD of the pinned ${want:0:9} — local commits, left as-is." >&2
        echo "         (commit+push, then bump the gitlink to publish; the build uses your local copy.)" >&2
        return 0
    fi
    # Uncommitted changes to tracked files = unsaved work — never clobber. THIS is the case that used
    # to make the update silently skip forever (a build that dirties a tracked file); now it says so.
    if ! git -C "$sub" diff --quiet --ignore-submodules=all 2>/dev/null \
       || ! git -C "$sub" diff --cached --quiet --ignore-submodules=all 2>/dev/null; then
        echo "  $label: at ${cur:0:9} but repo pins ${want:0:9}, and the tree has uncommitted changes —" >&2
        echo "         NOT auto-updating (would lose work). Re-run with SOH3D_FORCE_ENGINE=1 to discard," >&2
        echo "         or: git -C $sub reset --hard $want" >&2
        return 0
    fi
    # Clean and behind/diverged → move to the pinned commit.
    echo "  $label: ${cur:0:9} -> pinned ${want:0:9} (updating)…" >&2
    git -C "$parent" submodule update --init "$subpath" >&2 || \
        echo "  $label: ERROR updating to ${want:0:9} (offline, or the commit isn't on the remote?)" >&2
}

# Bring the engine checkout in line with the commits THIS repo pins (Shipwright gitlink, then the
# libultraship gitlink it records). Reports each decision; never clobbers local engine work.
update_engine_if_safe() {
    if [ -n "${SOH3D_NOUPDATE:-}" ]; then
        echo "engine: update skipped (SOH3D_NOUPDATE set)" >&2
        return 0
    fi
    echo "engine: checking it matches the pinned commits…" >&2
    sync_submodule_to_pin "$REPO" "Shipwright" "Shipwright"
    # libultraship's pinned commit only exists in OUR fork; configure that URL before syncing it
    # (Shipwright's own .gitmodules still points it at upstream).
    git -C "$REPO/Shipwright" config submodule.libultraship.url "$LIBULTRA_FORK" 2>/dev/null || true
    sync_submodule_to_pin "$REPO/Shipwright" "libultraship" "libultraship"
}

# Check out the Shipwright engine submodule (+ libultraship from our fork) if the sources aren't
# present. Only does the initial checkout here — an existing checkout is handled by
# update_engine_if_safe, which reports + never clobbers local engine work.
ensure_sources() {
    if [ -f "$REPO/Shipwright/CMakeLists.txt" ] && [ -f "$REPO/Shipwright/libultraship/CMakeLists.txt" ]; then
        update_engine_if_safe
        return 0
    fi
    echo "checking out Shipwright engine submodule (pinned commit)…" >&2
    git -C "$REPO" submodule sync Shipwright >&2 2>/dev/null || true
    git -C "$REPO" submodule update --init Shipwright >&2 || {
        echo "error: failed to check out the Shipwright submodule" >&2; exit 1; }
    # The recorded libultraship commit only exists in our fork; override the nested URL before its
    # recursive checkout (Shipwright's own .gitmodules still points at upstream).
    git -C "$REPO/Shipwright" config submodule.libultraship.url "$LIBULTRA_FORK"
    echo "initialising libultraship (from our fork)…" >&2
    git -C "$REPO/Shipwright" submodule update --init --recursive >&2 || {
        echo "error: submodule update (libultraship) failed" >&2; exit 1; }
}

# Ensure the engine is cloned, updated, configured, and the target is built+current.
# The build step always runs (ninja is a near-instant no-op when nothing changed), so an engine
# update is actually compiled. Pass: <target> <output-binary>.
ensure_built() {
    local target="$1" binary="$2"
    ensure_sources
    # Configure if the build dir has no generator file yet. A leftover CMakeCache.txt from a
    # FAILED earlier configure (e.g. -G Ninja before ninja was installed) counts as not-configured:
    # wipe the stale cache and configure cleanly, otherwise the build step finds no build.ninja.
    if [ ! -f "$BUILD/build.ninja" ]; then
        if [ -f "$BUILD/CMakeCache.txt" ]; then
            echo "build dir half-configured — wiping stale cache and reconfiguring…" >&2
            rm -f "$BUILD/CMakeCache.txt"; rm -rf "$BUILD/CMakeFiles"
        else
            echo "configuring build dir (first run)…" >&2
        fi
        cmake -S "$REPO/Shipwright" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >&2 || {
            echo "error: cmake configure failed" >&2; exit 1; }
    fi
    echo "building '$target' (this can take a while the first time)…" >&2
    cmake --build "$BUILD" --target "$target" -j"$NPROC" >&2 || {
        echo "error: build of '$target' failed" >&2; exit 1; }
    [ -x "$binary" ] || { echo "error: '$binary' still missing after build" >&2; exit 1; }
}

# The engine refuses to start without its runtime asset archive `soh.o2r` (custom fonts, UI
# textures, GL/Metal shaders) sitting next to the binary — on macOS it pops a blocking
# "Missing soh.o2r … Exiting" dialog. That archive is GENERATED from the in-tree custom assets
# (NOT from a game ROM), via the CMake `GenerateSohOtr` target (extract_assets.py --norom), which
# emits it to $BUILD/soh/soh.o2r — exactly where we cd before launching, so the binary finds it
# (Context::LocateFileAcrossAppDirs searches cwd / app dir). We always launch with cwd=$SOH, so
# placing it there satisfies both macOS and Linux. Never commit it: it's a build product.
# Idempotent: skip if already present (the target itself isn't dependency-tracked for staleness,
# so don't rebuild it every run — only when the file is actually missing).
ensure_soh_o2r() {
    [ -f "$SOH/soh.o2r" ] && return 0
    echo "soh.o2r missing — generating runtime asset archive (GenerateSohOtr)…" >&2
    cmake --build "$BUILD" --target GenerateSohOtr -j"$NPROC" >&2 || {
        echo "error: failed to generate soh.o2r (GenerateSohOtr)" >&2; exit 1; }
    [ -f "$SOH/soh.o2r" ] || { echo "error: soh.o2r still missing after GenerateSohOtr" >&2; exit 1; }
}

# ROM provisioning: env -> gitignored .env -> any *.3ds / *.z64 dropped in the repo dir.
. "$REPO/tools/rom_provision.sh"
soh3d_provision_roms "$REPO" "$SOH"
if [ -z "${SOH3D_3DS_ROM:-}" ]; then
    echo "error: no OoT3D .3ds found — set SOH3D_3DS_ROM, add ./.env, or drop a *.3ds into $REPO" >&2
    exit 1
fi

# If no graphical session is inherited (e.g. launched over SSH/headless terminal), fall back
# to the primary local X display. Linux/X11 only — on macOS the window backend is Cocoa and
# forcing DISPLAY=:0 would wrongly push SDL onto an X11 path.
if [ "$(uname)" = "Linux" ] && [ -z "${DISPLAY:-}" ]; then
    export DISPLAY=:0
    [ -z "${XAUTHORITY:-}" ] && for x in /run/user/$(id -u)/xauth_*; do
        [ -f "$x" ] && export XAUTHORITY="$x" && break
    done
fi

# Subcommand: `./run.sh tool [args]` -> the charcompare character-compare tool.
if [ "${1:-}" = "tool" ]; then
    shift
    CC="$SOH/charcompare/charcompare"
    ensure_built charcompare "$CC"
    ensure_soh_o2r          # tool warns (GUI font / GL shaders) without it
    cd "$SOH"
    exec ./charcompare/charcompare "$@"
fi

ensure_built soh "$SOH/$SOH_BIN"
ensure_soh_o2r              # game hard-exits with a "Missing soh.o2r" dialog without it

export SOH3D=1                                  # render OoT3D assets
export SOH3D_WARP=1                             # auto-warp past title/file-select
export SOH3D_ENTRANCE="${SOH3D_ENTRANCE:-219}"  # 219 = Kakariko Village front gate
export SOH3D_TIME="${SOH3D_TIME:-0x8000}"       # 0x8000 = noon (force DAY; Kakariko else loads at night)
export SOH3D_AUTO="${SOH3D_AUTO:-1}"            # 1 = auto-replace non-table actors with OoT3D models
export SOH3D_N64ANIM="${SOH3D_N64ANIM:-1}"      # drive OoT3D skeletons from live N64 joints (req. for characters)
export SOH3D_VULKAN="${SOH3D_VULKAN:-1}"         # 1 = single Vulkan Fast3D backend (default); 0 = legacy OpenGL
# Skybox/HUD stripe corruption hunting: launch with SOH3D_GL_STATECHECK=1 ./run.sh — every SoH3D
# render pass then logs (stderr) any GL state it failed to restore. Off by default (per-frame glGet).
export SOH3D_GL_STATECHECK="${SOH3D_GL_STATECHECK:-}"

cd "$SOH"
exec "./$SOH_BIN" "$@"
