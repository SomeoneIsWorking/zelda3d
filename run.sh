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
# required; run.sh inits the submodules for you). On every run it makes sure each engine submodule
# is properly registered (re-cloning it from scratch if it is half-present / broken) and otherwise
# UPDATES it to the commit THIS repo pins (the submodule gitlink) — but only FORWARD: if the local
# submodule HEAD already contains the pin (local commits in progress on top of it), it is left
# alone rather than rewound, so engine work-in-progress survives. (Plain `git submodule update`
# would silently orphan such detached-HEAD commits — it only aborts on uncommitted file edits, not
# on local commits — so run.sh guards against that itself.) So `git pull && ./run.sh` stays
# current. Skip with SOH3D_NOUPDATE=1.
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

# Bring ONE submodule to the commit its parent repo pins (the gitlink at the parent's HEAD),
# hard-resetting tracked files to it (discarding local edits to engine sources — the common cause
# of a stuck/stale checkout). It does NOT touch untracked files (so the build dir survives), and it
# leaves a checkout with local COMMITS ahead of the pin alone (dev work in progress on this
# machine). Args: <parent_dir> <subpath> <label>.
sync_submodule_to_pin() {
    local parent="$1" subpath="$2" label="$3"
    local sub="$parent/$subpath" want cur
    want="$(git -C "$parent" rev-parse --verify --quiet "HEAD:$subpath" 2>/dev/null || true)"
    [ -z "$want" ] && { echo "  $label: cannot resolve pinned commit — skipping" >&2; return 0; }

    # Is the submodule PROPERLY REGISTERED? It must (a) have a configured URL in the parent's git
    # config (i.e. `git submodule init` has run for it) AND (b) be a real git working tree on disk.
    # If either is missing the checkout is half-present / broken (empty dir, stray clone, deinitialised
    # gitlink) → delete it and clone it again from scratch. Otherwise just update it.
    local registered=1
    git -C "$parent" config --get "submodule.${subpath}.url" >/dev/null 2>&1 || registered=0
    git -C "$sub" rev-parse --is-inside-work-tree >/dev/null 2>&1 || registered=0

    if [ "$registered" = 0 ]; then
        echo "  $label: not properly registered as a submodule — deleting and cloning it again" >&2
        rm -rf "$sub"
        git -C "$parent" submodule update --init "$subpath" >&2 || {
            echo "error: failed to clone $label" >&2; exit 1; }
        return 0
    fi

    cur="$(git -C "$sub" rev-parse --verify --quiet HEAD 2>/dev/null || true)"
    [ "$cur" = "$want" ] && return 0

    # Make the pinned commit available locally so we can both reason about ancestry and check it
    # out. (The parent's pull may have advanced the gitlink past what this clone has fetched.)
    git -C "$sub" cat-file -e "${want}^{commit}" 2>/dev/null || git -C "$sub" fetch --quiet --all 2>/dev/null || true

    # CRITICAL: `git submodule update` does a *detached* checkout of the pin. With a clean working
    # tree it does NOT abort on local COMMITS — it silently moves HEAD to the pin, orphaning any
    # commits that aren't on a branch (the common case for a submodule). It only aborts on
    # uncommitted *file* edits. So the only safe guard for in-progress engine work is to detect,
    # ourselves, when the local HEAD is AHEAD of the pin and refuse to rewind it. If the pin is an
    # ancestor of the current HEAD, this checkout has local commits on top of the pin → leave it.
    if git -C "$sub" cat-file -e "${want}^{commit}" 2>/dev/null && \
       git -C "$sub" merge-base --is-ancestor "$want" "$cur" 2>/dev/null; then
        echo "  $label: HEAD ${cur:0:9} already contains pin ${want:0:9} (local commits ahead) — left as-is." \
             "(push the pin bump, or SOH3D_NOUPDATE=1, to silence.)" >&2
        return 0
    fi

    # Pin is newer than / diverged from the local HEAD (e.g. a `git pull` advanced the gitlink) and
    # the local checkout has no commits past it → advance to the pin. `git submodule update` still
    # aborts here if uncommitted file edits would be overwritten, so those are preserved.
    echo "  $label: updating ${cur:0:9} -> pinned ${want:0:9}" >&2
    git -C "$parent" submodule update "$subpath" >&2 || \
        echo "  $label: could not advance to ${want:0:9} — local edits in the way; left as-is." \
             "(commit or stash them to update; SOH3D_NOUPDATE=1 to silence.)" >&2
}

# Bring the engine checkout in line with the commits THIS repo pins (Shipwright gitlink, then the
# libultraship gitlink it records): re-clone a broken/unregistered submodule, otherwise update it
# to its pin. Reports each decision; never hard-resets, so local engine work is preserved.
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
# Cold boot to the TITLE SCREEN: a plain `./run.sh` powers on like a real console — title screen,
# file select, New Game, the real intro, normal save + normal day/night clock. The dev-warp shortcuts
# (skip title and teleport straight into a scene, with an optional clean save / pinned time) are
# OPT-IN, off by default — set them on the command line when you want to test a specific scene:
#   SOH3D_WARP=1 SOH3D_ENTRANCE=177 SOH3D_TIME=0x8000 SOH3D_COLDBOOT=1 ./run.sh   # warp to Market, noon, fresh save
export SOH3D_WARP="${SOH3D_WARP:-}"              # set 1 to auto-warp past title/file-select (dev)
export SOH3D_COLDBOOT="${SOH3D_COLDBOOT:-}"      # set 1 (with WARP) to start the warp save as a clean NEW game
export SOH3D_ENTRANCE="${SOH3D_ENTRANCE:-219}"  # warp target when SOH3D_WARP=1 (219 = Kakariko front gate)
export SOH3D_TIME="${SOH3D_TIME:-}"             # set e.g. 0x8000 to PIN time-of-day (else the game clock runs)
export SOH3D_AUTO="${SOH3D_AUTO:-1}"            # 1 = auto-replace non-table actors with OoT3D models
export SOH3D_N64ANIM="${SOH3D_N64ANIM:-1}"      # drive OoT3D skeletons from live N64 joints (req. for characters)
export SOH3D_VULKAN="${SOH3D_VULKAN:-1}"         # 1 = single Vulkan Fast3D backend (default); 0 = legacy OpenGL
# Skybox/HUD stripe corruption hunting: launch with SOH3D_GL_STATECHECK=1 ./run.sh — every SoH3D
# render pass then logs (stderr) any GL state it failed to restore. Off by default (per-frame glGet).
export SOH3D_GL_STATECHECK="${SOH3D_GL_STATECHECK:-}"

cd "$SOH"
exec "./$SOH_BIN" "$@"
