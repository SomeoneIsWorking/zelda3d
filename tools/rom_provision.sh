#!/usr/bin/env bash
# Shared ROM provisioning for SoH3D launch scripts. Source this, then call:
#
#   soh3d_provision_roms "$REPO" [SOH_APP_DIR]
#
# Resolves the two ROMs SoH3D needs, in priority order, and exports them:
#   1. an already-set environment variable (SOH3D_3DS_ROM / SOH3D_N64_ROM)
#   2. a gitignored `.env` next to the repo root
#   3. a ROM file dropped into the repo root — ANY name, matched by extension
#      (preferring the canonical oot3d.3ds / oot.z64). This is the "just drop the
#      file in and run" path the repo rule asks for.
#
# - SOH3D_3DS_ROM (decrypted OoT3D .3ds): required by SoH3D asset rendering.
# - SOH3D_N64_ROM (.z64/.n64/.v64): only needed the first time, to extract oot.o2r.
#   When an app dir is given and it has no extracted N64 archive yet, the discovered
#   N64 ROM is symlinked into it so SoH's first-run extractor (which scans its own
#   app dir) picks it up automatically.
#
# No copyrighted asset or machine path is ever committed — this only locates files
# the user dropped locally.

# Pick the first existing file matching the given glob patterns. Globs that match
# nothing expand to the literal pattern, which the `[ -f ]` guard rejects.
_soh3d_first_file() {
    local f
    for f in "$@"; do
        [ -f "$f" ] && { printf '%s\n' "$f"; return 0; }
    done
    return 1
}

# True if the given dir already holds extracted N64 assets (oot*.o2r / oot*.otr).
_soh3d_have_n64_assets() {
    local d="$1" f
    for f in "$d"/oot*.o2r "$d"/oot*.otr; do
        [ -f "$f" ] && return 0
    done
    return 1
}

soh3d_provision_roms() {
    local repo="$1" sohdir="${2:-}"

    [ -f "$repo/.env" ] && . "$repo/.env"

    if [ -z "${SOH3D_3DS_ROM:-}" ]; then
        SOH3D_3DS_ROM="$(_soh3d_first_file "$repo/oot3d.3ds" "$repo"/*.3ds || true)"
    fi
    if [ -z "${SOH3D_N64_ROM:-}" ]; then
        SOH3D_N64_ROM="$(_soh3d_first_file "$repo/oot.z64" "$repo"/*.z64 "$repo"/*.n64 "$repo"/*.v64 || true)"
    fi

    [ -n "${SOH3D_3DS_ROM:-}" ] && export SOH3D_3DS_ROM
    [ -n "${SOH3D_N64_ROM:-}" ] && export SOH3D_N64_ROM

    # First-run N64 asset extraction: SoH scans its own app dir for a *.z64/.n64/.v64.
    # If no extracted archive exists yet but we found an N64 ROM, expose it there.
    if [ -n "$sohdir" ] && [ -n "${SOH3D_N64_ROM:-}" ] && ! _soh3d_have_n64_assets "$sohdir"; then
        ln -sf "$SOH3D_N64_ROM" "$sohdir/$(basename "$SOH3D_N64_ROM")" 2>/dev/null || true
    fi
}
