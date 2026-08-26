#include "2s2h/zelda3d/mm3d_player_animation_policy.h"
#include "2s2h/zelda3d/mm3d_animation_playhead.h"

#include <cassert>

using Zelda3D::MM3D::ActorAnimationPlayhead;
using Zelda3D::MM3D::PlayerAnimationPathCandidates;
using Zelda3D::MM3D::PlayerAnimationPathsForForm;
using Zelda3D::MM3D::PlayerModelForm;

int main() {
    PlayerAnimationPathCandidates paths =
        PlayerAnimationPathsForForm(PlayerModelForm::Goron, "__OTR__objects/gameplay_keep/gPlayerAnim_pg_wait");
    assert(paths.count == 1);
    assert(paths.paths[0] == "goron/anim/pg_wait.csab");

    paths = PlayerAnimationPathsForForm(PlayerModelForm::Zora, "objects/gameplay_keep/gPlayerAnim_pz_wait");
    assert(paths.count == 1);
    assert(paths.paths[0] == "zora/anim/pz_wait.csab");

    paths = PlayerAnimationPathsForForm(PlayerModelForm::Deku, "__OTR__objects/gameplay_keep/gPlayerAnim_pn_Tbox_open");
    assert(paths.count == 1);
    assert(paths.paths[0] == "nuts/anim/pn_Tbox_open.csab");

    paths = PlayerAnimationPathsForForm(PlayerModelForm::FierceDeity,
                                        "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_wait_free");
    assert(paths.count == 1);
    assert(paths.paths[0] == "boy/anim/link_normal_wait_free.csab");

    paths = PlayerAnimationPathsForForm(PlayerModelForm::Human,
                                        "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_walk");
    assert(paths.count == 2);
    assert(paths.paths[0] == "child/anim/link_normal_walk.csab");
    assert(paths.paths[1] == "boy/anim/link_normal_walk.csab");

    assert(PlayerAnimationPathsForForm(PlayerModelForm::Human, nullptr).count == 0);
    assert(PlayerAnimationPathsForForm(PlayerModelForm::Human, "__OTR__objects/gameplay_keep/gDoorHumanOpenLeftAnim")
               .count == 0);
    assert(PlayerAnimationPathsForForm(PlayerModelForm::Human, "__OTR__objects/gameplay_keep/gPlayerAnim_bad/path")
               .count == 0);

    ActorAnimationPlayhead playhead;
    assert(playhead.beginModel(100));
    playhead.frame = 12.0f;
    playhead.hasFrame = true;
    playhead.lastCsab = "child/anim/link_normal_walk.csab";
    playhead.lastFrame = 12.0f;
    playhead.morphOut = "boy/anim/link_normal_wait_free.csab";
    playhead.morphOutFrame = 4.0f;
    assert(!playhead.beginModel(100));
    assert(playhead.lastCsab == "child/anim/link_normal_walk.csab");
    assert(playhead.beginModel(200));
    assert(playhead.modelId == 200);
    assert(!playhead.hasFrame);
    assert(playhead.frame == 0.0f);
    assert(playhead.lastCsab.empty());
    assert(playhead.morphOut.empty());
    return 0;
}
