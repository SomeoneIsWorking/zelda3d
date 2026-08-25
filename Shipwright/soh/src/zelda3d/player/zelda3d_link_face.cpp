// Zelda3D behavior: Link's FACIAL animation (eye + mouth texture indices), ported from OoT3D.
//
// USER BUG #201(d): Link plays the stretch/yawn idle fidget with a completely neutral face — eyes
// wide open, mouth shut. On 3DS his eyes squeeze shut and his mouth opens wide. We were not porting
// the facial channel for the PLAYER at all (every NPC already had it — see behaviors/actor/*.cpp).
//
// GROUND TRUTH (RE'd 2026-07-23 from the retail romfs; docs/re-frontier.md `player.facial-anim`):
//
//   1. The FACE IS PART OF THE ANIMATION, not of the actor state. Both Link zars carry, next to
//      every one of their 582 `boy/anim/<clip>.csab`, a `boy/anim/<clip>.faceb` — a step-keyframe
//      track of (frame, eyeIndex, mouthIndex) (format + 0xFF hold semantics: asset/faceb.h). This
//      is Grezzo's re-encoding of the SAME data N64 hides in the animation's fake limb 22
//      (z_player_lib.c Player_DrawImpl: eye = (jointTable[22].x & 0xF) - 1, mouth = (>> 4) - 1).
//      So the port is faithful only if the index comes from the CLIP AT ITS PLAYHEAD — which is why
//      this samples the frame the draw path actually resolved (Zelda3D_LastAutoAnim), not the N64
//      curFrame (the CSAB and the N64 clip differ in length, so the two drift apart).
//
//   2. The index selects a TexturePalette cmab frame bound to ONE eye and ONE mouth material —
//      the ordinary Zelda3D facial channel, no player special case:
//        child  childlink_eye.cmab -> mat 14 (8 frames) | childlink_mouth.cmab -> mat 15 (4 frames)
//        adult  link_eye.cmab      -> mat 16 (8 frames) | link_mouth.cmab      -> mat 17 (4 frames)
//      8 eye / 4 mouth is exactly N64's sEyeTextures[8] / sMouthTextures[4]. The material slots are
//      the cmab's own mmad material_index (read from the asset, not guessed) and live in ONE place,
//      kFacialAssets in zelda3d_model.cpp, reached here via Zelda3D_FacialMaterialIndex.
//
//   The yawn the user reported is `wait_typeD_20f` (N64 sFidgetAnimations FIDGET_STRETCH_*): its
//   faceb holds eye=7 (squeezed shut) over frames 19..38 and mouth=3 (wide open) over frames 36..78.
//
// FALSIFIED on the way (do not re-chase):
//   - "OoT3D Link's eye is a UV-scrolled atlas": no. link_e00 is 128x64 and c_eye01 64x64, but the
//     alternates are separate blobs inside the cmab, selected by a TexturePalette track — a texture
//     SWAP, identical to every NPC. (tools/cmab.py on link_eye.cmab: `TexturePalette mat=16`.)
//   - "the vendored N64 z_player.c already computes the indices, just forward them": the Player
//     struct has no eye/mouth field at all. N64 reads them out of the ANIMATION (limb 22) with only
//     a `shape.face` fallback for scripted faces, and our Link plays OoT3D CSABs — whose joint data
//     has no limb 22 — so the N64 values are simply not available on this path. The faceb IS the
//     3DS-side source; nothing about it is a stand-in.
//
// NOT YET PORTED (deliberately left OPEN rather than approximated — see re-frontier):
//   the `shape.face` scripted-face fallback (N64 sEyeMouthIndexes[face]) for damage/cutscene faces.
//   OoT3D's twin has not been RE'd, and a clip whose faceb holds (0xFF) simply keeps the last bound
//   face, which is the correct behavior for the whole idle/locomotion set.
#include "z64.h"
#include "zelda3d_link_face.h"
#include "../anim/automatic_playback.h"
#include "../behaviors/actor/npc_draw.h"

extern "C" {
// Sample `<clip>.faceb` at `frame`; out indices are -1 when the clip holds that channel.
int Zelda3D_FacebSample(int modelId, const char* animName, float frame, int* outEye, int* outMouth);
// CMB material a model's facial cmab drives: slot 0 = eye, slot 1 = mouth. -1 = none.
int Zelda3D_FacialMaterialIndex(int modelId, int slot);
}

extern "C" void Zelda3D_LinkFaceUpdate(int modelId) {
    const int eyeMat = Zelda3D_FacialMaterialIndex(modelId, 0);
    const int mouthMat = Zelda3D_FacialMaterialIndex(modelId, 1);
    if (eyeMat < 0 && mouthMat < 0) {
        return; // model has no facial cmab (e.g. a rig we haven't mapped) — leave the base sprites
    }

    // The face persists across clips: a faceb key of 0xFF (and a clip with no faceb at all) means
    // "this clip does not drive that channel", so the last bound index stays — exactly what makes a
    // blink authored in a fidget survive into the neutral idle that follows it. Hence the latch.
    static int sLastEye = 0;
    static int sLastMouth = 0;
    static int sLastModel = -1;
    if (modelId != sLastModel) { // age change (child <-> adult rig): start from the neutral face
        sLastModel = modelId;
        sLastEye = 0;
        sLastMouth = 0;
    }

    const char* csab = nullptr;
    float frame = 0.0f;
    if (Zelda3D_LastAutoAnim(modelId, &csab, &frame) && csab != nullptr) {
        int eye = -1, mouth = -1;
        Zelda3D_FacebSample(modelId, csab, frame, &eye, &mouth);
        if (eye >= 0) {
            sLastEye = eye;
        }
        if (mouth >= 0) {
            sLastMouth = mouth;
        }
    }

    if (eyeMat >= 0) {
        Zelda3D::applyFacialFrame(modelId, eyeMat, sLastEye);
    }
    if (mouthMat >= 0) {
        Zelda3D::applyFacialFrame(modelId, mouthMat, sLastMouth);
    }
}
