// The PICA fixed-function FRAGMENT-lighting transport (render.cmb-fragment-lighting).
//
// What these lock, and why each one is a transport property rather than a shading guess:
//
//  * The per-slot colour products must be the REGISTER VALUES the oracle recorded for a real MM3D
//    lit draw, byte for byte. That fixture is the reason the port is bounded: MM3D draw 6 at frame
//    2000 reads `max_light_index = 1`, `config0 = 0x80000400`, `config1 = 0xff7fffff`,
//    `global_ambient = 0x0470e024`, `light_enable = 0x00000010` (slot_mapping [0,1]) and the two
//    slots' colours below. The numbers are transcribed from the capture the oracle wrote, decoded
//    with `Pica::LightColor` (10-bit fields, 255 == 1.0, no enable bit).
//  * The transport must be INERT by default, and stay inert for a bank the hardware configuration
//    cannot express. A flat additive specular that is wrong is worse than the deliberate black it
//    replaces, so the gate is asserted here rather than left to the shader.
//  * The FRAGMENT_SECONDARY payoff: the two transported specular_0 triples sum to
//    (1.082, 0.894, 0.780) BEFORE the shader's clamp, i.e. near-white. The host has been adding
//    black. That number is the whole reason this transport exists.
//
// The reduced FORM itself lives in the two fragment shaders and is pinned to their generated source
// in zelda3d_render_tests.cpp; it is not duplicated here.

#include "gtest/gtest.h"

#include "fast/zelda3d_fragment_lighting.h"
#include "fast/zelda3d_sg_ubo.h"

namespace {

using Zelda3DSg::SgUbo;

// Decode one captured `LightColor` register word the way the emulator does: 10-bit fields, 255 is
// 1.0. Kept as a helper so the fixture reads as the oracle wrote it.
float LightChannel(unsigned word, int shift) {
    return static_cast<float>((word >> shift) & 0x3FFu) / 255.0f;
}

void SetTriple(float (&destination)[3], unsigned word) {
    for (int channel = 0; channel < 3; ++channel) {
        destination[channel] = LightChannel(word, 20 - 10 * channel);
    }
}

// MM3D draw 6, frame 2000. Every word below is transcribed verbatim from the oracle's capture;
// the decoded channels are in parentheses so a reader can check them without the JSON.
// Both slots read `config = 0x00000001` (directional), `specular1 = 0`, `ambient = 0`, `z = 0`, and
// `xy` puts the two binary16 halves as x = -0.9365234375 / +0.9365234375 with y = 0 -- the exactly
// antiparallel pair the N64 EnvLightSettings convention produces. Two lit slots, and no optional
// shading term anywhere.
constexpr unsigned kMm3dGlobalAmbient = 0x0470e024u;   // (71, 56, 36)
constexpr unsigned kMm3dSlot0Specular0 = 0x0530fc35u;  // (83, 63, 53)
constexpr unsigned kMm3dSlot1Specular0 = 0x0c129492u;  // (193, 165, 146)
constexpr unsigned kMm3dSlot0Diffuse = 0x02907c1au;    // (41, 31, 26)
constexpr unsigned kMm3dSlot1Diffuse = 0x06014848u;    // (96, 82, 72)
constexpr float kMm3dSlot0DirectionX = -0.9365234375f; // 0xbb7e as binary16
constexpr float kMm3dSlot1DirectionX = 0.9365234375f;  // 0x3b7e as binary16

Zelda3DFragmentLightBank Mm3dBank() {
    Zelda3DFragmentLightBank bank = {};
    bank.slotCount = 2;
    bank.reducedNoLutForm = 1;
    SetTriple(bank.slots[0].specular0, kMm3dSlot0Specular0);
    SetTriple(bank.slots[1].specular0, kMm3dSlot1Specular0);
    SetTriple(bank.slots[0].diffuse, kMm3dSlot0Diffuse);
    SetTriple(bank.slots[1].diffuse, kMm3dSlot1Diffuse);
    // ambient and specular_1 both read 0x00000000 on this draw.
    bank.slots[0].position[0] = kMm3dSlot0DirectionX;
    bank.slots[1].position[0] = kMm3dSlot1DirectionX;
    return bank;
}

// A material whose authored colours are irrelevant to the fixture: with the shipped slot colours
// already carrying the product, every one of these must come out IDENTICAL whatever the material
// block says, and that is only true if the product is taken from the bank alone.
Zelda3DFragmentLightMaterial WhiteMaterial() {
    Zelda3DFragmentLightMaterial material = {};
    material.fragmentLighting = 1;
    for (int channel = 0; channel < 3; ++channel) {
        material.emission[channel] = 0.0f;
        material.ambient[channel] = 1.0f;
        material.diffuse[channel] = 1.0f;
        material.specular0[channel] = 1.0f;
        material.specular1[channel] = 1.0f;
    }
    return material;
}

class Zelda3DFragmentLightingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        Zelda3D_GL_ClearFragmentLightBank();
    }
    void TearDown() override {
        Zelda3D_GL_ClearFragmentLightBank();
    }
};

} // namespace

