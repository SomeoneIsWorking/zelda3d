// MM3D's PICA fog window. See mm3d_fog_window.h for why the window and the colour are separate
// concerns, and why there is no blend arithmetic here.

#include "mm3d_fog_window.h"

#include "fast/zelda3d_fog.h"
#include "lighting/zelda3d_env_blend.h"
#include "mm3d_scene_lighting.h"

// The 0x20 records, whose f32 distances drive the PICA window. Same generated file as the 0x16
// substitution projection: one generator, one layout, two consumers with different jobs.
#include "mm3d_scene_lighting.inc"

/// The recovered palette for `sceneId`, or NULL when the scene has none.
static const Zelda3dSceneLight* Mm3d_ScenePalette(s16 sceneId) {
    if (sceneId < 0 || sceneId >= (s16)(sizeof(kMm3dSceneLighting) / sizeof(kMm3dSceneLighting[0]))) {
        return NULL;
    }
    const Zelda3dSceneLight* scene = &kMm3dSceneLighting[sceneId];
    if (scene->slots == NULL || scene->numSlots == 0) {
        return NULL;
    }
    return scene;
}

/// The window last installed, so `Mm3d_QueryFogWindow` can report what the renderer actually got
/// rather than what the table said. Reset whenever the fog is turned off, so a stale window can never
/// be read back as if it were live.
static bool sWindowLive = false;
static float sFogNear = 0.0f;
static float sFogFar = 0.0f;
static float sZFar = 0.0f;

void Mm3d_UpdateFogWindow(PlayState* play) {
    sWindowLive = false;

    const Zelda3dSceneLight* palette = Mm3d_ScenePalette(play->sceneId);
    if (palette == NULL) {
        Zelda3D_Fog3dOff();
        return;
    }

    float fogNear;
    float fogFar;
    float zFar;
    if (!Zelda3D_EnvBlendFogWindow(palette, &fogNear, &fogFar, &zFar)) {
        // No captured schedule this frame, an index outside the palette, or a degenerate window. All
        // three mean the fog is unknown, and unknown is not a reason to guess a window.
        Zelda3D_Fog3dOff();
        return;
    }

    const Vec3f eye = play->view.eye;
    // MM's View names the camera target `at` where SoH names it `lookAt` (both at 0x34).
    // Same field, different decomp vocabulary -- the kind of difference that makes a
    // copy-pasted line fail to compile rather than silently mean something else.
    const Vec3f at = play->view.at;
    const f32 fwd[3] = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
    const f32 eyeWorld[3] = { eye.x, eye.y, eye.z };
    Zelda3D_Fog3dSet(MM3D3D_CAMERA_NEAR, zFar, fogNear, fogFar, eyeWorld, fwd);

    sWindowLive = true;
    sFogNear = fogNear;
    sFogFar = fogFar;
    sZFar = zFar;
}

/// The recovered 3DS fog distances for one slot of one scene, read straight from the generated table.
///
/// This is the cross-check path. `Mm3d_QueryFogWindow` reports what the RENDERER was handed, which has
/// travelled through the capture, the blend and the feed; this reports what the TABLE says, without
/// any of that. Comparing the two is what turns "fog is on" into evidence: the recurring failure in
/// this project is a value with exactly the right shape and the wrong number, which a single readout
/// cannot distinguish from a correct one.
const Zelda3dLightSlot* Mm3d_SceneFogSlot(s16 sceneId, u8 slot) {
    const Zelda3dSceneLight* palette = Mm3d_ScenePalette(sceneId);
    if (palette == NULL || slot >= palette->numSlots) {
        return NULL;
    }
    return &palette->slots[slot];
}

bool Mm3d_QueryFogWindow(float* outFogNear, float* outFogFar, float* outZFar, float* outCameraNear) {
    if (outFogNear != NULL) {
        *outFogNear = sFogNear;
    }
    if (outFogFar != NULL) {
        *outFogFar = sFogFar;
    }
    if (outZFar != NULL) {
        *outZFar = sZFar;
    }
    if (outCameraNear != NULL) {
        *outCameraNear = MM3D3D_CAMERA_NEAR;
    }
    return sWindowLive;
}
