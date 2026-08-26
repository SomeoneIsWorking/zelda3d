# MM3D Player form-model routing

## Finding

MM Player could not reach a 3DS body through the generic object-name catalog. Retail MM3D stores each transformation's body in a dedicated `_new` actor archive, while the shared `zelda2_link_new.gar.lzs` archive contains animation data rather than a body CMB. Player also draws through `SkelAnime_DrawFlexLod`, the one flex walker that did not yet call the MM3D replacement seam.

The retail body identities are:

- Fierce Deity: `zelda2_link_boy_new.gar.lzs` / `link_demon`
- Goron: `zelda2_link_goron_new.gar.lzs` / `link_goron`
- Zora: `zelda2_link_zora_new.gar.lzs` / `link_zora`
- Deku: `zelda2_link_nuts_new.gar.lzs` / `link_deknuts`
- Human: `zelda2_link_child_new.gar.lzs` / `link_child`

Archive inventory found 1, 7, 3, 5, and 3 body CMBs respectively. The shared Link archive has zero CMBs and 847 CSABs.

## Ported scope

A focused policy owns transformation-to-archive/CMB identity. The catalog now supports explicit CMB selection instead of assuming the first CMB in an archive. `Zelda3D_TryDrawPlayer` stages the selected body and lets the stock Player draw continue, so its live skeleton and callbacks remain authoritative. The LOD flex walker consumes that pending draw and then re-walks the N64 skeleton under the existing side-effect guard, restoring display-list pointers afterward.

## Open evidence boundary

Geometry routing is grounded and covered by focused Clang tests. Animation selection still needs form-directory identity when resolving the shared 847-CSAB bank; equipment/hand meshes and a live rendered tour of all five forms remain unverified. `MM_ZELDA3D_LINK` therefore remains opt-in.
