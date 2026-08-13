// OoT3D Boss_Fd2 multipart draw port.
// Ground truth: oot3d-decomp/docs/boss_fd2.md; draw FUN_0020A3B0 and chain helper FUN_00335904.
#include "boss_fd2.h"

#include "../../render/zelda3d_render.h"
#include "../../zelda3d.h"
#include "../../model/zelda3d_cmab.h"
#include "asset/mat4.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"
#include "overlays/actors/ovl_Boss_Fd2/z_boss_fd2.h"

extern "C" void BossFd2_Wait(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Emerge(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_SetupIdle(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Idle(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Burrow(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_BreatheFire(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_ClawSwipe(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Vulnerable(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Damaged(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_Death(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_SetupVulnerable(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_SetupDamaged(BossFd2* thisx, PlayState* play);
extern "C" void BossFd2_SetupDeath(BossFd2* thisx, PlayState* play);
extern "C" void BossFd_Wait(BossFd* thisx, PlayState* play);

namespace {

bool sBossFd2IdleHold = false;

struct BossFd2CsabController {
    Actor* actor = nullptr;
    BossFd2ActionFunc lastAction = nullptr;
    int previousActionState = 0;
    int lastLogicFrame = -1;
    int previousTurnToLink = 0;
    int emergePhase = 0;
    // Remaining OoT3D 30 Hz timer in HALF-TICKS. SoH logic is 20 Hz, hence each SoH tick consumes
    // exactly 3 half-ticks (= 1.5 OoT3D updates); this preserves wall-clock timing without floats.
    int emergeTimerHalfTicks = 0;
    int fakeouts = 0;
    bool emergeActive = false;
    float frame = 0.0f;
    std::string csab;
    float outgoingFrame = 0.0f;
    std::string outgoingCsab;
    int morphFramesRemaining = 0;
};

BossFd2CsabController sCsabController;

void selectCsab(BossFd2CsabController& controller, const char* csab, float frame = 0.0f) {
    controller.csab = csab;
    controller.frame = frame;
    controller.outgoingCsab.clear();
    controller.outgoingFrame = 0.0f;
    controller.morphFramesRemaining = 0;
}

void transitionCsab(BossFd2CsabController& controller, const char* csab) {
    if (controller.csab == csab) return;
    controller.outgoingCsab = controller.csab;
    controller.outgoingFrame = controller.frame;
    controller.csab = csab;
    controller.frame = 0.0f;
    // OoT3D's FUN_0012827C and outgoing attack selectors pass -5.0 to the CSAB transition helper.
    controller.morphFramesRemaining = controller.outgoingCsab.empty() ? 0 : 5;
}

void advanceLoop(BossFd2CsabController& controller, float endFrame) {
    controller.frame = controller.frame >= endFrame ? 0.0f : controller.frame + 1.0f;
}

void advanceOnce(BossFd2CsabController& controller, float endFrame) {
    controller.frame = std::min(endFrame, controller.frame + 1.0f);
}

void initializeEmergeController(BossFd2CsabController& controller, Actor* actor) {
    controller.emergePhase = 0;
    controller.emergeTimerHalfTicks = 30 * 2;
    controller.emergeActive = true;
    controller.frame = 0.0f;
    controller.csab = "vba_up";
    const BossFd* parent = reinterpret_cast<const BossFd*>(actor->parent);
    const int health = parent != nullptr ? parent->actor.colChkInfo.health : 24;
    controller.fakeouts = health >= 18 ? 0 : health >= 12 ? 1 : health >= 6 ? 2 : 3;
}

void advanceEmergeController(BossFd2CsabController& controller, Actor* actor) {
    if (controller.emergePhase < 2) {
        if (controller.emergeTimerHalfTicks > 0) {
            controller.emergeTimerHalfTicks = std::max(0, controller.emergeTimerHalfTicks - 3);
            return;
        }
        if (controller.emergePhase == 0) {
            const BossFd* parent = reinterpret_cast<const BossFd*>(actor->parent);
            const int health = parent != nullptr ? parent->actor.colChkInfo.health : 24;
            controller.emergePhase = 1;
            const int timer30Hz = health == 24 ? 45 : health >= 18 ? 38 : health >= 12 ? 30 :
                                                health >= 6  ? 15 : 8;
            controller.emergeTimerHalfTicks = timer30Hz * 2;
        } else if (controller.fakeouts > 0) {
            --controller.fakeouts;
            controller.emergePhase = 0;
            controller.emergeTimerHalfTicks = 15 * 2;
        } else {
            controller.emergePhase = 2;
            controller.emergeTimerHalfTicks = 23 * 2;
            controller.frame = 0.0f;
        }
        return;
    }
    if (controller.frame < 26.0f) {
        controller.frame += 1.0f;
    } else {
        controller.csab = "vba_search";
        controller.frame = 0.0f;
        controller.emergePhase = 3;
        controller.emergeActive = false;
    }
}

void tickCsabController(PlayState* play, BossFd2* boss) {
    BossFd2CsabController& controller = sCsabController;
    Actor* actor = &boss->actor;
    if (controller.actor != actor) {
        controller = {};
        controller.actor = actor;
    }
    const bool actionChanged = boss->actionFunc != controller.lastAction;
    if (controller.morphFramesRemaining > 0) --controller.morphFramesRemaining;
    if (boss->actionFunc != BossFd2_Emerge) controller.emergeActive = false;
    if (boss->actionFunc == BossFd2_Emerge && actionChanged && !controller.emergeActive) {
        initializeEmergeController(controller, actor);
    } else if (boss->actionFunc == BossFd2_Emerge && controller.emergeActive) {
        advanceEmergeController(controller, actor);
    } else if (actionChanged) {
        if (boss->actionFunc == BossFd2_Wait) {
            selectCsab(controller, "vba_wait");
        } else if (boss->actionFunc == BossFd2_Idle) {
            // FUN_003B9814 completes emergence by selecting slot 10 (vba_search) before entering
            // FUN_0012827C. That action switches slots at the yaw-error threshold below.
            selectCsab(controller, "vba_search");
            controller.previousTurnToLink = boss->work[FD2_TURN_TO_LINK];
        } else if (boss->actionFunc == BossFd2_BreatheFire) {
            transitionCsab(controller, "vba_atack");
        } else if (boss->actionFunc == BossFd2_ClawSwipe) {
            transitionCsab(controller, "vba_tyokkai");
        } else if (boss->actionFunc == BossFd2_Burrow) {
            transitionCsab(controller, "vba_down");
        } else if (boss->actionFunc == BossFd2_Vulnerable) {
            // Collision controller FUN_0020A668 selects slot 8 before entering FUN_00120B20;
            // that action advances to slot 9 when its first authored clip completes.
            transitionCsab(controller, "vba_hit");
        } else if (boss->actionFunc == BossFd2_Damaged) {
            // Nonlethal damage enters FUN_00137D74 on slot 5, then selects slot 6 at state 1.
            transitionCsab(controller, "vba_beforedamage");
        } else if (boss->actionFunc == BossFd2_Death) {
            // Lethal collision selects slot 6 before entering FUN_001386D4.
            transitionCsab(controller, "vba_damage");
        } else {
            // Every persistent retail action is listed above. A new action must be RE'd before it
            // selects a 3DS clip; never infer one from N64 animation identity or phase.
            transitionCsab(controller, "vba_wait");
        }
    } else if (boss->actionFunc == BossFd2_Idle) {
        const int turnToLink = boss->work[FD2_TURN_TO_LINK];
        const int previousAbs = std::abs(controller.previousTurnToLink);
        const int currentAbs = std::abs(turnToLink);
        if (previousAbs <= 1000 && currentAbs > 1000) {
            transitionCsab(controller, "vba_search");
        } else if (previousAbs > 1000 && currentAbs <= 1000) {
            transitionCsab(controller, "vba_wait");
        } else {
            advanceLoop(controller, controller.csab == "vba_wait" ? 20.0f : 16.0f);
        }
        controller.previousTurnToLink = turnToLink;
    } else if (boss->actionFunc == BossFd2_BreatheFire) {
        advanceOnce(controller, 85.0f);
    } else if (boss->actionFunc == BossFd2_ClawSwipe) {
        advanceOnce(controller, 23.0f);
    } else if (boss->actionFunc == BossFd2_Burrow) {
        advanceOnce(controller, 20.0f);
    } else if (boss->actionFunc == BossFd2_Vulnerable) {
        const int actionState = boss->work[FD2_ACTION_STATE];
        if (controller.previousActionState == 0 && actionState == 1) {
            transitionCsab(controller, "vba_pikupiku");
        } else if (actionState == 0) {
            advanceOnce(controller, 44.0f);
        } else {
            advanceLoop(controller, 15.0f);
        }
    } else if (boss->actionFunc == BossFd2_Damaged) {
        const int actionState = boss->work[FD2_ACTION_STATE];
        if (controller.previousActionState == 0 && actionState == 1) {
            transitionCsab(controller, "vba_damage");
        } else if (actionState == 0) {
            advanceOnce(controller, 16.0f);
        } else {
            advanceOnce(controller, 45.0f);
        }
    } else if (boss->actionFunc == BossFd2_Death) {
        advanceOnce(controller, 45.0f);
    }
    controller.previousActionState = boss->work[FD2_ACTION_STATE];
    controller.lastAction = boss->actionFunc;
    controller.lastLogicFrame = static_cast<int>(play->gameplayFrames);
}

constexpr const char* kVolvagiaZar = "/actor/zelda_fd.zar";
constexpr const char* kFireHairCmb = "Model/valbasia_firehair.cmb";

extern "C" void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx,
                                                float r, float g, float b, float a);
extern "C" uint8_t* Zelda3D_AutoModelReadZarFile(int modelId, const char* suffix, size_t* outSize);
extern "C" void Zelda3D_GL_SetMatUvOverride(int modelId, int materialIndex, float u, float v);
extern "C" void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
extern "C" int Zelda3D_FacialFrameTex(int modelId, int materialIndex, int frame);
extern "C" void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);
extern "C" void Zelda3D_SetBonePostRot(int modelId, int boneId, const float* mat9);

void setPostRot(int modelId, int boneId, const Zelda3D::Mat4& matrix) {
    float m9[9] = { matrix[0], matrix[1], matrix[2], matrix[4], matrix[5], matrix[6],
                    matrix[8], matrix[9], matrix[10] };
    Zelda3D_SetBonePostRot(modelId, boneId, m9);
}

int bodyModel() {
    static int modelId = 0;
    if (modelId == 0) {
        // ACTOR_BOSS_FD2's forced-CMB resolver owns the ZAR's body selection and caches this exact
        // key. Reuse it; allocating a second skinned ID with a `zar|cmb` key has no skeleton-backed
        // provider and the draw is rejected. The forced-CMB table selects valbasiagnd for this actor.
        modelId = Zelda3D_AutoModelId(kVolvagiaZar);
    }
    return modelId;
}

int fireHairModel() {
    static int modelId = 0;
    if (modelId == 0) {
        char key[128];
        snprintf(key, sizeof(key), "%s|%s", kVolvagiaZar, kFireHairCmb);
        modelId = Zelda3D_AutoModelId(key);
    }
    return modelId;
}

void applyFireHairCmab(PlayState* play, int modelId) {
    static void* cmab = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        size_t size = 0;
        uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, "valbasia_firehair.cmab", &size);
        if (bytes != nullptr) {
            cmab = Zelda3D_CmabParse(bytes, size);
            free(bytes);
        }
    }
    if (cmab == nullptr) {
        return;
    }
    const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(cmab));
    float rgba[4];
    if (Zelda3D_CmabSampleConstColorRGBA(cmab, 0, 1, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 1, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    if (Zelda3D_CmabSampleConstColorRGBA(cmab, 0, 2, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 2, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
}

void* loadCmabOnce(int modelId, const char* suffix, void*& handle, bool& tried) {
    if (handle != nullptr || tried) {
        return handle;
    }
    tried = true;
    size_t size = 0;
    uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, suffix, &size);
    if (bytes != nullptr) {
        handle = Zelda3D_CmabParse(bytes, size);
        free(bytes);
    }
    return handle;
}

void applyBodyCmabs(PlayState* play, int modelId, const BossFd2* boss) {
    static void* body = nullptr;
    static void* eye = nullptr;
    static void* pulse = nullptr;
    static bool bodyTried = false;
    static bool eyeTried = false;
    static bool pulseTried = false;
    loadCmabOnce(modelId, "valbasiagnd.cmab", body, bodyTried);
    loadCmabOnce(modelId, "valbasia_eye.cmab", eye, eyeTried);
    loadCmabOnce(modelId, "valbasiagnd2.cmab", pulse, pulseTried);

    static int debug = -1;
    static bool reportedLoad = false;
    static int lastEyeState = -1;
    if (debug < 0) {
        const char* value = getenv("ZELDA3D_DBG_BOSSFD2_MAT");
        debug = value != nullptr && value[0] != '\0';
    }
    int uvSamples = 0;

    if (body != nullptr) {
        const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(body));
        for (int material : { 0, 1, 5 }) {
            float u = 0.0f;
            float v = 0.0f;
            if (Zelda3D_CmabSampleTranslationUV(body, material, 1, frame, &u, &v)) {
                Zelda3D_GL_SetMatUvOverride(modelId, material, u, v);
                ++uvSamples;
            }
        }
    }
    if (eye != nullptr) {
        int palette = 0;
        if (Zelda3D_CmabSampleTexturePalette(eye, 3, 0, static_cast<float>(boss->eyeState), &palette)) {
            const int texture = Zelda3D_FacialFrameTex(modelId, 3, palette);
            Zelda3D_GL_SetMatTexOverride(modelId, 3, texture);
            if (debug && boss->eyeState != lastEyeState) {
                fprintf(stderr, "[BossFd2Mat] eyeState=%d palette=%d texture=%d\n", boss->eyeState, palette, texture);
                lastEyeState = boss->eyeState;
            }
        }
    }
    if (pulse != nullptr) {
        const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(pulse));
        float rgba[4];
        if (Zelda3D_CmabSampleConstColorRGBA(pulse, 4, 4, frame, rgba)) {
            Zelda3D_GL_SetMatConstOverride(modelId, 4, 4, rgba[0], rgba[1], rgba[2], rgba[3]);
        }
    }
    if (debug && !reportedLoad) {
        fprintf(stderr, "[BossFd2Mat] cmab body=%s eye=%s pulse=%s; UV sampled %d/3 materials\n",
                body != nullptr ? "loaded" : "MISSING", eye != nullptr ? "loaded" : "MISSING",
                pulse != nullptr ? "loaded" : "MISSING", uvSamples);
        reportedLoad = true;
    }
}

} // namespace

namespace Zelda3D {

s16 BossFd2Behavior::actorId() const {
    return ACTOR_BOSS_FD2;
}

void BossFd2Behavior::applyDrawOverrides(int modelId, Actor* actor, bool track, bool /*facial*/) {
    if (!track || actor == nullptr) {
        return;
    }
    const BossFd2* boss = reinterpret_cast<const BossFd2*>(actor);
    constexpr float kBinangToRad = 3.14159265358979f / 32768.0f;

    // OoT3D FUN_001d0c3c, decompiled from the callback passed to the skeleton draw:
    // bone 10: post RotateZ(head.x) then RotateY(-head.y)
    // bone 13: post RotateZ(jaw)
    // bones 14/15: post RotateZ(-jaw * 0.1)
    // FUN_00371234 is the Z concatenation and FUN_003735e8 is Y; both are now independently
    // decompiled in oot3d-decomp. These are authored 3DS draw semantics, not N64 limb animation.
    setPostRot(modelId, 10,
               matMul(matRz(boss->headRot.x * kBinangToRad), matRy(-boss->headRot.y * kBinangToRad)));
    setPostRot(modelId, 13, matRz(boss->jawOpening * kBinangToRad));
    const Mat4 jawSide = matRz(-boss->jawOpening * 0.1f * kBinangToRad);
    setPostRot(modelId, 14, jawSide);
    setPostRot(modelId, 15, jawSide);
}

bool BossFd2Behavior::drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) {
    // Both implementations establish this through ActorShape_Init:
    //   N64 BossFd2_Init: shape.yOffset = -580 / actor.scale.y
    //   OoT3D live matrix at CSAB vba_up frame 8: actor y=150, draw-matrix y=-430
    // Actor_Draw applies shape.yOffset * scale.y in WORLD Y before shape rotation. The generic
    // replacement emitter omitted it, exposing the entire normally buried body as a vertical pole.
    *worldLiftY = actor->shape.yOffset * actor->scale.y;
    localOffset->x = 0.0f;
    localOffset->y = 0.0f;
    localOffset->z = 0.0f;
    return true;
}

bool BossFd2Behavior::prepareDeferredDraw(PlayState* play, Actor* actor) {
    BossFd2* boss = reinterpret_cast<BossFd2*>(actor);
    // BossFd2_Draw deliberately emits nothing while this hidden helper waits for the flying-form
    // parent. Preparing a deferred body in that state leaves the replacement pending, so the next
    // unrelated SkelAnime draw (the parent Boss_Fd) consumes it with the wrong skeleton, animation,
    // and 0.05 actor scale. Mirror BossFd2_Draw's own visibility gate here.
    if (boss->actionFunc == BossFd2_Wait) {
        return false;
    }
    const int modelId = bodyModel();
    if (modelId < 0) {
        return false;
    }
    if (modelId == 0) {
        return false;
    }

    applyBodyCmabs(play, modelId, boss);
    if (const char* onlyMid = getenv("ZELDA3D_DBG_BOSSFD2_MID")) {
        char* end = nullptr;
        const long mid = strtol(onlyMid, &end, 0);
        if (end != onlyMid && *end == '\0' && mid >= 0 && mid < 64) {
            Zelda3D_GL_SetMidMask(modelId, 1ull << mid);
        }
    }

    gZelda3dPendingActor = actor;
    gZelda3dPendingModel = modelId;
    gZelda3dPendingScale = actor->scale.x;
    gZelda3dPendingGroundOff = 0.0f;
    gZelda3dPendingAuto = 1; // OoT3D object always plays its authored CSAB; never feed it N64 joints
    gZelda3dPendingBoneMap = nullptr;
    return true;
}

} // namespace Zelda3D

extern "C" int Zelda3D_BossFd2ResolveAnim(PlayState* play, Actor* actor, const char** outCsab,
                                            float* outFrame, const char** outMorphCsab,
                                            float* outMorphFrame, float* outMorphWeight) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_FD2 || outCsab == nullptr ||
        outFrame == nullptr || outMorphCsab == nullptr || outMorphFrame == nullptr ||
        outMorphWeight == nullptr) {
        return 0;
    }

    BossFd2CsabController& controller = sCsabController;
    if (controller.actor != actor) {
        controller = {};
        controller.actor = actor;
    }

    // The actor post-update owns the controller tick. Draw only samples its authored CSAB result,
    // so no N64 animation pointer, clip identity, cursor, joint table, or morph state enters here.
    if (controller.csab.empty()) selectCsab(controller, "vba_wait");

    *outCsab = controller.csab.c_str();
    *outFrame = controller.frame;
    *outMorphCsab = controller.outgoingCsab.empty() ? nullptr : controller.outgoingCsab.c_str();
    *outMorphFrame = controller.outgoingFrame;
    *outMorphWeight = static_cast<float>(controller.morphFramesRemaining) / 5.0f;
    return 1;
}

extern "C" int Zelda3D_BossFd2DrawManeSegment(PlayState* play, Actor* actor, int chain, int segment,
                                               const Vec3f* pos, const Vec3f* rot,
                                               const Vec3f* scale) {
    if (actor == nullptr || actor->id != ACTOR_BOSS_FD2 || chain < 0 || chain >= 3 ||
        segment < 0 || segment >= 9) {
        return 0;
    }
    static int debug = -1;
    static bool reported[3][9] = {};
    if (debug < 0) {
        const char* value = getenv("ZELDA3D_DBG_BOSSFD2_MANE");
        debug = value != nullptr && value[0] != '\0';
    }
    if (debug && !reported[chain][segment]) {
        fprintf(stderr,
                "[BossFd2Mane] chain=%d segment=%d pos=(%.3f,%.3f,%.3f) rot=(%.6f,%.6f,%.6f) "
                "scale=(%.8f,%.8f,%.8f)\n",
                chain, segment, pos->x, pos->y, pos->z, rot->x, rot->y, rot->z,
                scale->x, scale->y, scale->z);
        reported[chain][segment] = true;
    }
    const int modelId = fireHairModel();
    if (modelId <= 0) {
        return 0;
    }
    applyFireHairCmab(play, modelId);
    // FUN_00335904 submits T(pos) * Ry * Rx * S(nonuniform) * Rx(pi/2). The final rotation must
    // remain after the nonuniform scale; folding it into rot.x changes the transform.
    return Zelda3D_DrawModelTransform(play, modelId, pos, rot, scale, M_PI / 2.0f);
}

extern "C" int Zelda3D_BossFd2ForceGround(Actor* actor) {
    if (actor == nullptr || actor->id != ACTOR_BOSS_FD2 || actor->parent == nullptr ||
        actor->parent->id != ACTOR_BOSS_FD) {
        return 0;
    }
    BossFd* parent = reinterpret_cast<BossFd*>(actor->parent);
    parent->handoffSignal = FD2_SIGNAL_GROUND;
    sBossFd2IdleHold = false;
    return 1;
}

extern "C" int Zelda3D_BossFd2ForceIdle(PlayState* play, Actor* actor, int hold) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_FD2 || actor->parent == nullptr ||
        actor->parent->id != ACTOR_BOSS_FD) {
        if (actor == nullptr) sBossFd2IdleHold = false;
        return 0;
    }
    BossFd2* boss = reinterpret_cast<BossFd2*>(actor);
    BossFd* parent = reinterpret_cast<BossFd*>(actor->parent);
    // The two actors are one state machine: while the ground head is active, the flying-body parent
    // is in BossFd_Wait and draws effects only. Forcing just BossFd2 produced a state the game cannot
    // reach and left the parent's long N64 body stretched across the diagnostic frame.
    parent->actionFunc = BossFd_Wait;
    parent->handoffSignal = FD2_SIGNAL_NONE;
    if (actor->world.pos.y < 0.0f) actor->world.pos.y = 150.0f;
    BossFd2_SetupIdle(boss, play);
    sBossFd2IdleHold = hold != 0;
    return 1;
}

extern "C" int Zelda3D_BossFd2ForceDamageState(PlayState* play, Actor* actor, int state) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_FD2 || actor->parent == nullptr ||
        actor->parent->id != ACTOR_BOSS_FD) {
        return 0;
    }
    BossFd2* boss = reinterpret_cast<BossFd2*>(actor);
    BossFd* parent = reinterpret_cast<BossFd*>(actor->parent);
    // Preserve the reachable two-actor state exactly as fd2idle does. Otherwise the still-active
    // flying parent immediately hands the helper back into emergence, making this diagnostic lie.
    parent->actionFunc = BossFd_Wait;
    parent->handoffSignal = FD2_SIGNAL_NONE;
    if (actor->world.pos.y < 0.0f) actor->world.pos.y = 150.0f;
    if (state == 0) {
        BossFd2_SetupVulnerable(boss, play);
    } else if (state == 1) {
        BossFd2_SetupDamaged(boss, play);
    } else if (state == 2) {
        BossFd2_SetupDeath(boss, play);
    } else {
        return 0;
    }
    sBossFd2IdleHold = false;
    return 1;
}

extern "C" void Zelda3D_BossFd2IdleTick(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_FD2) return;
    BossFd2* boss = reinterpret_cast<BossFd2*>(actor);
    tickCsabController(play, boss);
    if (!sBossFd2IdleHold) return;
    if (actor->parent != nullptr && actor->parent->id == ACTOR_BOSS_FD) {
        BossFd* parent = reinterpret_cast<BossFd*>(actor->parent);
        parent->actionFunc = BossFd_Wait;
        parent->handoffSignal = FD2_SIGNAL_NONE;
    }
    if (boss->actionFunc != BossFd2_Idle) BossFd2_SetupIdle(boss, play);
    actor->world.pos.y = 150.0f;
    actor->velocity = { 0.0f, 0.0f, 0.0f };
    actor->speedXZ = 0.0f;
}
