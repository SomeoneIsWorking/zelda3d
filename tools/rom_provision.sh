#!/usr/bin/env bash
# Shared ROM provisioning for Zelda3D launch scripts. Source this, then call:
#
#   zelda3d_provision_roms "$REPO" [SOH_APP_DIR]
#
# Resolves the two ROMs Zelda3D needs, in priority order, and exports them:
#   1. an already-set environment variable (ZELDA3D_OOT3D_ROM / ZELDA3D_OOT_ROM)
#   2. a gitignored `.env` next to the repo root
#   3. a ROM file dropped into the repo root — ANY name, matched by extension
#      (preferring the canonical oot3d.3ds / oot.z64). This is the "just drop the
#      file in and run" path the repo rule asks for.
#
# - ZELDA3D_OOT3D_ROM (decrypted OoT3D .3ds): required by Zelda3D asset rendering.
# - ZELDA3D_OOT_ROM (.z64/.n64/.v64): only needed the first time, to extract oot.o2r.
#   When an app dir is given and it has no extracted N64 archive yet, the discovered
#   N64 ROM is symlinked into it so SoH's first-run extractor (which scans its own
#   app dir) picks it up automatically.
#
# No copyrighted asset or machine path is ever committed — this only locates files
# the user dropped locally.

# Pick the first existing file matching the given glob patterns. Globs that match
# nothing expand to the literal pattern, which the `[ -f ]` guard rejects.
_zelda3d_first_file() {
    local f
    for f in "$@"; do
        [ -f "$f" ] && { printf '%s\n' "$f"; return 0; }
    done
    return 1
}

# True if the given dir already holds extracted N64 assets (oot*.o2r / oot*.otr).
_zelda3d_have_n64_assets() {
    local d="$1" f
    for f in "$d"/oot*.o2r "$d"/oot*.otr; do
        [ -f "$f" ] && return 0
    done
    return 1
}

zelda3d_provision_roms() {
    local repo="$1" sohdir="${2:-}"

    [ -f "$repo/.env" ] && . "$repo/.env"

    if [ -z "${ZELDA3D_OOT3D_ROM:-}" ]; then
        ZELDA3D_OOT3D_ROM="$(_zelda3d_first_file "$repo/oot3d.3ds" "$repo"/*.3ds || true)"
    fi
    if [ -z "${ZELDA3D_OOT_ROM:-}" ]; then
        ZELDA3D_OOT_ROM="$(_zelda3d_first_file "$repo/oot.z64" "$repo"/*.z64 "$repo"/*.n64 "$repo"/*.v64 || true)"
    fi

    [ -n "${ZELDA3D_OOT3D_ROM:-}" ] && export ZELDA3D_OOT3D_ROM
    [ -n "${ZELDA3D_OOT_ROM:-}" ] && export ZELDA3D_OOT_ROM

    # First-run N64 asset extraction: SoH scans its own app dir for a *.z64/.n64/.v64.
    # If no extracted archive exists yet but we found an N64 ROM, expose it there.
    if [ -n "$sohdir" ] && [ -n "${ZELDA3D_OOT_ROM:-}" ] && ! _zelda3d_have_n64_assets "$sohdir"; then
        ln -sf "$ZELDA3D_OOT_ROM" "$sohdir/$(basename "$ZELDA3D_OOT_ROM")" 2>/dev/null || true
    fi
}
