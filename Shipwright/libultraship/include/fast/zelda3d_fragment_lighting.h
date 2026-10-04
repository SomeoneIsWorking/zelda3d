// PICA fixed-function FRAGMENT lighting: the per-light bank transport (render.cmb-fragment-lighting).
//
// WHAT THIS IS. PICA's fragment unit adds two colours to the TEV chain that the vertex unit never
// produces: FRAGMENT_PRIMARY (0x6210) and FRAGMENT_SECONDARY (0x6211). Both are produced by one
// function over two inputs:
//   * the MATERIAL colour block, authored in the CMB at +0xA0 emission / +0xA4 ambient /
//     +0xA8 diffuse / +0xAC specular_0 / +0xB0 specular_1, which arrives here as
//     Zelda3DGlGroup::matEmission / matAmbient / matDiffuse / matSpecular0 / matSpecular1; and
//   * the per-LIGHT records, whose four colour triples and direction PICA reads from the
//     `LightSrc` block at register 0x140 + slot*0x10. Those are THIS header's payload.
// `FUN_003fa5d0` is the only writer of that payload on a live draw: for each enabled light it
// forms ambient = matAmbient*lightAmbient, diffuse = matDiffuse*lightDiffuse,
// specular_0 = matSpecular0*lightSpecular0 and specular_1 = matSpecular1*lightSpecular1, clamps
// each channel to [0,1], quantises it to 8 bits, and negates the light's direction into `position`.
// (oot3d-decomp/docs/fragment_lighting.md, "FOUND: the 0x2C record is fully mapped".)
//
// WHY THERE IS NO PRODUCER YET. On the 3DS those light records live at `renderer + 0x10 + i*0x60`
// (colours +0x88/0x98/0xA8/0xB8, direction +0xD8, enable +0xE4) and NO FUNCTION THAT WRITES THEM
// HAS BEEN LOCATED. Everything downstream of them is therefore transport: this header, the packer,
// and the closed-form shading in both fragment shaders. The cheapest unblock is one write-watch on
// `light[0] + 0xE4` in the cache-owned MM3D frame, reporting the writer PC.
//
// The bank is therefore EMPTY by default and `Zelda3D_GL_SetFragmentLightBank` has no caller in the
// tree. That is deliberate and it is the safe direction: with no slots the shaders take their
// existing path unchanged. Do NOT publish synthesised slots to make this look live. A flat additive
// specular that is WRONG is worse than the black it replaces, because black was at least a constant
// this host chose on purpose -- and for MM3D's captured slots the true FRAGMENT_SECONDARY is
// sum(specular_0 + specular_1) = (1.082, 0.894, 0.780), which clamps to near-white, so a guessed
// colour is not a small error.
//
// SLOT SHAPE. PICA loops `light_index` from 0 to `max_light_index` inclusive and selects the light
// record through the three-bit `light_enable` slot map. The bank instead carries one self-contained
// payload per ORDERED SLOT, which is what the recorded configuration actually is: MM3D's 12 lit
// captures all read `max_light_index = 1` with `slot_mapping = [0,1,0,...]`, i.e. the identity map.
// A producer with a non-identity map fills two slots with the same payload.
//
// DIRECTIONAL ONLY. `FUN_003fa5d0` writes the slot-enable byte and `record[0x18] = 1` (PICA's
// `LightSrc.config.directional`) inside the same `if`, so on the retail path every enabled slot is
// directional; spot and distance attenuation are explicitly zeroed by the per-record initialiser
// `FUN_004c7c80` and never written again. A NON-directional light would need PICA's `view` vector
// (`light_vector = position + view`, and the Fresnel/LUT terms need a half vector), which no host
// path has, so this transport cannot represent one and the publisher rejects it outright rather
// than shading a point light as if it were directional.

#ifndef ZELDA3D_FAST_FRAGMENT_LIGHTING_H
#define ZELDA3D_FAST_FRAGMENT_LIGHTING_H

// PICA has eight light slots; `FUN_003fa5d0` visits three, and every captured configuration reads
// two. Two is the transport's capacity, and it is a bound rather than a guess: a wider bank would
// have to grow the pushed UBO, which is already close to SDL3 GPU's 4096-byte per-push cap.
#define ZELDA3D_FRAG_LIGHT_SLOTS 2

