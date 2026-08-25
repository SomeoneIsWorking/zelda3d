#include "help.h"

#include "../zelda3d_repl.h"

#include <cstring>

namespace {

constexpr const char* kCommandCatalog[] = {
    "core: help enable state statecheck unified quit quitteardown fps frames freeze step settle",
    "render: mul diff tint lightdir lightparams worldamb worldlit fog fog3d sky sceneoff scenescale "
    "scale yoff rotx roty rotz gscale autoyoff facecull stairs stairsize",
    "model: auto autostate n64anim animlist animforce animdbg animrate animframe animlive submitted "
    "jointdump atlasdump sgdrawlist sgdrawonly sgmodelonly sgdump geomscan",
    "scene: tp tpf warp cswarp introcs eventflag age savecycle roominfo roomwarp hlroom terrainwarp collision "
    "floorat floorcol exitat floorgrid exitgrid wallscan meshfloor time",
    "player: move gcam walkhold btnhold pause turn posinfo cammode climbinfo forceclimb linkstate "
    "linkanimstate linkground boots gohmaclimb",
    "camera/cutscene: cam camfreeze camorbit camdraw camlift cscams csinfo titlecam titlecs titlecue "
    "skip skiptest",
    "actor: asel ztarget ztargetstate ahide afreeze apos arot aparams acam aaim aorbit ainfo actors "
    "actorscan actorsnear apeek bscan asample spawn spawnp floaters",
    "actor behavior: vbball vbinfo fd2ground fdfly fddeath fdfx fdinfo fd2idle fd2state chickflap "
    "cuccoheld cuccopose cuccostate flapinfo wingflap wingmap wingprobe enkomask dooraxis doorbone "
    "doorforce doorgain doorhold swtilt",
    "pose/geometry: bitem boneinfo bonerot bonestats cvari faceframe facial mptrace morph posescan "
    "track archinfo",
    "HUD/input: nativehud navicall xboxui key keycap inputdev btnhold hudtex gi gidisp girot giscale "
    "upg log dump",
    "menu/launcher: menu menuclick menuhit menurow launcher switchgame randogen",
};

} // namespace

bool Zelda3D_HelpReplCommand(const char* command, const char* outPath) {
    if (std::strcmp(command, "help") != 0) {
        return false;
    }

    Zelda3D_ReplReply(outPath, "Zelda3D REPL command catalog:");
    for (const char* category : kCommandCatalog) {
        Zelda3D_ReplReply(outPath, "  %s", category);
    }
    return true;
}
