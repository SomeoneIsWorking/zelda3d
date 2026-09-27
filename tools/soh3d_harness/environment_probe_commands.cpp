#include "environment_probe_commands.h"

#include <cstdint>
#include <cstdio>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_render_debug_bridge.h"
#include "repl_protocol.h"
#include "soh_environment_state.h"
#include "soh_render_debug_bridge.h"

namespace HarnessEnvironmentProbe {
namespace {

bool HandleZelda3dLight() {
    float ambient[3] = {};
    float light1[3] = {};
    float light2[3] = {};
    float n64Ambient[3] = {};
    const int live = SohState_Zelda3DLive(ambient, light1, light2, n64Ambient);
    if (!live) {
        HarnessRepl::PrintErr("soh_z3dlive: no live environment (still in a menu?)");
        return true;
    }
    // `ambient` is the SUBMITTED scene ambient (what Zelda3D_GL_SetLightParams received, and at the
    // title the 3DS title-palette blend). `worldAmbColor` is gZelda3dWorldAmbColor, which the shader
    // also reads but which is only written when gZelda3dWorldAmbOverride is 0 -- it defaults to 1, so
    // it normally sits at its init (0,0,1). Both are printed and labelled because each has been
    // mistaken for the other.
    std::printf("ok soh_z3dlive submittedAmbient=(%.3f,%.3f,%.3f) light1=(%.3f,%.3f,%.3f) "
                "light2=(%.3f,%.3f,%.3f) worldAmbColorOverrideGated=(%.3f,%.3f,%.3f)\n",
                ambient[0], ambient[1], ambient[2], light1[0], light1[1], light1[2], light2[0], light2[1],
                light2[2], n64Ambient[0], n64Ambient[1], n64Ambient[2]);
    return true;
}

bool HandleZelda3dFog(std::istringstream& arguments) {
    std::string valueText;
    if (arguments >> valueText) {
        const auto value = HarnessRepl::ParseNum(valueText);
        if (!value) {
            HarnessRepl::PrintErr("soh_fog3d: bad arg");
            return true;
        }
        gZelda3dFog3dForceOff = *value == 0 ? 1 : 0;
    }
    std::printf("ok soh_fog3d forceOff=%d on=%d a=%.6f b=%.4f near=%.1f far=%.1f "
                "fwd=(%.3f,%.3f,%.3f) fwdDotEye=%.1f\n",
                gZelda3dFog3dForceOff, gZelda3dFog3dOn, gZelda3dFog3d[0], gZelda3dFog3d[1], gZelda3dFog3d[2],
                gZelda3dFog3d[3], gZelda3dFog3d[4], gZelda3dFog3d[5], gZelda3dFog3d[6], gZelda3dFog3d[7]);
    return true;
}

bool HandleEnvironment() {
    unsigned int daytime = 0;
    unsigned char skybox1 = 0;
    unsigned char skybox2 = 0;
    float blend = 0.0F;
    unsigned char ambient[3] = {};
    unsigned char fog[3] = {};
    short fogNear = 0;
    short fogFar = 0;
    if (!SohState_DayTimeAndEnv(&daytime, &skybox1, &skybox2, &blend, ambient, fog, &fogNear, &fogFar)) {
        HarnessRepl::PrintErr("soh_env: no playstate");
        return true;
    }
    std::printf("ok soh_env daytime=0x%04x skybox1=%u skybox2=%u blend=%.3f "
                "ambient=(%u,%u,%u) fog=(%u,%u,%u) fogNear=%d fogFar=%d\n",
                daytime, skybox1, skybox2, blend, ambient[0], ambient[1], ambient[2], fog[0], fog[1], fog[2], fogNear,
                fogFar);
    return true;
}

bool HandleMoon() {
    float sunPositionY = 0.0F;
    float color = 0.0F;
    float scale = 0.0F;
    float discScale = 0.0F;
    if (!SohState_MoonDebug(&sunPositionY, &color, &scale, &discScale)) {
        HarnessRepl::PrintErr("soh_moon: no playstate");
        return true;
    }
    std::printf("ok soh_moon sunPosY=%.4f color=%.4f scale=%.4f discScale=%.4f\n", sunPositionY, color, scale,
                discScale);
    return true;
}

bool HandleOracleFog() {
    static char buffer[16384];
    const int size = soh3d_fog_dump(buffer, sizeof(buffer));
    if (size < 0) {
        HarnessRepl::PrintErr("az_fog: pica not up");
        return true;
    }
    std::printf("ok az_fog %s", buffer);
    std::printf("ok end\n");
    return true;
}

bool HandleOracleDaytime() {
    const auto word = Core::System::GetInstance().Memory().Read32OrNullopt(OracleLayout::kSaveContextAddress +
                                                                           OracleLayout::kDayTimeOffset);
    if (!word) {
        HarnessRepl::PrintErr("az_daytime: unmapped");
        return true;
    }
    std::printf("ok az_daytime daytime=0x%04x\n", static_cast<unsigned>(*word & 0xFFFFU));
    return true;
}

} // namespace

bool HandleCommand(const std::string& command, std::istringstream& arguments) {
    if (command == "soh_z3dlive") {
        return HandleZelda3dLight();
    }
    if (command == "soh_fog3d") {
        return HandleZelda3dFog(arguments);
    }
    if (command == "soh_letterbox") {
        std::printf("ok soh_letterbox %d\n", SohState_ShrinkWindowVal());
        return true;
    }
    if (command == "soh_env") {
        return HandleEnvironment();
    }
    if (command == "soh_moon") {
        return HandleMoon();
    }
    if (command == "az_fog") {
        return HandleOracleFog();
    }
    if (command == "az_daytime") {
        return HandleOracleDaytime();
    }
    return false;
}

} // namespace HarnessEnvironmentProbe
