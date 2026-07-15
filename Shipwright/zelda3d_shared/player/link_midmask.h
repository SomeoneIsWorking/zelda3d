// Link mesh-id visibility policy — game-agnostic. Consumes a LinkGear snapshot; returns
// the bitmask of childlink_v2.cmb / link_v2.cmb mesh_ids that should be VISIBLE this
// frame (each hand/back variant is baked as a distinct mesh_id, so the rig can render
// a state-dependent subset without duplicating geometry).
//
// Ports the OoT (soh) `LinkMidMask::boyMidMask` policy verbatim under a game-agnostic
// input. Per-game adapters read Player-native enums and translate to LinkGear.
//
// mesh_id map (bit index into the returned bitmask == CMB mesh_id):
//   see Shipwright/soh/src/zelda3d/player/link_mesh_id_map.md (ADULT section).
#ifndef ZELDA3D_SHARED_PLAYER_LINK_MIDMASK_H
#define ZELDA3D_SHARED_PLAYER_LINK_MIDMASK_H

#include "link_gear.h"

namespace Zelda3D {

// Adult / boy link_v2.cmb mesh-id mask. Bit N set iff mesh_id N is drawn this frame.
// Deterministic, pure — same input, same mask.
unsigned long long linkAdultMidMask(const LinkGear& gear);

} // namespace Zelda3D

#endif // ZELDA3D_SHARED_PLAYER_LINK_MIDMASK_H
