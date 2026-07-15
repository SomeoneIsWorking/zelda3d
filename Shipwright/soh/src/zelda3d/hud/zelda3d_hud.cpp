// Zelda3D PC HUD: layout + per-frame draw of the native Vulkan HUD (hearts, magic bar, rupees,
// hotbar) — user directive 2026-06-23, replaces both the N64 Fast3D HUD and RmlUi. This module
// owns LAYOUT (sized from the gZelda3dHudState snapshot); the C-ABI it calls into
// (Zelda3D_Hud_Begin/Tex/Draw/End, implemented in libultraship/src/fast/zelda3d_hud_vk.cpp) owns
// the actual Vulkan textured-quad drawing. Pairs with zelda3d_hud_tex.cpp (this dir), which builds
// the crisp HUD texture assets these draws blit. Extracted from zelda3d.c (Phase 2 codebase reorg
// — see docs/codemap.md).
#include "../zelda3d.h"
#include "../behaviors/title/title_presentation.h" // Zelda3D_Title_IsActive (suppress HUD during the title demo)
#include "../input/zelda3d_input.h" // Zelda3D_InputDevice (hotbar glyph set: gamepad vs keyboard)
#include <stdlib.h> // getenv
#include <stdio.h>  // snprintf

// ---- PC HUD (native Vulkan, zelda3d_hud_vk.cpp) -----------------------------------------------
// The in-game HUD rendered directly through the Vulkan backend (user directive 2026-06-23): a
// modern PC layout drawing the real HD textures, replacing both the N64 Fast3D HUD and RmlUi.
// zelda3d.c owns the LAYOUT (sized from the gZelda3dHudState snapshot); the C-ABI below (implemented
// extern "C" in libultraship/src/fast/zelda3d_hud_sdl3gpu.cpp) owns the GPU textured-quad drawing.
// Declared extern "C" here (this is now a .cpp TU, unlike the old zelda3d.c) so linkage matches.
extern "C" {
int  Zelda3D_Hud_Available(void);
int  Zelda3D_Hud_Begin(int* outW, int* outH);
int  Zelda3D_Hud_Tex(const void* key, const void* rgba, int w, int h);
void Zelda3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1,
                    float v1, unsigned int tintRGBA);
void Zelda3D_Hud_End(void);
}

