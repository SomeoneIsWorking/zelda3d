// mm3d_draw — see mm3d_draw.h. The MM decomp-side draw divert + model emit.
//
// Mirrors OoT's Zelda3D_EmitModelDraw but stays MINIMAL and generic: the standard actor
// transform (world.pos + full YXZ shape.rot + uniform world scale + a model-space ground
// lift), then the shared G_ZELDA3D_DRAW opcode. No OoT-specific per-actor draw hacks live
// here — actor-specific draw-space corrections get added behind the behavior registry
// later, exactly as OoT grew them, not bolted onto this seam.
#include "2s2h/zelda3d/mm3d_draw.h"
#include "2s2h/zelda3d/mm3d_model.h"
#include "2s2h/zelda3d/mm3d_model_diagnostics.h"
#include "2s2h/zelda3d/mm3d_pending_draw.h"

#include <fast/zelda3d_pose.h>
#include <stdio.h>  // fprintf (bring-up diagnostic)
#include <stdlib.h> // getenv, atoi
#include "global.h" // Actor, PlayState, POLY_OPA_DISP, Matrix_*, Gfx_SetupDL25_Opa, gSPZelda3DDraw

static void Zelda3D_EmitModelDraw(PlayState* play, Actor* actor, int modelId, float worldScale, float groundOffset) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Opa(play->state.gfxCtx);

    // Standard engine actor transform (Matrix_SetTranslateRotateYXZ, z_actor.c): translate
    // to world.pos, apply the FULL YXZ shape.rot (upright props carry x=z=0 -> a no-op),
    // scale to world units, then a model-space ground lift (scales with worldScale so the
    // model's feet meet the actor ground). Innermost = applied first.
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateYF(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateXF(BINANG_TO_RAD(actor->shape.rot.x), MTXMODE_APPLY);
    Matrix_RotateZF(BINANG_TO_RAD(actor->shape.rot.z), MTXMODE_APPLY);
    Matrix_Scale(worldScale, worldScale, worldScale, MTXMODE_APPLY);
    if (groundOffset != 0.0f) {
        Matrix_Translate(0.0f, groundOffset, 0.0f, MTXMODE_APPLY);
    }

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);

    // The display list executes after actor traversal, so snapshot this draw's current
    // pose and material overrides in emit order. This is also what carries an MM Player
    // form's visible-mesh mask to the matching deferred submission.
    Zelda3D_GL_EmitPose(modelId);

    // High bit of the handle = "lit": apply the renderer's half-Lambert FORM term.
    // Characters/props carry no baked vertex lighting, so without it they render flat.
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, 255, 255, 255);

    CLOSE_DISPS(play->state.gfxCtx);
}

void Zelda3D_MM_EmitModelDraw(void* play, void* actor, int modelId, float worldScale, float groundOffset) {
    Zelda3D_EmitModelDraw((PlayState*)play, (Actor*)actor, modelId, worldScale, groundOffset);
}

// Derive limbCount for a raw N64 skeleton — highest reachable limb index + 1, capped at 64.
// Matches OoT's Zelda3D_CountN64Limbs (Shipwright/soh/src/zelda3d/core/zelda3d.c). Small iterative
// walk to avoid recursion into the C++ retarget path from a C entry point.
static int Zelda3D_MM_CountLimbs(void** skeleton) {
    if (skeleton == NULL || skeleton[0] == NULL)
        return 0;
    int stack[64];
    s32 sp = 0;
    stack[sp++] = 0;
    int maxIdx = 0;
    int steps = 0;
    while (sp > 0 && steps < 256) {
        int i = stack[--sp];
        steps++;
        if (i < 0 || i >= 64)
            continue;
        if (skeleton[i] == NULL)
            continue;
        StandardLimb* lb = (StandardLimb*)Lib_SegmentedToVirtual(skeleton[i]);
        if (i > maxIdx)
            maxIdx = i;
        if (lb->sibling != LIMB_DONE && sp < 64)
            stack[sp++] = lb->sibling;
        if (lb->child != LIMB_DONE && sp < 64)
            stack[sp++] = lb->child;
    }
    return maxIdx + 1;
}

