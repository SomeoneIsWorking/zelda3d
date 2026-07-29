#!/usr/bin/env bash
# Rasterize the crisp HUD counter icons — rupee gem (gRupeeCounterIconTex, always-on bottom-left),
# small key (gSmallKeyCounterIconTex, dungeons) and clock (gClockIconTex, timers) — to grayscale-on-
# transparent PNGs and embed them as a C header the HUD loads at runtime (stbi_load_from_memory ->
# persistent RGBA32). Backlog #31 (UI textures); mirror of tools/zelda3d_gen_button_tex.sh /
# zelda3d_gen_hud_tex.sh / zelda3d_gen_digit_tex.sh (the reusable SVG->texture pipeline).
#   tools/zelda3d_gen_counter_icons.sh
# Requires ImageMagick (magick/convert) with the librsvg delegate.
#
# Combine semantics these icons rely on: the rupee + small-key draws are MODULATEIA_PRIM
# (Gfx_SetupDL_39Overlay: out.rgb = TEXEL0.rgb*PRIM, out.a = TEXEL0.a*PRIM) and PRIM carries the
# tint colour (the rupee colour rColor; the key white), so a GRAYSCALE icon (rgb = bevel/facet
# intensity, a = coverage) tints IDENTICALLY to the original 16x16 IA8 texture — the 3D facet look
# comes from the intensity, the colour from PRIM. The clock draw is MODULATERGBA_PRIM with PRIM
# white, so its grayscale shows directly. Output is RGBA32 grayscale fed raw to
# gDPLoadTextureBlock(G_IM_FMT_RGBA,32b) (the #31/#32 raw-in-RAM HUD path).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="$REPO/scratch/svg"; mkdir -p "$OUTDIR"
HDR="$REPO/Shipwright/soh/src/zelda3d/assets/counter_icon_png.h"
# 64x64 (4x the N64 16x16): RGBA32 = 16KB, within the proven HUD-texture load envelope. These are
# FULL-LOAD single draws (no shared-tile reuse like gButtonBackgroundTex), so no dsdx tuning gotcha.
DIM=64

RASTER=(magick); command -v magick >/dev/null 2>&1 || RASTER=(convert)

# --- rupee gem (iconic faceted Zelda rupee crystal, tilted to match the N64 icon) ---
cat > "$OUTDIR/counter_rupee.svg" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED crisp rupee counter icon. Grayscale = facet bevel intensity; the rupee colour is
     applied by the HUD combine via PRIM. See tools/zelda3d_gen_counter_icons.sh. -->
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 16 16">
  <g transform="rotate(22 8 8)" stroke="#262626" stroke-width="0.3" stroke-linejoin="round">
    <polygon points="8,4.0 13.4,7.0 8,15.0" fill="#5e5e5e" stroke="none"/>
    <polygon points="8,4.0 8,15.0 2.6,7.0" fill="#8e8e8e" stroke="none"/>
    <polygon points="5.4,2.6 10.6,2.6 13.4,7.0 8,4.0 2.6,7.0" fill="#d6d6d6" stroke="none"/>
    <polygon points="5.4,2.6 10.6,2.6 8,4.0" fill="#eaeaea" stroke="none"/>
    <polygon points="5.4,2.6 10.6,2.6 13.4,7.0 8,15.0 2.6,7.0" fill="none"/>
    <polyline points="2.6,7.0 8,4.0 13.4,7.0" fill="none" stroke="#3a3a3a" stroke-width="0.25"/>
    <line x1="8" y1="4.0" x2="8" y2="15.0" stroke="#3a3a3a" stroke-width="0.25"/>
  </g>
</svg>
EOF

# --- small key (skeleton key: round bow top-right, blade with teeth lower-left) ---
cat > "$OUTDIR/counter_key.svg" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED crisp small-key counter icon. Grayscale (a=coverage); PRIM tints. -->
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 16 16">
  <g stroke="#202020" stroke-width="0.4" stroke-linejoin="round" stroke-linecap="round">
    <line x1="10.2" y1="6.2" x2="3.3" y2="13.1" stroke="#cfcfcf" stroke-width="1.7"/>
    <line x1="10.2" y1="6.2" x2="3.3" y2="13.1" stroke="#8a8a8a" stroke-width="0.7"/>
    <line x1="4.7" y1="11.7" x2="6.4" y2="13.4" stroke="#cfcfcf" stroke-width="1.4"/>
    <line x1="3.3" y1="13.1" x2="5.0" y2="14.8" stroke="#cfcfcf" stroke-width="1.4"/>
    <circle cx="11.4" cy="5.0" r="3.7" fill="#cfcfcf"/>
    <circle cx="11.4" cy="5.0" r="1.7" fill="none" stroke="#303030" stroke-width="0.9"/>
  </g>
</svg>
EOF

# --- clock (pocket-watch face: round body + crown loop + two hands) ---
cat > "$OUTDIR/counter_clock.svg" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED crisp clock/timer counter icon. Grayscale shown directly (PRIM white). -->
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 16 16">
  <g stroke-linejoin="round" stroke-linecap="round">
    <rect x="7.0" y="0.3" width="2.0" height="2.2" rx="0.4" fill="#9a9a9a"/>
    <circle cx="8.0" cy="1.0" r="1.4" fill="none" stroke="#b0b0b0" stroke-width="0.8"/>
    <circle cx="8.0" cy="8.8" r="6.6" fill="#7d7d7d" stroke="#303030" stroke-width="0.5"/>
    <circle cx="8.0" cy="8.8" r="5.4" fill="#bdbdbd" stroke="#5a5a5a" stroke-width="0.5"/>
    <circle cx="8.0" cy="8.8" r="4.6" fill="#dcdcdc"/>
    <line x1="8.0" y1="8.8" x2="8.0" y2="5.2" stroke="#303030" stroke-width="0.8"/>
    <line x1="8.0" y1="8.8" x2="11.0" y2="10.6" stroke="#303030" stroke-width="0.8"/>
    <circle cx="8.0" cy="8.8" r="0.7" fill="#303030"/>
  </g>
</svg>
EOF

emit_array () { # $1=svg $2=cname
  local svg="$1" cname="$2" png="${1%.svg}.png"
  "${RASTER[@]}" -background none "$svg" -resize ${DIM}x${DIM}\! "$png"
  printf 'static const unsigned char %s[] = {\n' "$cname"
  od -An -v -tu1 "$png" | tr -s ' ' | sed 's/^ //; s/ /,/g; s/$/,/'
  printf '};\n'
  printf 'static const unsigned int %sLen = sizeof(%s);\n' "$cname" "$cname"
}

{
  echo "// GENERATED by tools/zelda3d_gen_counter_icons.sh — do not edit."
  echo "// Raw PNG bytes of the crisp HUD counter icons (rupee gem / small key / clock; grayscale+"
  echo "// alpha); decoded at runtime via stbi_load_from_memory into persistent RGBA32. Backlog #31."
  echo "#pragma once"
  emit_array "$OUTDIR/counter_rupee.svg" kRupeeIconPng
  emit_array "$OUTDIR/counter_key.svg"   kSmallKeyIconPng
  emit_array "$OUTDIR/counter_clock.svg" kClockIconPng
} > "$HDR"

echo "rasterized 3 counter icons; wrote $HDR (DIM=$DIM)"