// The state of the world today: nothing publishes the 0x60-byte runtime light records, so the
// transport reports zero usable slots and the shaders keep their previous sources. If this test
// ever fails on a fresh tree, something started synthesising lights.
TEST_F(Zelda3DFragmentLightingTest, UnpublishedBankKeepsTheFragmentSourcesInert) {
    const Zelda3DFragmentLightBank bank = Mm3dBank();
    Zelda3D_GL_SetFragmentLightBank(&bank);
    Zelda3D_GL_ClearFragmentLightBank();

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    EXPECT_FLOAT_EQ(ubo.uFragCtl[0], 0.0f);
    for (const float lane : ubo.uFragLight) {
        EXPECT_FLOAT_EQ(lane, 0.0f);
    }
}

// A material with lighting DISABLED must not receive the enabled form even from a published bank:
// PICA zeroes both fragment colours there, which this port already implements exactly.
TEST_F(Zelda3DFragmentLightingTest, DisabledMaterialReportsNoUsableSlots) {
    const Zelda3DFragmentLightBank bank = Mm3dBank();
    Zelda3D_GL_SetFragmentLightBank(&bank);
    Zelda3DFragmentLightMaterial material = WhiteMaterial();
    material.fragmentLighting = 0;

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, material);
    EXPECT_FLOAT_EQ(ubo.uFragCtl[0], 0.0f);
    for (const float lane : ubo.uFragLight) {
        EXPECT_FLOAT_EQ(lane, 0.0f);
    }
}

// Eleven of MM3D's twelve lit captures reduce to the closed form because config1 sets
// `disable_lut_d0`; the twelfth clears it and needs a Distribution-0 LUT lookup, i.e. a half
// vector this host cannot form. A publisher that cannot assert the reduced form must be refused,
// or the flat specular would be applied to a material whose real form has an N·H term.
TEST_F(Zelda3DFragmentLightingTest, LutEnabledConfigurationIsRefusedWhole) {
    Zelda3DFragmentLightBank bank = Mm3dBank();
    bank.reducedNoLutForm = 0;
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    EXPECT_FLOAT_EQ(ubo.uFragCtl[0], 0.0f);
}

// A non-directional light needs PICA's `view` vector (`light_vector = position + view`), which no
// host path has. Half a bank of point lights shaded as directional is strictly worse than the inert
// state, so the whole bank is refused.
TEST_F(Zelda3DFragmentLightingTest, NonDirectionalSlotIsRefusedWhole) {
    Zelda3DFragmentLightBank bank = Mm3dBank();
    bank.slots[1].position[0] = 0.0f;
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    EXPECT_FLOAT_EQ(ubo.uFragCtl[0], 0.0f);
    for (const float lane : ubo.uFragLight) {
        EXPECT_FLOAT_EQ(lane, 0.0f);
    }
}

TEST_F(Zelda3DFragmentLightingTest, SlotCountBeyondTheTransportCapacityIsRefused) {
    Zelda3DFragmentLightBank bank = Mm3dBank();
    bank.slotCount = ZELDA3D_FRAG_LIGHT_SLOTS + 1;
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    EXPECT_FLOAT_EQ(ubo.uFragCtl[0], 0.0f);
}

// The captured per-slot colours must reach the UBO exactly as the oracle recorded them. The slot
// payload already carries the material's colour (FUN_003fa5d0's product), so the white material
// block below is an identity factor and must not perturb a single byte.
TEST_F(Zelda3DFragmentLightingTest, CapturedMm3dSlotColoursReachTheUboUnchanged) {
    const Zelda3DFragmentLightBank bank = Mm3dBank();
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    ASSERT_FLOAT_EQ(ubo.uFragCtl[0], 2.0f);

    // Slot 0: [0] ambient, [4] diffuse, [8] specular_0, [12] specular_1.
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_FLOAT_EQ(ubo.uFragLight[0 + channel], 0.0f) << "ambient channel " << channel;
        EXPECT_FLOAT_EQ(ubo.uFragLight[4 + channel], LightChannel(kMm3dSlot0Diffuse, 20 - 10 * channel))
            << "diffuse channel " << channel;
        EXPECT_FLOAT_EQ(ubo.uFragLight[8 + channel], LightChannel(kMm3dSlot0Specular0, 20 - 10 * channel))
            << "specular_0 channel " << channel;
        EXPECT_FLOAT_EQ(ubo.uFragLight[12 + channel], 0.0f) << "specular_1 channel " << channel;
    }
    // The direction halves ride the .w lanes in x/y/z order.
    EXPECT_FLOAT_EQ(ubo.uFragLight[3], kMm3dSlot0DirectionX);
    EXPECT_FLOAT_EQ(ubo.uFragLight[7], 0.0f);
    EXPECT_FLOAT_EQ(ubo.uFragLight[11], 0.0f);

    // Slot 1 is at base 16.
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_FLOAT_EQ(ubo.uFragLight[16 + 0 + channel], 0.0f);
        EXPECT_FLOAT_EQ(ubo.uFragLight[16 + 4 + channel], LightChannel(kMm3dSlot1Diffuse, 20 - 10 * channel));
        EXPECT_FLOAT_EQ(ubo.uFragLight[16 + 8 + channel], LightChannel(kMm3dSlot1Specular0, 20 - 10 * channel));
        EXPECT_FLOAT_EQ(ubo.uFragLight[16 + 12 + channel], 0.0f);
    }
    EXPECT_FLOAT_EQ(ubo.uFragLight[16 + 3], kMm3dSlot1DirectionX);
}