// Σ of live N64 bone lengths (|jointPos| of every non-root reachable limb) — the
// rotation-invariant N64 skeleton "size" that pairs with the CMB bone-length sum
// for the rest-pose scale derivation. Mirrors OoT's Zelda3D_N64SkelBoneLenSum.
// Iterative walk (shared with CountLimbs) so we don't recurse from a C entry.
static float Zelda3D_MM_SkelBoneLenSum(void** skeleton, int limbCap) {
    if (skeleton == NULL || skeleton[0] == NULL || limbCap <= 0)
        return 0.0f;
    int stack[64];
    s32 sp = 0;
    stack[sp++] = 0;
    float sum = 0.0f;
    int steps = 0;
    while (sp > 0 && steps < 256) {
        int i = stack[--sp];
        steps++;
        if (i < 0 || i >= limbCap || i >= 64)
            continue;
        if (skeleton[i] == NULL)
            continue;
        StandardLimb* lb = (StandardLimb*)Lib_SegmentedToVirtual(skeleton[i]);
        if (i != 0) { // root jointPos is placement, not a bone length
            float x = lb->jointPos.x, y = lb->jointPos.y, z = lb->jointPos.z;
            sum += sqrtf(x * x + y * y + z * z);
        }
        if (lb->sibling != LIMB_DONE && sp < 64)
            stack[sp++] = lb->sibling;
        if (lb->child != LIMB_DONE && sp < 64)
            stack[sp++] = lb->child;
    }
    return sum;
}

// Shared SkelAnime intercept prologue: called at the top of each MM SkelAnime_Draw*Opa entry
// point. If a skinned MM3D replacement is pending for this actor, poses OoT3D bones from the
// live N64 jointTable and returns 1; caller returns immediately without walking the N64 tree.
int gZelda3dMmColliderPass = 0;

