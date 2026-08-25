#include "model_joint_diagnostics.h"

#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>
#include "overlays/actors/ovl_En_Ge1/z_en_ge1.h"

bool Zelda3D_ModelJointDiagnosticsReplCommand(PlayState* play, const char* command, const char* line,
                                              const char* outPath) {
    char path[1024];
    if (strcmp(command, "jointdump") != 0 || sscanf(line, "%*s %1023s", path) != 1) {
        return false;
    }

    EnGe1* gerudo = nullptr;
    for (s32 category = 0; category < ACTORCAT_MAX && gerudo == nullptr; ++category) {
        for (Actor* actor = play->actorCtx.actorLists[category].head; actor != nullptr; actor = actor->next) {
            if (actor->id == ACTOR_EN_GE1) {
                gerudo = reinterpret_cast<EnGe1*>(actor);
                break;
            }
        }
    }
    if (gerudo == nullptr || gerudo->skelAnime.jointTable == nullptr || gerudo->skelAnime.limbCount <= 0) {
        Zelda3D_ReplReply(outPath, "jointdump: no live En_Ge1 with a jointTable found");
        return true;
    }

    FILE* output = fopen(path, "w");
    if (output == nullptr) {
        Zelda3D_ReplReply(outPath, "jointdump: cannot open %s", path);
        return true;
    }
    const char* animation = reinterpret_cast<const char*>(gerudo->animation);
    fprintf(output, "# En_Ge1 jointTable; limbCount=%d curFrame=%.3f animLength=%.1f anim=%s\n",
            gerudo->skelAnime.limbCount, gerudo->skelAnime.curFrame, gerudo->skelAnime.animLength,
            animation != nullptr ? animation : "(null)");
    fprintf(output, "idx,x,y,z\n");
    for (s32 limb = 0; limb <= gerudo->skelAnime.limbCount; ++limb) {
        const Vec3s& joint = gerudo->skelAnime.jointTable[limb];
        fprintf(output, "%d,%d,%d,%d\n", limb, joint.x, joint.y, joint.z);
    }
    fclose(output);
    Zelda3D_ReplReply(outPath, "jointdump -> %s (limbCount=%d curFrame=%.2f anim=%s)", path,
                      gerudo->skelAnime.limbCount, gerudo->skelAnime.curFrame,
                      animation != nullptr ? animation : "(null)");
    return true;
}