int gZelda3dPcHud = -1; // -1=uninit, 0=off, 1=on
int Zelda3D_PcHudEnabled(void) {
    if (gZelda3dPcHud < 0) {
        const char* v = getenv("ZELDA3D_PCHUD");
        gZelda3dPcHud = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    // Only active when the Vulkan HUD layer is live; a GL build keeps the native HUD as fallback.
    return gZelda3dPcHud && Zelda3D_Hud_Available();
}

Zelda3dHudState gZelda3dHudState = { 0 };

void Zelda3D_HudUpdateFrame(PlayState* play) {
    (void)play;
    if (!Zelda3D_PcHudEnabled()) {
        return;
    }
    gZelda3dHudState.health         = gSaveContext.health;
    gZelda3dHudState.healthCapacity = gSaveContext.healthCapacity;
    gZelda3dHudState.magic          = (int)(u8)gSaveContext.magic;
    gZelda3dHudState.magicCapacity  = gSaveContext.magicCapacity;
    gZelda3dHudState.magicLevel     = gSaveContext.magicLevel;
    gZelda3dHudState.rupees         = gSaveContext.rupees;
    for (int i = 0; i < 6; i++) {
        gZelda3dHudState.hotbarItems[i] = gZelda3dHotbarItems[i];
    }
    gZelda3dHudState.hotbarActive = gZelda3dHotbarActive;
    gZelda3dHudState.inputDevice  = Zelda3D_InputDevice();
    gZelda3dHudState.valid        = 1;
}

// Draw a texture obtained from one of the Zelda3D_*Tex accessors (or gItemIcons). `buf` is the RGBA32
// pointer (also used as the upload cache key); (tw,th) its dimensions; the quad is (x,y,w,h) px.
static void Zelda3D_HudBlit(const void* buf, int tw, int th, float x, float y, float w, float h,
                          unsigned int tint) {
    if (buf == NULL || tw <= 0 || th <= 0) {
        return;
    }
    int id = Zelda3D_Hud_Tex(buf, buf, tw, th);
    if (id == 0) {
        return;
    }
    Zelda3D_Hud_Draw(id, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

// Blit a sub-rect (sx,sy,sw,sh in atlas pixels) of an atlas texture (aw x ah) into the quad
// (x,y,w,h). Used for the real OoT3D 3DS HUD atlases (rupee from hud_all, items from icon_item_menu).
static void Zelda3D_HudBlitAtlas(const void* atlas, int aw, int ah, int sx, int sy, int sw, int sh,
                               float x, float y, float w, float h, unsigned int tint) {
    if (atlas == NULL || aw <= 0 || ah <= 0) {
        return;
    }
    int id = Zelda3D_Hud_Tex(atlas, atlas, aw, ah);
    if (id == 0) {
        return;
    }
    Zelda3D_Hud_Draw(id, x, y, w, h, (float)sx / aw, (float)sy / ah, (float)(sx + sw) / aw,
                   (float)(sy + sh) / ah, tint);
}

// OoT3D 3DS HUD atlas romfs paths + sub-rect geometry (measured from the decoded atlases).
#define ZELDA3D_HUD_ALL_CTXB   "/menu/01_US_ENGLISH/hud_all00.ctxb"
#define ZELDA3D_ICON_ITEM_CTXB "/menu/01_US_ENGLISH/icon_item_menu00.ctxb"
// icon_item_menu00 is a 12-column grid (pitch 42px, origin (1,1), ~40px icons); cell index == item id.
#define ZELDA3D_ITEM_COLS 12
#define ZELDA3D_ITEM_PITCH 42
#define ZELDA3D_ITEM_ORIGIN 1
#define ZELDA3D_ITEM_CELL 40

// Solid (untextured) tinted rectangle — panels, magic bar, highlights.
static void Zelda3D_HudRect(float x, float y, float w, float h, unsigned int tint) {
    Zelda3D_Hud_Draw(0, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void Zelda3D_HudFrame(void) {
    if (!Zelda3D_PcHudEnabled()) {
        return;
    }
    // Az's title-demo shot 1 shows no HUD — suppress here so the PC-HUD
    // path also honours the title-demo (task #14, alongside Interface_Draw).
    if (Zelda3D_Title_IsActive()) {
        return;
    }
    const Zelda3dHudState* s = &gZelda3dHudState;
    if (!s->valid) {
        return;
    }
    int W = 0, H = 0;
    if (!Zelda3D_Hud_Begin(&W, &H)) {
        return;
    }
    // Layout authored in 720p units, scaled by framebuffer height for resolution independence.
    const float sc = (H > 0 ? (float)H : 720.0f) / 720.0f;
    const float margin = 18.0f * sc;

    // ---- Hearts (top-left) -------------------------------------------------------------------
    const float heart = 30.0f * sc;
    const float hgap = 2.0f * sc;
    const int FULL = 16; // quarter-hearts per container (FULL_HEART_HEALTH)
    int containers = s->healthCapacity / FULL;
    if (containers < 1) {
        containers = 1;
    }
    const int perRow = 10;
    for (int i = 0; i < containers; i++) {
        int rem = s->health - i * FULL;
        int kind;
        if (rem >= FULL)      kind = ZELDA3D_HEART_FULL;
        else if (rem >= 12)   kind = ZELDA3D_HEART_THREEQUARTER;
        else if (rem >= 8)    kind = ZELDA3D_HEART_HALF;
        else if (rem >= 1)    kind = ZELDA3D_HEART_QUARTER;
        else                  kind = ZELDA3D_HEART_EMPTY;
        int tw = 0, th = 0;
        const void* tex = Zelda3D_HudHeartRGBA(kind, &tw, &th);
        float hx = margin + (i % perRow) * (heart + hgap);
        float hy = margin + (i / perRow) * (heart + hgap);
        Zelda3D_HudBlit(tex, tw, th, hx, hy, heart, heart, 0xFFFFFFFFu);
    }
    int heartRows = (containers + perRow - 1) / perRow;
    float belowHearts = margin + heartRows * (heart + hgap) + 6.0f * sc;

    // ---- Magic bar (below hearts; only when the player has magic) ----------------------------
    if (s->magicLevel > 0 && s->magicCapacity > 0) {
        const float mbw = 124.0f * sc;
        const float mbh = 9.0f * sc;
        float mx = margin, my = belowHearts;
        Zelda3D_HudRect(mx - 1.0f * sc, my - 1.0f * sc, mbw + 2.0f * sc, mbh + 2.0f * sc, 0x101820D0u); // frame
        int fillPx = (int)(mbw * s->magic / s->magicCapacity);
        if (fillPx < 0) fillPx = 0;
        if (fillPx > (int)mbw) fillPx = (int)mbw;
        Zelda3D_HudRect(mx, my, mbw, mbh, 0x00000080u);          // empty track
        Zelda3D_HudRect(mx, my, (float)fillPx, mbh, 0x32D232FFu); // green fill
    }

    // ---- Rupees (bottom-left): real OoT3D 3DS rupee gem (hud_all atlas) + digit glyphs --------
    {
        const float gem = 30.0f * sc;
        float rx = margin;
        float ry = (float)H - margin - gem;
        // Rupee sub-rect in the 256x256 hud_all atlas (the teal/gold gem), measured from the decode
        // (gem body x[131,171]; excludes the C-button at x<124 and the d-pad red arrow at x>=180).
        const int RX = 131, RY = 8, RW = 41, RH = 46;
        int aw = 0, ah = 0;
        const void* hudAtlas = Zelda3D_OoT3dAtlas(ZELDA3D_HUD_ALL_CTXB, 0, &aw, &ah);
        float gemW = gem * (float)RW / (float)RH; // preserve the gem's aspect
        Zelda3D_HudBlitAtlas(hudAtlas, aw, ah, RX, RY, RW, RH, rx, ry, gemW, gem, 0xFFFFFFFFu);
        float dx = rx + gemW + 4.0f * sc;
        const float dh = gem;
        char buf[8];
        int rup = s->rupees;
        if (rup < 0) rup = 0;
        if (rup > 999) rup = 999;
        snprintf(buf, sizeof(buf), "%d", rup);
        for (const char* p = buf; *p; p++) {
            int dw = 0, dhh = 0;
            const void* dtex = Zelda3D_DigitTex(*p - '0', &dw, &dhh);
            if (dtex && dw > 0 && dhh > 0) {
                float dwpx = dh * (float)dw / (float)dhh; // keep glyph aspect
                Zelda3D_HudBlit(dtex, dw, dhh, dx, ry, dwpx, dh, 0xFFFFFFFFu);
                dx += dwpx + 1.0f * sc;
            }
        }
    }

    // ---- Hotbar (top-right corner, user-requested): 6 slots with item icons + slot glyphs -----
    {
        extern const void* Zelda3D_NumGlyphTex(char which, int* w, int* h);
        extern const void* Zelda3D_XboxGlyphTex(char which, int* w, int* h);
        const int NSLOTS = 6;
        const float slot = 54.0f * sc;
        const float sgap = 6.0f * sc;
        const float totalW = NSLOTS * slot + (NSLOTS - 1) * sgap;
        float bx = (float)W - margin - totalW; // right-aligned
        float by = margin;                      // top
        int kbd = (s->inputDevice == 1);
        static const char kPadGlyph[6] = { 'B', 'Y', 'A', 'B', 'X', 'Y' };
        for (int i = 0; i < NSLOTS; i++) {
            float sx = bx + i * (slot + sgap);
            int active = (i == s->hotbarActive);
            if (active) {
                float b = 3.0f * sc; // gold border behind the slot
                Zelda3D_HudRect(sx - b, by - b, slot + 2 * b, slot + 2 * b, 0xFFD24FFFu);
            }
            Zelda3D_HudRect(sx, by, slot, slot, active ? 0x282420E0u : 0x14141EC0u); // slot panel
            int itemId = s->hotbarItems[i];
            if (itemId != 0xFF && itemId >= 0) {
                float pad = 5.0f * sc;
                // Prefer the real OoT3D 3DS item icon (icon_item_menu atlas; cell index == item id).
                int row = itemId / ZELDA3D_ITEM_COLS, col = itemId % ZELDA3D_ITEM_COLS;
                int iw = 0, ih = 0;
                const void* itemAtlas = Zelda3D_OoT3dAtlas(ZELDA3D_ICON_ITEM_CTXB, 0, &iw, &ih);
                int cellY = ZELDA3D_ITEM_ORIGIN + row * ZELDA3D_ITEM_PITCH;
                if (itemAtlas != NULL && iw > 0 && ih > 0 && cellY + ZELDA3D_ITEM_CELL <= ih) {
                    int cellX = ZELDA3D_ITEM_ORIGIN + col * ZELDA3D_ITEM_PITCH;
                    Zelda3D_HudBlitAtlas(itemAtlas, iw, ih, cellX, cellY, ZELDA3D_ITEM_CELL, ZELDA3D_ITEM_CELL,
                                       sx + pad, by + pad, slot - 2 * pad, slot - 2 * pad, 0xFFFFFFFFu);
                } else if (itemId < 158 && gItemIcons[itemId] != NULL) {
                    // Fallback for item ids beyond the 3DS atlas grid (quest/equipment icons).
                    Zelda3D_HudBlit(gItemIcons[itemId], 32, 32, sx + pad, by + pad, slot - 2 * pad,
                                  slot - 2 * pad, 0xFFFFFFFFu);
                }
            }
            // Slot glyph badge, top-right corner.
            int gw = 0, gh = 0;
            const void* glyph = NULL;
            if (kbd) {
                glyph = Zelda3D_NumGlyphTex((char)('1' + i), &gw, &gh);
            } else {
                glyph = Zelda3D_XboxGlyphTex(kPadGlyph[i], &gw, &gh);
            }
            if (glyph && gw > 0 && gh > 0) {
                float bsz = 20.0f * sc;
                Zelda3D_HudBlit(glyph, gw, gh, sx + slot - bsz - 1.0f * sc, by + 1.0f * sc, bsz, bsz,
                              0xFFFFFFFFu);
            }
        }
    }

    Zelda3D_Hud_End();
}

// #31 — substitute crisp higher-res HUD textures (hearts) for the blocky 16x16 N64 ones.
// -1 = uninit (read ZELDA3D_HUDTEX env, default on). z_lifemeter.c reads this and swaps the heart
// texture/load size/texcoords; see Zelda3D_HeartTex.
int gZelda3dHudTex = -1;
int Zelda3D_HudTexEnabled(void) {
    if (gZelda3dHudTex < 0) {
        const char* v = getenv("ZELDA3D_HUDTEX");
        gZelda3dHudTex = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dHudTex;
}
