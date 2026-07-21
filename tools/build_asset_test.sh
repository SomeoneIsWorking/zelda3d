#!/usr/bin/env bash
# Build the standalone C++ asset-loader verifier (tools/zelda3d_asset_test.cpp).
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# asset parsers moved to the shared cmb3d layer (Shipwright/cmb3d/asset) in the reorg.
A="$REPO/Shipwright/cmb3d/asset"
mkdir -p "$REPO/scratch/bin"
g++ -std=c++17 -O2 -Wall -I"$REPO/Shipwright/cmb3d" -o "$REPO/scratch/bin/asset_test" \
    "$REPO/tools/zelda3d_asset_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zar.cpp" "$A/cmb.cpp" "$A/csab.cpp" "$A/pica_texture.cpp"
echo "built $REPO/scratch/bin/asset_test"

# ZSI (scene/room) parser verifier — cross-checks tools/zsi.py.
g++ -std=c++17 -O2 -Wall -I"$REPO/Shipwright/cmb3d" -o "$REPO/scratch/bin/zsi_test" \
    "$REPO/tools/zelda3d_zsi_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zsi.cpp" "$A/cmb.cpp" "$A/pica_texture.cpp" "$A/lzs.cpp"
echo "built $REPO/scratch/bin/zsi_test"

# Room-geometry well-formedness check (OoT3D reference vs MM3D under test).
g++ -std=c++17 -O2 -Wall -I"$REPO/Shipwright/cmb3d" -o "$REPO/scratch/bin/room_geom_test" \
    "$REPO/tools/zelda3d_room_geom_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zsi.cpp" "$A/cmb.cpp" "$A/pica_texture.cpp" "$A/lzs.cpp"
echo "built $REPO/scratch/bin/room_geom_test"

# Scene-collision layout check (OoT3D reference vs MM3D under test). Validates the format's own
# invariants (plane identity, face normal) rather than assuming MM3D shares OoT3D's layout.
g++ -std=c++17 -O2 -Wall -I"$REPO/Shipwright/cmb3d" -o "$REPO/scratch/bin/collision_test" \
    "$REPO/tools/zelda3d_collision_test.cpp" \
    "$A/ctr_rom.cpp" "$A/zcol.cpp" "$A/lzs.cpp"
echo "built $REPO/scratch/bin/collision_test"

# MM3D collision LAYOUT derivation aid (reads the collision command offset from the ZSI header).
g++ -std=c++17 -O2 -Wall -I"$REPO/Shipwright/cmb3d" -o "$REPO/scratch/bin/collision_layout" \
    "$REPO/tools/zelda3d_collision_layout.cpp" \
    "$A/ctr_rom.cpp" "$A/zsi.cpp" "$A/cmb.cpp" "$A/pica_texture.cpp" "$A/lzs.cpp"
echo "built $REPO/scratch/bin/collision_layout"
