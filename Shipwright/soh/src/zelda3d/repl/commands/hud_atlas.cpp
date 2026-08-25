#include "hud_atlas.h"

#include "../../hud/zelda3d_hud.h"
#include "../../hud/zelda3d_hud_assets.h"
#include "../../hud/navi_prompt_control.h"
#include "../../input/zelda3d_input.h"
#include "../../input/zelda3d_keymap.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_HudAtlasReplCommand(const char* command, const char* line, const char* outPath) {
    float f1;
    if (strcmp(command, "xboxui") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 — toggle Xbox face-button glyphs in the HUD button prompts (live; the HUD reads
        // gZelda3dXboxBtn every frame). 1 = Xbox A/B/X/Y glyphs, 0 = the N64 colored circles.
        gZelda3dXboxBtn = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "xboxui=%d", gZelda3dXboxBtn);
    } else if (strcmp(command, "navicall") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #205 verify — force the C-Up "Navi" prompt on/off. It normally appears only when the game
        // decides Navi has something to say, which is not reproducible on demand; the C-Up button and
        // its label cannot be A/B'd against the interpreter path without it.
        // A one-shot write does not survive: Interface_Update clears the flag again before the HUD
        // draws. So this sets a persistent override that Interface_Draw re-applies each frame.
        gZelda3dNaviCallForce = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "navicall=%d (forced each frame)", gZelda3dNaviCallForce);
    } else if (strcmp(command, "nativehud") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #205 — force the native HUD path on/off live. Off makes every converted element fall back
        // to its Fast3D display list, so an element's native and interpreter renders can be captured
        // in the SAME scene; comparing two separately-captured frames does not work, because the
        // world behind the HUD moves between them and any colour mask picks up the difference.
        Zelda3D_HudSetEnabled((int)f1);
        Zelda3D_ReplReply(outPath, "nativehud=%d", Zelda3D_HudGetEnabled());
    } else if (strcmp(command, "hudtex") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #31 — toggle crisp higher-res HUD textures (hearts) live; z_lifemeter.c reads
        // gZelda3dHudTex every frame. 1 = crisp 64x64 hearts, 0 = the blocky N64 16x16 hearts.
        gZelda3dHudTex = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "hudtex=%d", gZelda3dHudTex);
    } else if (strcmp(command, "keycap") == 0) {
        // #203 — inspect the keyboard HUD badges without eyeballing a screenshot.
        //   `keycap`          print the label each HUD item slot resolves to from the LIVE binding
        //   `keycap <label>`  composite that label and dump raw RGBA to scratch/raw/keycap.rgba
        // The second form is how the multi-character (widened) cap gets verified, since the default
        // scheme binds only single-character keys to the four badged buttons.
        char label[16] = { 0 };
        if (sscanf(line, "%*s %15s", label) == 1) {
            int cw = 0, ch = 0;
            const void* rgba = Zelda3D_KeyCapTex(label, &cw, &ch);
            if (rgba != NULL && cw > 0 && ch > 0) {
                FILE* f = fopen("scratch/raw/keycap.rgba", "wb");
                if (f != NULL) {
                    fwrite(rgba, 1, (size_t)cw * ch * 4, f);
                    fclose(f);
                }
                Zelda3D_ReplReply(outPath, "keycap '%s' -> %dx%d (scratch/raw/keycap.rgba)", label, cw, ch);
            } else {
                Zelda3D_ReplReply(outPath, "keycap '%s': composite failed", label);
            }
        } else {
            Zelda3D_ReplReply(outPath, "B='%s' C-Left='%s' C-Down='%s' C-Right='%s' | C-Up='%s'",
                              Zelda3D_KeyLabelForButton(BTN_B), Zelda3D_KeyLabelForButton(BTN_CLEFT),
                              Zelda3D_KeyLabelForButton(BTN_CDOWN), Zelda3D_KeyLabelForButton(BTN_CRIGHT),
                              Zelda3D_KeyLabelForButton(BTN_CUP));
        }
    } else if (strcmp(command, "atlasdump") == 0) {
        // TEMP tooling: decode an OoT3D romfs .ctxb atlas and dump raw RGBA to scratch for offline
        // inspection (find the rupee / item-icon sub-rects). `atlasdump <romfsPath> [texIdx]`.
        char path[256] = { 0 };
        int idx = 0;
        if (sscanf(line, "%*s %255s %d", path, &idx) >= 1) {
            int aw = 0, ah = 0;
            const void* rgba = Zelda3D_OoT3dAtlas(path, idx, &aw, &ah);
            if (rgba && aw > 0 && ah > 0) {
                FILE* f = fopen("scratch/raw/atlas.rgba", "wb");
                if (f) {
                    fwrite(rgba, 1, (size_t)aw * ah * 4, f);
                    fclose(f);
                }
                Zelda3D_ReplReply(outPath, "atlas %s idx=%d -> %dx%d (scratch/raw/atlas.rgba)", path, idx, aw, ah);
            } else {
                Zelda3D_ReplReply(outPath, "atlas %s idx=%d: decode failed", path, idx);
            }
        }
    } else {
        return false;
    }
    return true;
}
