// MM3D animation playback: resolves, phase-locks, morphs, and uploads native CSAB poses.
#include "mm3d_animation.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <fast/zelda3d_pose.h>

#include "asset/csab.h"
#include "mm3d_animation_playhead.h"
#include "mm3d_model_store.h"
#include "mm3d_phase_diagnostics.h"
#include "mm3d_player_animation.h"

namespace Zelda3D::MM3D {
namespace {

struct CapturedAnimation {
    const char* otr = nullptr;
    float frame = 0.0f;
    float length = 0.0f;
    float morphWeight = 0.0f;
};

std::unordered_map<void*, CapturedAnimation> g_capturedAnimations;
std::unordered_map<const void*, ActorAnimationPlayhead> g_playheads;

struct AnimationAssets {
    std::unordered_map<std::string, std::unique_ptr<Csab>> clips;
    std::string defaultClip;
    bool defaultResolved = false;
};

std::unordered_map<int, AnimationAssets> g_animationAssets;

struct AnimationMap {
    const char* n64Otr;
    const char* csab;
};

const AnimationMap kAnimationMaps[] = {
#include "mm3d_animmap.inc"
};

struct DefaultAnimation {
    const char* gar;
    const char* csab;
};

const DefaultAnimation kDefaultAnimations[] = {
#include "mm3d_defaultanim.inc"
};

const char* ResolveCsab(const char* animationOtr) {
    if (animationOtr == nullptr) {
        return nullptr;
    }
    const char* key = animationOtr;
    if (strncmp(key, "__OTR__", 7) == 0) {
        key += 7;
    }
    for (const auto& mapping : kAnimationMaps) {
        if (strcmp(mapping.n64Otr, key) == 0) {
            return mapping.csab;
        }
    }
    return nullptr;
}

const char* DefaultCsab(int modelId, LoadedModel& model) {
    AnimationAssets& assets = g_animationAssets[modelId];
    if (assets.defaultResolved) {
        return assets.defaultClip.empty() ? nullptr : assets.defaultClip.c_str();
    }
    assets.defaultResolved = true;
    if (!model.garName.empty()) {
        for (const auto& fallback : kDefaultAnimations) {
            if (model.garName == fallback.gar) {
                assets.defaultClip = fallback.csab;
                return assets.defaultClip.c_str();
            }
        }
    }
    if (model.gar != nullptr) {
        const GarFile* first = nullptr;
        for (const auto& file : model.gar->files()) {
            if (file.type != "csab") {
                continue;
            }
            if (first == nullptr) {
                first = &file;
            }
            if (file.name.find("wait") != std::string::npos) {
                assets.defaultClip = file.name;
                break;
            }
        }
        if (assets.defaultClip.empty() && first != nullptr) {
            assets.defaultClip = first->name;
        }
    }
    return assets.defaultClip.empty() ? nullptr : assets.defaultClip.c_str();
}

Csab* LoadAnimation(int modelId, LoadedModel& model, const char* baseName) {
    if (baseName == nullptr || baseName[0] == '\0') {
        return nullptr;
    }
    std::string key(baseName);
    AnimationAssets& assets = g_animationAssets[modelId];
    if (const auto found = assets.clips.find(key); found != assets.clips.end()) {
        return found->second.get();
    }
    const bool playerModel = IsPlayerAnimationModel(modelId);
    const Gar* archive = playerModel ? PlayerAnimationArchive(modelId) : model.gar.get();
    if (archive == nullptr) {
        return nullptr;
    }
    const GarFile* animationFile = nullptr;
    for (const auto& file : archive->files()) {
        const bool matches = playerModel ? file.path == key : file.name == key;
        if (file.type == "csab" && matches) {
            animationFile = &file;
            break;
        }
    }
    std::unique_ptr<Csab> animation;
    if (animationFile != nullptr) {
        animation = std::make_unique<Csab>(archive->read(*animationFile));
        if (!animation->ok()) {
            fprintf(stderr, "[MM3D] Csab %s: %s\n", key.c_str(), animation->error().c_str());
            animation.reset();
        }
    } else {
        fprintf(stderr, "[MM3D] csab not found: %s\n", key.c_str());
    }
    return assets.clips.emplace(std::move(key), std::move(animation)).first->second.get();
}

void UploadPose(int modelId, LoadedModel& model, const std::vector<std::array<float, 16>>& skinMatrices) {
    const auto& bindMatrices = model.cmb->boneMatrices();
    Zelda3D_GL_SetBoneBind(modelId, bindMatrices.empty() ? nullptr : bindMatrices.front().data(),
                           static_cast<int>(bindMatrices.size()));
    Zelda3D_GL_SetBones(modelId, skinMatrices.empty() ? nullptr : skinMatrices.front().data(),
                        static_cast<int>(skinMatrices.size()));
}

void SampleAnimation(int modelId, const char* name, float frame) {
    LoadedModel* model = LoadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr || name == nullptr || name[0] == '\0') {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    Csab* animation = LoadAnimation(modelId, *model, name);
    if (animation == nullptr) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    std::vector<std::array<float, 16>> matrices;
    animation->skinMatrices(*model->cmb, frame, matrices);
    UploadPose(modelId, *model, matrices);
}

void SampleMorph(int modelId, const char* incomingName, float incomingFrame, const char* outgoingName,
                 float outgoingFrame, float weight) {
    LoadedModel* model = LoadModel(modelId);
    if (model == nullptr || !model->ok || model->cmb == nullptr || incomingName == nullptr || incomingName[0] == '\0') {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    Csab* incoming = LoadAnimation(modelId, *model, incomingName);
    if (incoming == nullptr) {
        Zelda3D_GL_SetBones(modelId, nullptr, 0);
        return;
    }
    Csab* outgoing =
        outgoingName != nullptr && outgoingName[0] != '\0' ? LoadAnimation(modelId, *model, outgoingName) : nullptr;
    std::vector<std::array<float, 16>> matrices;
    if (outgoing != nullptr) {
        incoming->skinMatricesMorph(*model->cmb, incomingFrame, *outgoing, outgoingFrame, weight, matrices);
    } else {
        incoming->skinMatrices(*model->cmb, incomingFrame, matrices);
    }
    UploadPose(modelId, *model, matrices);
}

void DriveAnimation(int modelId, const void* actorKey, const char* name, float rate, float n64Frame, float n64Length,
                    float morphWeight) {
    if (name == nullptr || name[0] == '\0') {
        g_playheads.erase(actorKey);
        SampleAnimation(modelId, nullptr, 0.0f);
        return;
    }
    ActorAnimationPlayhead& playhead = g_playheads[actorKey];
    playhead.beginModel(modelId);
    LoadedModel* model = LoadModel(modelId);
    float duration = 0.0f;
    if (model != nullptr && model->ok && model->cmb != nullptr) {
        if (Csab* animation = LoadAnimation(modelId, *model, name); animation != nullptr) {
            duration = static_cast<float>(animation->duration());
        }
    }

    const bool phaseLocked = n64Length > 4.0f && n64Frame >= 0.0f && duration > 0.0f;
    float frame = 0.0f;
    if (phaseLocked) {
        frame = (n64Frame / n64Length) * duration;
    } else {
        frame = playhead.hasFrame ? playhead.frame + rate : 0.0f;
        if (duration > 0.0f && frame > duration) {
            frame = std::fmod(frame, duration);
        }
    }
    playhead.frame = frame;
    playhead.hasFrame = true;
    RecordAnimationPhase(modelId, name, actorKey, frame, duration, phaseLocked, morphWeight);

    const bool hadLast = !playhead.lastCsab.empty();
    if (!hadLast || playhead.lastCsab != name) {
        if (morphWeight > 0.0f && hadLast) {
            playhead.morphOut = playhead.lastCsab;
            playhead.morphOutFrame = playhead.lastFrame;
        } else {
            playhead.morphOut.clear();
        }
    }
    if (morphWeight <= 0.0f) {
        playhead.morphOut.clear();
    }
    playhead.lastCsab = name;
    playhead.lastFrame = frame;

    if (!playhead.morphOut.empty() && morphWeight > 0.0f) {
        SampleMorph(modelId, name, frame, playhead.morphOut.c_str(), playhead.morphOutFrame, morphWeight);
    } else {
        SampleAnimation(modelId, name, frame);
    }
}

} // namespace

const char* ApplyCapturedAnimation(int modelId, const void* jointTable) {
    const auto captured = g_capturedAnimations.find(const_cast<void*>(jointTable));
    const char* animationOtr = nullptr;
    float n64Frame = -1.0f;
    float n64Length = 0.0f;
    float morphWeight = 0.0f;
    if (captured != g_capturedAnimations.end()) {
        animationOtr = captured->second.otr;
        n64Frame = captured->second.frame;
        n64Length = captured->second.length;
        morphWeight = captured->second.morphWeight;
    }

    const bool playerModel = IsPlayerAnimationModel(modelId);
    const char* csab = nullptr;
    if (playerModel) {
        csab = ResolvePlayerAnimationPath(modelId, animationOtr);
    } else {
        csab = ResolveCsab(animationOtr);
    }
    if (csab == nullptr) {
        if (!playerModel) {
            LoadedModel* model = LoadModel(modelId);
            if (model != nullptr) {
                csab = DefaultCsab(modelId, *model);
            }
        }
        if (animationOtr != nullptr) {
            static std::set<std::string> seen;
            if (seen.insert(animationOtr).second) {
                fprintf(stderr, "[MM3D-ANIM] model=%d unmapped n64='%s' -> %s '%s'\n", modelId, animationOtr,
                        playerModel ? "exact player route" : "default", csab == nullptr ? "(none)" : csab);
            }
        }
    }
    DriveAnimation(modelId, jointTable, csab, 1.0f, n64Frame, n64Length, morphWeight);
    return csab;
}

AnimationResetCounts ResetAnimationState() {
    const AnimationResetCounts counts = { g_capturedAnimations.size(), g_playheads.size() };
    g_capturedAnimations.clear();
    g_playheads.clear();
    return counts;
}

} // namespace Zelda3D::MM3D

extern "C" void Zelda3D_MM_CaptureAnimState(void* jointTable, void* animation, float curFrame, float animLength,
                                            float morphWeight) {
    if (jointTable == nullptr) {
        return;
    }
    Zelda3D::MM3D::g_capturedAnimations[jointTable] = { static_cast<const char*>(animation), curFrame, animLength,
                                                        morphWeight };
}