int Zelda3D_MM_InterceptSkelAnime(PlayState* play, Actor* actor, void** skeleton, Vec3s* jointTable) {
    if (skeleton == NULL || jointTable == NULL)
        return 0;
    // #107 collider re-walk: caller has already emitted the MM3D replacement and is
    // now re-running the N64 walk purely for its postLimbDraw side effects — do NOT
    // replace this second pass, let SkelAnime walk the tree and update the spheres.
    if (gZelda3dMmColliderPass)
        return 0;
    int limbCount = Zelda3D_MM_CountLimbs(skeleton);
    if (limbCount <= 0)
        return 0;
    // Stage 3 auto-scale + ground-offset. See mm3d_model.h. Skip if no pending model
    // (Zelda3D_MM_OverridePending is a no-op) or actor missing (fall back to base scale).
    if (actor != NULL) {
        float n64Sum = Zelda3D_MM_SkelBoneLenSum(skeleton, limbCount);
        // Pending's modelId lives in mm3d_model — fetch bone sum + minY. If no pending
        // replacement (-1) or CMB has no skeleton, we leave the base scale untouched.
        int mid = Zelda3D_MM_PendingModelId();
        if (mid >= 0) {
            float cmbSum = Zelda3D_MM_ModelBoneLenSum(mid);
            float minY = Zelda3D_MM_ModelMinY(mid);
            if (n64Sum > 1e-3f && cmbSum > 1e-3f) {
                float scale = actor->scale.x * (n64Sum / cmbSum);
                // ZELDA3D_MM_SCALE_LOG prints the RESULT; print the three INPUT terms too, so a
                // wrong scale can be attributed to actor->scale.x, the N64 skeleton sum or the CMB
                // sum without re-deriving. Needed because En_Stop_heishi (object_sdn) comes out
                // ~2.5x too large while the dog, using the same formula, is correct.
                {
                    static int sT = -1;
                    if (sT < 0) {
                        const char* e = getenv("ZELDA3D_MM_SCALE_LOG");
                        sT = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
                    }
                    if (sT) {
                        // DISTINGUISHER: sums matching could mean the two walks read the same data
                        // (bug) OR that Grezzo kept MM's skeleton proportions bone-for-bone (by
                        // design). Print the first few INDIVIDUAL N64 jointPos magnitudes; compare
                        // against the CMB bone translations to tell those apart element-wise.
                        static int sOnce = 0;
                        if (!sOnce) {
                            sOnce = 1;
                            for (int li = 1; li < limbCount && li < 6; li++) {
                                if (skeleton[li] == NULL)
                                    continue;
                                StandardLimb* L = (StandardLimb*)Lib_SegmentedToVirtual(skeleton[li]);
                                float x = L->jointPos.x, y = L->jointPos.y, z = L->jointPos.z;
                                fprintf(stderr, "[MM3D-BONE-N64] model=%d limb=%d jointPos=(%.1f,%.1f,%.1f) |v|=%.2f\n",
                                        mid, li, x, y, z, sqrtf(x * x + y * y + z * z));
                            }
                            Zelda3D_MM_DumpModelBones(mid, 5);
                        }
                        fprintf(stderr,
                                "[MM3D-SCALE-IN] model=%d actorId=0x%03X actorScale=%.5f "
                                "n64Sum=%.2f cmbSum=%.2f ratio=%.4f limbs=%d -> %.5f\n",
                                mid, (unsigned)actor->id, actor->scale.x, n64Sum, cmbSum, n64Sum / cmbSum, limbCount,
                                scale);
                    }
                }
                Zelda3D_MM_OverridePending(scale, -minY);
            }
        }
    }
    return Zelda3D_MM_SkelAnimeDrawRaw(play, skeleton, jointTable, limbCount);
}

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor) {
    int modelId = -1;
    float worldScale = 1.0f;
    float groundOffset = 0.0f;

    Zelda3D_EnsureModelProvider();

    if (actor == NULL || actor->draw == NULL) {
        return 0;
    }
    // The actor stores its object SLOT (index into ObjectContext); the object id lives on
    // the loaded slot entry. The draw path only runs with the object loaded, so the id is
    // valid/positive here.
    int objectId = -1;
    if (actor->objectSlot >= 0) {
        objectId = play->objectCtx.slots[actor->objectSlot].id;
    }
    if (!Zelda3D_LookupModel(actor->id, objectId, &modelId, &worldScale, &groundOffset)) {
        return 0; // no MM3D model registered -> vanilla N64 draw
    }
    // Stage 2 SkelAnime port: for skinned models, DEFER — stash the pending state and
    // return 0 so the actor's own draw() runs, letting Zelda3D_MM_SkelAnimeDrawRaw
    // (invoked at the top of MM's SkelAnime_DrawXxxOpa) pose the OoT3D bones from the
    // live N64 jointTable and emit. See docs/MM_SKELANIME_PORT.md.
    if (Zelda3D_IsModelSkinned(modelId)) {
        Zelda3D_MM_SetPending((void*)actor, modelId, worldScale, groundOffset);
        return 0;
    }
    Zelda3D_EmitModelDraw(play, actor, modelId, worldScale, groundOffset);
    return 1;
}

// MM3D scene-room divert. MM3D ships /scenes/<name>_<room>_info.zsi in the same ZSI format OoT3D
// uses, so this mirrors Zelda3D_TryDrawRoom in OoT: map sceneNum -> MM3D scene folder name, register
// the room's model id, draw it, and return 1 so the caller skips the N64 room mesh.
//
// The room CMB's verts are already WORLD-space, so the transform is identity (no per-room placement).
// The model id is emitted WITHOUT the high "lit" bit that actors use: scene rooms carry MM3D's baked
// vertex-colour lighting/AO and must not be re-shaded.
#include "mm3d_scene_names.inc"

