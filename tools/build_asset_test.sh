#!/usr/bin/env bash
# Build the standalone C++ asset-loader verifier (tools/soh3d_asset_test.cpp).
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
A="$REPO/Shipwright/soh/src/soh3d/asset"
mkdir -p "$REPO/scratch/bin"
g++ -std=c++17 -O2 -Wall -o "$REPO/scratch/bin/asset_test" \
    "$REPO/tools/soh3d_asset_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zar.cpp" "$A/cmb.cpp" "$A/csab.cpp" "$A/pica_texture.cpp"
echo "built $REPO/scratch/bin/asset_test"

# ZSI (scene/room) parser verifier — cross-checks tools/zsi.py.
g++ -std=c++17 -O2 -Wall -o "$REPO/scratch/bin/zsi_test" \
    "$REPO/tools/soh3d_zsi_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zsi.cpp" "$A/cmb.cpp" "$A/pica_texture.cpp"
echo "built $REPO/scratch/bin/zsi_test"