// The number the row is bounded by: PICA's FRAGMENT_SECONDARY for these slots is
// sum(specular_0 + specular_1) = (1.082, 0.894, 0.780), which CLAMPS to near-white. The host has
// been adding black. This is the transported payload, not the shader's clamp, so the assertion is
// about the data and cannot be satisfied by tuning the shading.
TEST_F(Zelda3DFragmentLightingTest, CapturedMm3dSpecularSumsToNearWhiteBeforeTheClamp) {
    const Zelda3DFragmentLightBank bank = Mm3dBank();
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, WhiteMaterial());
    ASSERT_FLOAT_EQ(ubo.uFragCtl[0], 2.0f);

    for (int channel = 0; channel < 3; ++channel) {
        const float summed = ubo.uFragLight[8 + channel] + ubo.uFragLight[12 + channel] +
                             ubo.uFragLight[16 + 8 + channel] + ubo.uFragLight[16 + 12 + channel];
        EXPECT_GT(summed, 0.78f) << "channel " << channel;
        EXPECT_LE(summed, 1.09f) << "channel " << channel;
    }
    // Red overflows 1.0 and therefore saturates; green and blue do not. That asymmetry is the
    // whole shape of the error: a guessed white specular would be right about red and wrong about
    // the other two, which is the worst kind of wrong.
    EXPECT_GT(ubo.uFragLight[8] + ubo.uFragLight[16 + 8], 1.0f);
    EXPECT_LT(ubo.uFragLight[9] + ubo.uFragLight[16 + 9], 1.0f);
    EXPECT_LT(ubo.uFragLight[10] + ubo.uFragLight[16 + 10], 1.0f);
}

// `global_ambient` is the material's emission colour, clamped and quantised like a light colour
// (FUN_003fa34c at `0x003fa4b4`..`0x003fa528`).
TEST_F(Zelda3DFragmentLightingTest, GlobalAmbientComesFromTheMaterialsEmissionColour) {
    const Zelda3DFragmentLightBank bank = Mm3dBank();
    Zelda3D_GL_SetFragmentLightBank(&bank);
    Zelda3DFragmentLightMaterial material = WhiteMaterial();
    for (int channel = 0; channel < 3; ++channel) {
        material.emission[channel] = LightChannel(kMm3dGlobalAmbient, 20 - 10 * channel);
    }

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, material);
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_FLOAT_EQ(ubo.uFragGlobalAmbient[channel], LightChannel(kMm3dGlobalAmbient, 20 - 10 * channel))
            << "channel " << channel;
    }
}

// The product must round-trip through PICA's 8-bit register field. A host that handed the shader
// the raw float product would be exact where the hardware is not, by up to half a quantisation
// step per channel; the slot payload is authored as byte/255 here so it must survive untouched,
// and the material's OWN colour block is the place a half-step error would appear.
TEST_F(Zelda3DFragmentLightingTest, ProductsRoundTripThroughTheEightBitRegisterField) {
    // material colour = 200/255, light colour = 100/255 -> 0.30784... quantises to 78/255.
    Zelda3DFragmentLightMaterial material = WhiteMaterial();
    for (int channel = 0; channel < 3; ++channel) {
        material.specular0[channel] = 200.0f / 255.0f;
    }
    Zelda3DFragmentLightBank bank = Mm3dBank();
    SetTriple(bank.slots[0].specular0, 0); // black: makes the product depend only on the material
    bank.slotCount = 1;
    Zelda3D_GL_SetFragmentLightBank(&bank);

    SgUbo ubo = {};
    Zelda3DFragmentLighting::PackDraw(ubo, material);
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_FLOAT_EQ(ubo.uFragLight[8 + channel], 0.0f) << "channel " << channel;
    }

    // Now give the light 100/255 and the material 200/255.
    SetTriple(bank.slots[0].specular0, (100u << 20) | (100u << 10) | 100u);
    Zelda3D_GL_SetFragmentLightBank(&bank);
    SgUbo quantised = {};
    Zelda3DFragmentLighting::PackDraw(quantised, material);
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_FLOAT_EQ(quantised.uFragLight[8 + channel], 78.0f / 255.0f) << "channel " << channel;
    }
}