// Debug isolation: ZELDA3D_MM_SCENE=0 disables ONLY the scene/room divert (actors still divert), so a
// crash can be bisected room-divert vs actor-divert without a rebuild. 2 = skip the N64 room but draw
// nothing. Mirrors OoT's ZELDA3D_SCENE exactly, including the default of 1 (draw).
//
// DEFAULT ON since 2026-07-21. It was opt-in during bring-up because the world rendered wrong; both
// causes were CMB parser bugs that MM3D was simply the first content to exercise (OoT3D hid both by
// coincidence):
//   1. the index-region offset -- prm.first counts u16 SLOTS, so the byte offset is always first*2,
//      never first*elementSize (OoT3D prms are all USHORT, making the two identical)
//   2. the mesh-entry stride -- version-gated 0x04 (v6) vs 0x0C (v10), previously hardcoded to 4,
//      which orphaned 27 of 41 sepds in a room and built only 49% of its geometry
//
// Evidence for defaulting on: all 313 MM3D rooms pass the structural test (finite positions, sane
// extent, zero orphan sepds) except 3 that carry no CMB at all and fall back to the N64 room; and
// South Clock Town, East Clock Town and Termina Field were each verified rendering complete in a live
// run, ground included.
static int mmSceneDivertEnabled(void) {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("ZELDA3D_MM_SCENE");
        v = (e != NULL && e[0] != '\0') ? atoi(e) : 1; // 0=off, 1=draw, 2=skip-only
    }
    return v;
}

const char* Zelda3D_MM_SceneName(PlayState* play) {
    int n;
    if (play == NULL) {
        return NULL;
    }
    n = play->sceneId;
    if (n < 0 || n >= (int)(sizeof(kMm3dSceneNames) / sizeof(kMm3dSceneNames[0]))) {
        return NULL;
    }
    return kMm3dSceneNames[n];
}

int Zelda3D_TryDrawRoom(PlayState* play, Room* room) {
    const char* sceneName;
    int modelId;

    if (!mmSceneDivertEnabled() || play == NULL || room == NULL) {
        return 0;
    }
    sceneName = Zelda3D_MM_SceneName(play);
    {
        static int logged = 0;
        if (!logged) {
            logged = 1;
            fprintf(stderr, "[MM3D-ROOM] sceneId=%d room=%d name=%s\n", (int)play->sceneId, (int)room->num,
                    sceneName ? sceneName : "(none)");
        }
    }
    if (sceneName == NULL) {
        return 0; // no MM3D scene for this sceneId -> N64 room
    }
    modelId = Zelda3D_MM_RoomModelId(sceneName, room->num);
    if (modelId < 0) {
        return 0;
    }

    // ZELDA3D_MM_SCENE=2: skip the N64 room mesh but draw NOTHING, to bisect "skipping the N64
    // room breaks the frame" vs "our GL draw is wrong". Same escape hatch OoT's ZELDA3D_SCENE has.
    if (mmSceneDivertEnabled() == 2) {
        return 1;
    }
    Zelda3D_EnsureModelProvider();
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW); // scene verts are world-space
    // BRING-UP PROBE: ZELDA3D_MM_SCENE_ROT tries a room orientation without a rebuild.
    //   0 = none (as-authored)   1 = 180deg about X   2 = 180deg about Z   3 = Y mirror (scale -1)
    // RESOLVED: the answer is 0 (as-authored). The "inverted/fragmented" symptom these modes were
    // chasing was the CMB index-offset bug, now fixed -- rotating was treating a data bug as a
    // transform bug. Kept only as a diagnostic knob; do NOT reach for it for the placement gap.
    {
        static int rotMode = -1;
        if (rotMode < 0) {
            const char* e = getenv("ZELDA3D_MM_SCENE_ROT");
            rotMode = (e != NULL && e[0] != '\0') ? atoi(e) : 0;
        }
        if (rotMode == 1) {
            Matrix_RotateXF(M_PI, MTXMODE_APPLY);
        } else if (rotMode == 2) {
            Matrix_RotateZF(M_PI, MTXMODE_APPLY);
        } else if (rotMode == 3) {
            Matrix_Scale(1.0f, -1.0f, 1.0f, MTXMODE_APPLY);
        }
    }
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1; // drew the MM3D room -> caller skips the N64 mesh
}

// Predicate used to suppress the N64 pre-rendered bg-image skybox (Play_Draw path in OoT's #134).
// Now that MM3D room CMBs render, the vanilla N64 bg image must NOT also draw for a covered scene —
// it composites over/under the MM3D room. Suppress exactly when this scene has an MM3D mapping (and
// the room divert is enabled), so uncovered scenes keep their N64 bg and stay visible.
int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play) {
    return mmSceneDivertEnabled() && Zelda3D_MM_SceneName(play) != NULL;
}