// The per-draw MATERIAL half of the colour products: the five authored CMB colours, in [0,1].
// `FUN_003fa5d0` reads them from the material entry (emission +0xA0, ambient +0xA4, diffuse
// +0xA8, specular_0 +0xAC, specular_1 +0xB0); the provider fills this from
// CmbMaterial::mat_emission / mat_ambient / mat_diffuse / mat_specular_0 / mat_specular_1, which
// reach the renderer as Zelda3DGlGroup::matEmission / matAmbient / matDiffuse / matSpecular0 /
// matSpecular1. This header stays free of them so the packer does not depend on a renderer's
// group record.
typedef struct Zelda3DFragmentLightMaterial {
    // CMB +0x00, `CmbMaterial::fragment_lighting`. PICA zeroes BOTH fragment colours when its
    // lighting is disabled, which this port already implements exactly; the enabled form must not
    // be applied on top of it.
    int fragmentLighting;
    float emission[3];
    float ambient[3];
    float diffuse[3];
    float specular0[3];
    float specular1[3];
} Zelda3DFragmentLightMaterial;

#ifdef __cplusplus
extern "C" {
#endif

// One light record's contribution, i.e. the 0x2C-byte PICA submission record's live payload.
typedef struct Zelda3DFragmentLightSlot {
    // The four per-light colour triples, each already carrying the material's colour: PICA reads
    // these as 8-bit-per-channel values with 255 == 1.0 (Pica::LightColor), so keep them in [0,1].
    float specular0[3];
    float specular1[3];
    float diffuse[3];
    float ambient[3];
    // PICA's `LightSrc` x/y/z: the light's direction with the SIGN FUN_003fa5d0 negates, i.e.
    // pointing from the surface toward the light. Normalised by the shader, as the hardware does.
    float position[3];
} Zelda3DFragmentLightSlot;

typedef struct Zelda3DFragmentLightBank {
    // Number of ORDERED slots the shading loop runs: PICA's `max_light_index + 1`. 0 means "no
    // grounded producer", which is the state of the world today and keeps the shaders on their
    // pre-existing path.
    int slotCount;
    // Must be 1. The closed form both shaders evaluate is only exact when EVERY optional term of
    // `ComputeFragmentsColors` is off: `config1.disable_lut_d0` set (so `d0_lut_value` stays at its
    // initial 1.0f and specular_0 is added with NO N·H term -- the specular is FLAT), and
    // `disable_lut_d1`/`disable_lut_fr`/`disable_lut_rr`/`disable_lut_rg`/`disable_lut_rb`/
    // `disable_spot_atten`/`disable_dist_atten`/`disable_shadow` all set, with `config0` at the
    // baseline 0x80000400. Eleven of MM3D's twelve lit captures reduce to exactly that; the twelfth
    // clears `disable_lut_d0` and needs a real Distribution-0 LUT lookup, i.e. a half vector and a
    // LUT this host has neither of. A publisher that cannot assert this must leave it 0.
    int reducedNoLutForm;
    Zelda3DFragmentLightSlot slots[ZELDA3D_FRAG_LIGHT_SLOTS];
} Zelda3DFragmentLightBank;

// Publish the bank for subsequent draws. A bank with `slotCount` out of range, a non-positive
// `slotCount`, `reducedNoLutForm == 0`, or any published slot that is not a directional light is
// REJECTED as a whole and leaves the transport inert; the reason is the header comment above, and
// a half-valid light set would shade the scene with a mixture of two models.
void Zelda3D_GL_SetFragmentLightBank(const Zelda3DFragmentLightBank* bank);
// Return to the inert state. Called by a frame/draw boundary that knows the lights changed.
void Zelda3D_GL_ClearFragmentLightBank(void);

#ifdef __cplusplus
} // extern "C"

namespace Zelda3DSg {
struct SgUbo;
} // namespace Zelda3DSg

namespace Zelda3DFragmentLighting {

// Reduce the published bank and this draw's material colour block into the fragment UBO.
// Idempotent for a cleared UBO, and reports zero usable slots for a draw whose material has
// `fragmentLighting == 0`.
void PackDraw(Zelda3DSg::SgUbo& ubo, const Zelda3DFragmentLightMaterial& material);

} // namespace Zelda3DFragmentLighting
#endif // __cplusplus

#endif // ZELDA3D_FAST_FRAGMENT_LIGHTING_H
