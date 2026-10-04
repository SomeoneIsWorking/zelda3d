// PICA fixed-function fragment lighting: the published-bank owner and its reduction into the UBO.
//
// The formula this feeds is NOT here -- the fragment shaders own the shading (they are the shipping
// implementation), and the header fast/zelda3d_fragment_lighting.h explains why the transport exists
// and why no producer publishes into it yet. What is here is the one thing only this layer can do:
// turn a MATERIAL colour block and a per-LIGHT record into the four quantised colour products
// FUN_003fa5d0 forms, byte for byte.

#include "fast/zelda3d_fragment_lighting.h"

#include "fast/zelda3d_sg_ubo.h"

#include <cstddef>

namespace {

// The published bank. Empty by construction: `Zelda3D_GL_SetFragmentLightBank` has no caller in the
// tree because the 0x60-byte runtime light records it would carry have no located producer.
Zelda3DFragmentLightBank gFragmentLightBank = { 0, 0, {} };

// One channel of a colour register, already in [0,1].
//
// PICA stores these as `LightColor`: 10-bit register fields holding 8-BIT values with 255 == 1.0
// (`Azahar/src/video_core/pica/regs_lighting.h:90-102`). Both the per-light products and the
// `global_ambient` register are quantised with `(u8)(0.5f + 255.0f * v)` before their pack
// (`0x003fa8dc`..`0x003fa8e8` and siblings for the products, `0x003fa51c`..`0x003fa528` for
// global_ambient).
//
// The round trip is load-bearing and both halves are in this one function: a host that multiplied
// and handed the product straight to the shader would be exact where the hardware is not, and the
// error is a whole quantisation step per channel. `(u8)` truncates, so `0.5f` is inside the
// expression, not a rounding aid.
float QuantizeChannel(float value) {
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    const unsigned quantised = static_cast<unsigned>(clamped * 255.0f + 0.5f);
    return static_cast<float>(quantised) / 255.0f;
}

// `FUN_003fa5d0` builds each product channel as `clamp(light * material, 0, 1)` with the MATERIAL
// side already divided by 255 -- which is how the CMB colour block decodes -- and then quantises it.
float ProductChannel(float lightComponent, float materialComponent) {
    return QuantizeChannel(lightComponent * materialComponent);
}

void PackTriple(float* destination, const float light[3], const float material[3]) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
        destination[channel] = ProductChannel(light[channel], material[channel]);
    }
}

bool SlotIsDirectional(const Zelda3DFragmentLightSlot& slot) {
    // The retail record always sets `config.directional` with the slot-enable byte
    // (`0x003fa7b0` and `0x003fa890` are in the same `if`), and the host's slot payload carries the
    // direction in PICA's `position` field -- the negated light direction. A producer with no
    // direction has no directional light and cannot publish.
    const float lengthSquared =
        slot.position[0] * slot.position[0] + slot.position[1] * slot.position[1] + slot.position[2] * slot.position[2];
    return lengthSquared > 0.0f;
}

} // namespace

extern "C" void Zelda3D_GL_SetFragmentLightBank(const Zelda3DFragmentLightBank* bank) {
    if (bank == nullptr) {
        Zelda3D_GL_ClearFragmentLightBank();
        return;
    }
    const bool usable =
        bank->slotCount > 0 && bank->slotCount <= ZELDA3D_FRAG_LIGHT_SLOTS && bank->reducedNoLutForm == 1;
    // One `if` over the published slots, only reached once the cheap checks have passed, so the
    // common rejected path costs nothing. A partially valid set is rejected whole: shading half the
    // slots from real records and half from a guessed model is strictly worse than the inert state.
    bool directional = false;
    for (int slot = 0; usable && !directional && slot < bank->slotCount; ++slot) {
        directional = !SlotIsDirectional(bank->slots[slot]);
    }
    if (!usable || directional) {
        Zelda3D_GL_ClearFragmentLightBank();
        return;
    }
    gFragmentLightBank = *bank;
}

extern "C" void Zelda3D_GL_ClearFragmentLightBank(void) {
    gFragmentLightBank = Zelda3DFragmentLightBank{ 0, 0, {} };
}

void Zelda3DFragmentLighting::PackDraw(Zelda3DSg::SgUbo& ubo, const Zelda3DFragmentLightMaterial& material) {
    // PICA's `global_ambient`. FUN_003fa34c reads the material's emission bytes (+0xA0..+0xA2),
    // scales them by 1/255, clamps to [0,1] and quantises them the same way as a light colour
    // (`0x003fa4b4`..`0x003fa528`); the `material.ambient * lighting.ambient` addend the register
    // comment mentions is multiplied by a stack-local zero in that function (`vldr s6, [ip, #4]`
    // reads the zero `stm` at `0x003fa474` before the `vmla.f32 s0, s6, s3`), so it contributes
    // nothing and is not invented here.
    for (std::size_t channel = 0; channel < 3; ++channel) {
        ubo.uFragGlobalAmbient[channel] = QuantizeChannel(material.emission[channel]);
    }
    ubo.uFragGlobalAmbient[3] = 0.0f;

    // The material must also ask for fixed-function lighting: PICA zeroes both fragment colours when
    // `regs.lighting.disable` is set, which is the branch this port already implements exactly, and
    // applying the enabled form to a material that consumes the disabled result would double-light
    // the five OoT3D materials that read a fragment source with lighting off.
    const int usableSlots = (material.fragmentLighting != 0) ? gFragmentLightBank.slotCount : 0;
    for (int slot = 0; slot < ZELDA3D_FRAG_LIGHT_SLOTS; ++slot) {
        float* const base = &ubo.uFragLight[slot * 16];
        if (slot >= usableSlots) {
            // A slot outside the published range contributes nothing to either sum, so zeroing it
            // keeps the shader's loop a plain `for (i < slotCount)` with no per-slot validity test.
            for (std::size_t lane = 0; lane < 16; ++lane) {
                base[lane] = 0.0f;
            }
            continue;
        }
        const Zelda3DFragmentLightSlot& light = gFragmentLightBank.slots[slot];
        PackTriple(&base[0], light.ambient, material.ambient);
        PackTriple(&base[4], light.diffuse, material.diffuse);
        PackTriple(&base[8], light.specular0, material.specular0);
        PackTriple(&base[12], light.specular1, material.specular1);
        // The position halves ride the .w lanes of the first three vec4s, in x/y/z order.
        base[3] = light.position[0];
        base[7] = light.position[1];
        base[11] = light.position[2];
        base[15] = 0.0f;
    }
    ubo.uFragCtl[0] = static_cast<float>(usableSlots);
    ubo.uFragCtl[1] = 0.0f;
    ubo.uFragCtl[2] = 0.0f;
    ubo.uFragCtl[3] = 0.0f;
}
