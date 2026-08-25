#include "input_injection.h"

#include "../../hud/zelda3d_hud_assets.h"
#include "../../input/zelda3d_input.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_InputInjectionReplCommand(const char* command, const char* line, const char* outPath) {
    float f1;
    if (strcmp(command, "inputdev") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 hotswap — force the "last-used input device" signal (0=gamepad glyphs, 1=keyboard
        // glyphs). Normally set automatically by the LUS input path on each key/gamepad event;
        // this REPL command overrides it for testing when no physical device is connected.
        gZelda3dInputDevice = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "inputdev=%d (%s glyphs)", gZelda3dInputDevice,
                          gZelda3dInputDevice ? "keyboard" : "gamepad");
    } else if (strcmp(command, "key") == 0) {
        // #20 — inject a raw keyboard scancode through the real SDL->ControlDeck path so the
        // DEFAULT keyboard->N64-button mapping can be verified headless. `key <scancode> <0|1>`
        // (1=key down/held, 0=key up). Hold a key (down, wait, up) to drive locomotion (WASD=stick)
        // or to hold a button. CURRENT PC-native default map (#96, verified 2026-07-17 vs
        // libultraship ControllerDefaultMappings.cpp — the PRE-#96 "A=X(45)/Start=SPACE(57)" values
        // this comment used to list are WRONG and misled menu-driving): A=SPACE(57) B=F(33) Z=Q(16)
        // R=CTRL(29) L=SHIFT(42) Start=ENTER(28) C-up=C(46) C-left/down/right=1/2/3(2/3/4 — the
        // PC-native item bar, #203; they were the arrow keys before scheme v3, which are now unbound)
        // D-up/dn/lt/rt=I/K/J/L(23/37/36/38) stick L/R/U/D=A/D/W/S(30/32/17/31). So a menu CONFIRM
        // is `key 57` (A=SPACE), pause/menu open is `key 28` (Start=ENTER). Pair with posinfo to observe.
        // Zelda3D_InjectKey declared via input/zelda3d_input.h (included above); moved from
        // zelda3d_model.cpp to zelda3d/input/zelda3d_input.cpp (Phase 1 input consolidation).
        int sc = 0, down = 1;
        int n = sscanf(line, "%*s %d %d", &sc, &down);
        if (n >= 1) {
            int r = Zelda3D_InjectKey(sc, down);
            Zelda3D_ReplReply(outPath, "key scancode=%d down=%d -> consumed=%d%s", sc, down, r,
                              r < 0 ? " (no control deck)" : "");
        } else {
            Zelda3D_ReplReply(outPath, "usage: key <scancode> <0|1>  (e.g. key 57 1 = Start down)");
        }
    } else {
        return false;
    }
    return true;
}
