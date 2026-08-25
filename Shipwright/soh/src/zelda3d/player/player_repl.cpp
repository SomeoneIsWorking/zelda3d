#include "../anim/automatic_playback.h"
#include "functions/game_state.h"
#include "functions/player.h"
#include "../anim/pose_inspection.h"
#include "../anim/skeleton_draw_bridge.h"
#include "../core/zelda3d_log.h"
#include "../diagnostics/actor_selection.h"
#include "../repl/zelda3d_repl.h"
#include "player_behavior.h"
#include "player_draw_policy.h"
#include "player_pose_scan.h"
#include "player_retarget.h"
#include "zelda3d_link.h"
#include "zelda3d_link_face.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static inline Zelda3D::PlayerBehavior& P() {
    return Zelda3D::PlayerBehavior::instance();
}

// Write one N64 limb to a file in tools/zelda3d_skel_match.py's load_n64() format (REPL linkskeldump).
static void Zelda3D_DumpLimbFileCb(int limbIndex, StandardLimb* lb, void* ud) {
    FILE* f = (FILE*)ud;
    fprintf(f, "N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d\n", limbIndex, lb->jointPos.x, lb->jointPos.y,
            lb->jointPos.z, lb->child, lb->sibling);
}

// Handle a `link*` REPL command. Returns 1 if handled (cmd was a link command), 0 otherwise.
int Zelda3D::PlayerBehavior::repl(PlayState* play, const char* cmd, const char* line, const char* outPath) {
    char path[1024];
    float f1, f2, f3;
    int iv;
    if (strcmp(cmd, "linkgrab") == 0) {
        // #6/#9 — drive a REAL cucco pickup: `asel 0x19` first, then `linkgrab [frames]` (default
        // 60). Holds the selected actor in front of Link + injects A edges until heldActor is set,
        // so the genuine grab/lift action fires (B then shows Throw). afreeze should be 0.
        int frames = 60;
        sscanf(line, "%*s %d", &frames);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "linkgrab: no actor selected (asel 0x19 first)");
        } else {
            gZelda3dActorFreeze = 0; // the driver positions it every frame; freeze would fight that
            P().grab.start(frames);
            Zelda3D_ReplReply(outPath, "linkgrab: driving pickup of id=0x%X for %d frames", gZelda3dSelActor->id,
                              frames);
        }
    } else if (strcmp(cmd, "linkskeldump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // Dump the live PLAYER N64 skeleton (limb tree + jointPos) to a file in the
        // zelda3d_skel_match.py load_n64() format, to DERIVE the OoT3D-bone -> N64-limb bonemap for
        // the N64-retarget Link mode (linksrc n64). One-shot, on demand.
        Player* pl = GET_PLAYER(play);
        if (pl == NULL || pl->skelAnime.skeleton == NULL || pl->skelAnime.limbCount <= 0) {
            Zelda3D_ReplReply(outPath, "linkskeldump: no player skeleton");
        } else {
            FILE* sf = fopen(path, "w");
            if (sf == NULL) {
                Zelda3D_ReplReply(outPath, "linkskeldump: cannot open %s", path);
            } else {
                fprintf(sf, "# player N64 skeleton; limbCount=%d age=%d\n", pl->skelAnime.limbCount, LINK_AGE_IN_YEARS);
                Zelda3D_WalkN64Skeleton((void**)pl->skelAnime.skeleton, pl->skelAnime.limbCount, Zelda3D_DumpLimbFileCb,
                                        sf);
                fclose(sf);
                Zelda3D_ReplReply(outPath, "linkskeldump -> %s (limbCount=%d age=%d)", path, pl->skelAnime.limbCount,
                                  LINK_AGE_IN_YEARS);
            }
        }
    } else if (strcmp(cmd, "linktrace") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Routed through the diagnostic-logger registry (equivalent to `log link <0|1>`).
        Zelda3D_LogSet("link", iv ? 1 : 0);
        Zelda3D_ReplReply(outPath, "linktrace=%d (per-draw held/carryWalk/lower+upper anim/csab -> run.log)",
                          iv ? 1 : 0);
    } else if (strcmp(cmd, "link") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dLinkOn = iv ? 1 : 0;
        Zelda3D_ReplReply(outPath, "link=%d (OoT3D player body replacement; 1 = the shipped default)", gZelda3dLinkOn);
    } else if (strcmp(cmd, "linkscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dLinkScale = f1;
        Zelda3D_ReplReply(outPath, "linkscale=%.5f (OoT3D-link-local -> N64 player world units)", gZelda3dLinkScale);
    } else if (strcmp(cmd, "linkloco") == 0) {
        // #7 calibrate the speed->CSAB-cadence gain for Link's locomotion cycle. `linkloco <gain>`
        // sets it; no-arg reports it + Link's live speedXZ (so the cadence can be tuned vs N64).
        if (sscanf(line, "%*s %f", &f1) == 1) {
            gZelda3dLinkLocoGain = f1;
        }
        Zelda3D_ReplReply(outPath, "linkloco gain=%.3f (CSAB frames/draw per speed unit); link speedXZ=%.2f",
                          gZelda3dLinkLocoGain, GET_PLAYER(play)->actor.speedXZ);
    } else if (strcmp(cmd, "linkfreeze") == 0) {
        // #7 hand-weave: freeze Link's live idle pose so `linkcorr` tweaks are the only variable.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            if (iv) {
                P().retarget.freezeReq = 1;
                P().retarget.frozenCount = 0;
            } else {
                P().retarget.frozenCount = 0;
                P().retarget.freezeReq = 0;
            }
        }
        Zelda3D_ReplReply(outPath, "linkfreeze=%d (frozenLimbs=%d, req=%d)", P().retarget.frozenCount > 0 ? 1 : 0,
                          P().retarget.frozenCount, P().retarget.freezeReq);
    } else if (strcmp(cmd, "linkpin") == 0) {
        // #8 hand-weave: pin Link's world pos + facing yaw so the side-profile view is identical
        // across `linkcorr` tweaks (linkfreeze alone leaves the actor free to idle-turn -> desync).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            Player* pl = GET_PLAYER(play);
            if (iv) {
                P().transformPin.set(pl, true);
            } else {
                P().transformPin.set(pl, false);
            }
        }
        Zelda3D_ReplReply(outPath, "linkpin=%d (pins player world pos+yaw each frame)", P().transformPin.enabled());
    } else if (strcmp(cmd, "linkcorr") == 0) {
        // #7 HAND-WEAVE the per-bone arm/upper-body retarget correction LIVE (linksrc n64).
        //   linkcorr                         -> show upper-body bones (b9..b20): mode + C euler (zyx deg)
        //   linkcorr set <bid> <mode> <cx> <cy> <cz> [<c2x> <c2y> <c2z>]
        //                                    -> set bone bid (mode 1=replace 2=C·R 3=R·C 4=C·R·C2 5=C·R·C⁻¹ conj)
        //   linkcorr reset                   -> restore the generated table
        //   linkcorr bake <path>             -> write the live table as a 25-row .inc for committing
        char sub[64] = "";
        P().retarget.ensure();
        int n = sscanf(line, "%*s %63s", sub);
        if (n == 1 && strcmp(sub, "reset") == 0) {
            P().retarget.reset();
            Zelda3D_ReplReply(outPath, "linkcorr reset to generated table");
        } else if (n == 1 && strcmp(sub, "set") == 0) {
            int bid = -1, mode = 0;
            float c[3] = { 0, 0, 0 }, c2[3] = { 0, 0, 0 };
            int got = sscanf(line, "%*s %*s %i %i %f %f %f %f %f %f", &bid, &mode, &c[0], &c[1], &c[2], &c2[0], &c2[1],
                             &c2[2]);
            if (got >= 5 && bid >= 0 && bid < 25) {
                P().retarget.table[bid].mode = (unsigned char)mode;
                Zelda3D_EulerToMat3(c[0], c[1], c[2], P().retarget.table[bid].C);
                Zelda3D_EulerToMat3(c2[0], c2[1], c2[2], P().retarget.table[bid].C2);
                Zelda3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d C=(%.1f,%.1f,%.1f) C2=(%.1f,%.1f,%.1f)", bid,
                                  P().retarget.table[bid].limb, mode, c[0], c[1], c[2], c2[0], c2[1], c2[2]);
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr set <bid> <mode> <cx> <cy> <cz> [c2x c2y c2z]");
            }
        } else if (n == 1 && strcmp(sub, "limb") == 0) {
            // #8 remap test: change which N64 jointTable limb drives an OoT3D bone, live (the
            // .inc's static limb field is otherwise the only authority). Lets us test the
            // structural-mismatch fix (OoT3D head b11 <- N64 head limb 10, chest b10 <- torso
            // limb 9) without a rebuild-per-guess. -1 = rest (no live limb).
            int bid = -1, limb = -2;
            if (sscanf(line, "%*s %*s %i %i", &bid, &limb) == 2 && bid >= 0 && bid < 25) {
                P().retarget.table[bid].limb = (signed char)limb;
                Zelda3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d", bid, P().retarget.table[bid].limb,
                                  P().retarget.table[bid].mode);
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr limb <bid> <n64limb|-1>");
            }
        } else if (n == 1 && strcmp(sub, "bake") == 0) {
            char path[1024] = "";
            if (sscanf(line, "%*s %*s %1023s", path) == 1) {
                FILE* f = fopen(path, "w");
                if (!f) {
                    Zelda3D_ReplReply(outPath, "linkcorr bake: cannot open %s", path);
                } else {
                    fprintf(f, "// HAND-WOVEN by REPL `linkcorr` (#7 long-arm) — arms/upper body tuned by\n");
                    fprintf(f, "// hand vs the OoT3D CSAB ground truth; legs/neck on pure replace. NOT auto-fit.\n");
                    fprintf(f, "// Per OoT3D childlink_v2 bone: { n64Limb, mode, C[9], C2[9] }. mode 0=rest,\n");
                    fprintf(f, "// 1=replace, 2=left C·R, 3=right R·C, 4=two-sided C·R·C2.\n");
                    fprintf(f, "static const Zelda3dBoneCorr kLinkChildBoneCorr[25] = {\n");
                    for (int b = 0; b < 25; b++) {
                        const Zelda3dBoneCorr* bc = &P().retarget.table[b];
                        fprintf(f, "    { %3d, %d, {", bc->limb, bc->mode);
                        for (int k = 0; k < 9; k++)
                            fprintf(f, "%s%.6ff", k ? "," : "", bc->C[k]);
                        fprintf(f, " }, {");
                        for (int k = 0; k < 9; k++)
                            fprintf(f, "%s%.6ff", k ? "," : "", bc->C2[k]);
                        fprintf(f, " } }, // b%d\n", b);
                    }
                    fprintf(f, "};\n");
                    fclose(f);
                    Zelda3D_ReplReply(outPath, "linkcorr baked -> %s", path);
                }
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr bake <path>");
            }
        } else {
            // show: dump upper-body bones with their C (and C2) decomposed to zyx euler degrees.
            char buf[1024];
            int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "linkcorr (upper body):");
            for (int b = 9; b <= 20 && off < (int)sizeof(buf) - 80; b++) {
                float e[3], e2[3];
                Zelda3D_Mat3ToEuler(P().retarget.table[b].C, e);
                Zelda3D_Mat3ToEuler(P().retarget.table[b].C2, e2);
                off += snprintf(buf + off, sizeof(buf) - off, " b%d:l%d m%d C(%.0f,%.0f,%.0f)", b,
                                P().retarget.table[b].limb, P().retarget.table[b].mode, e[0], e[1], e[2]);
                if (P().retarget.table[b].mode == 4)
                    off += snprintf(buf + off, sizeof(buf) - off, "/C2(%.0f,%.0f,%.0f)", e2[0], e2[1], e2[2]);
            }
            Zelda3D_ReplReply(outPath, "%s", buf);
        }
    } else if (strcmp(cmd, "linkrot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gZelda3dLinkRotX = f1;
        gZelda3dLinkRotY = f2;
        gZelda3dLinkRotZ = f3;
        Zelda3D_ReplReply(outPath, "linkrot=(%.0f,%.0f,%.0f)", gZelda3dLinkRotX, gZelda3dLinkRotY, gZelda3dLinkRotZ);
    } else if (strcmp(cmd, "linkanim") == 0) {
        // `linkanim <csab-base>` pins that CSAB on Link (verify idle/walk/run without real input);
        // `linkanim off` / no-arg returns to live anim resolution.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gZelda3dLinkForceCsab, name, sizeof(gZelda3dLinkForceCsab) - 1);
            gZelda3dLinkForceCsab[sizeof(gZelda3dLinkForceCsab) - 1] = '\0';
            Zelda3D_ReplReply(outPath, "linkanim='%s' (forced on Link; `linkanim off` to release)",
                              gZelda3dLinkForceCsab);
        } else {
            gZelda3dLinkForceCsab[0] = '\0';
            Zelda3D_ReplReply(outPath, "linkanim OFF (live anim resolution restored)");
        }
    } else if (strcmp(cmd, "linkframe") == 0) {
        // `linkframe <f>` pins the CSAB playhead of the `linkanim`-forced clip so a facial/pose frame
        // can be observed deterministically instead of waiting for the live phase to sweep past it.
        // `linkframe off` / no-arg releases (live phase-lock).
        char arg[32] = "";
        if (sscanf(line, "%*s %31s", arg) == 1 && strcmp(arg, "off") != 0) {
            gZelda3dLinkForceFrame = (float)atof(arg);
        } else {
            gZelda3dLinkForceFrame = -1.0f;
        }
        Zelda3D_ReplReply(outPath, "linkframe=%.2f (needs `linkanim <csab>`; -1 = live)", gZelda3dLinkForceFrame);
    } else if (strcmp(cmd, "linkface") == 0) {
        // #201d verification: report the live facial state — which clip+playhead the draw resolved,
        // the `.faceb` indices sampled there, and the eye/mouth material slots being driven.
        int modelId = Zelda3D_LinkModelId();
        const char* csab = NULL;
        float frame = 0.0f;
        int have = Zelda3D_LastAutoAnim(modelId, &csab, &frame);
        int eye = -1, mouth = -1;
        int track = have ? Zelda3D_FacebSample(modelId, csab, frame, &eye, &mouth) : 0;
        Zelda3D_ReplReply(outPath,
                          "linkface model=%d csab=%s frame=%.2f faceb=%d eye=%d mouth=%d "
                          "eyeMat=%d mouthMat=%d",
                          modelId, (have && csab) ? csab : "(none)", frame, track, eye, mouth,
                          Zelda3D_FacialMaterialIndex(modelId, 0), Zelda3D_FacialMaterialIndex(modelId, 1));
    } else if (strcmp(cmd, "linktwo") == 0) {
        // `linktwo <lowerCsab> <upperCsab>` — force the #85 carry-WALK two-source per-limb blend with
        // explicit CSABs (lower drives legs, upper drives arms via kLinkUpperBodyMask), so the blend
        // can be skindumped/verified without a live grab. `linktwo off` releases.
        char lo[64] = "", up[64] = "";
        int got = sscanf(line, "%*s %63s %63s", lo, up);
        if (got >= 1 && strcmp(lo, "off") == 0) {
            gZelda3dLinkForceTwoLower[0] = '\0';
            gZelda3dLinkForceTwoUpper[0] = '\0';
            Zelda3D_ReplReply(outPath, "linktwo OFF");
        } else if (got == 2) {
            strncpy(gZelda3dLinkForceTwoLower, lo, sizeof(gZelda3dLinkForceTwoLower) - 1);
            strncpy(gZelda3dLinkForceTwoUpper, up, sizeof(gZelda3dLinkForceTwoUpper) - 1);
            Zelda3D_ReplReply(outPath, "linktwo lower='%s' upper='%s' (forced two-source; `linktwo off`)",
                              gZelda3dLinkForceTwoLower, gZelda3dLinkForceTwoUpper);
        } else {
            Zelda3D_ReplReply(outPath, "usage: linktwo <lowerCsab> <upperCsab> | off");
        }
    } else if (strcmp(cmd, "linkheldfix") == 0) {
        // #6 A/B toggle: attach the carried actor (held cucco) to 3DS Link's posed hands. Default on;
        // `linkheldfix 0` reverts to the engine's stale pickup-spot pos for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1)
            gZelda3dHeldAttach = (iv != 0);
        Zelda3D_ReplReply(outPath, "linkheldfix=%d (attach carried actor to posed hands)", gZelda3dHeldAttach);
    } else if (strcmp(cmd, "linkfocusfix") == 0) {
        // #16(b) A/B toggle: keep actor.focus.pos at the 3DS Link posed head so the first-person
        // (C-up) camera frames Link's eye. Default on; `linkfocusfix 0` reverts to the stale focus
        // (Player_DrawGameplay skipped) for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1)
            gZelda3dFocusFix = (iv != 0);
        Zelda3D_ReplReply(outPath, "linkfocusfix=%d (keep focus.pos at posed head)", gZelda3dFocusFix);
    } else if (strcmp(cmd, "linkjointdump") == 0) {
        // `linkjointdump <path> [nframes]` — capture the live player jointTable over nframes (default 60)
        // consecutive draws to a CSV, for the per-bone retarget-correction derivation. Pin Link to a
        // named anim first (`linkanim` only forces the 3DS CSAB display; the N64 jointTable still reflects
        // whatever named anim Link's SkelAnime is actually playing — keep him idle/standing for nml_wait).
        int n = 60;
        if (sscanf(line, "%*s %1023s %d", path, &n) >= 1) {
            if (!P().jointDump.start(path, n)) {
                Zelda3D_ReplReply(outPath, "linkjointdump: cannot open %s", path);
            } else {
                if (n < 1)
                    n = 1;
                Zelda3D_ReplReply(outPath, "linkjointdump -> %s (%d frames)", path, n);
            }
        } else {
            Zelda3D_ReplReply(outPath, "usage: linkjointdump <path> [nframes]");
        }
    } else if (strcmp(cmd, "skindump") == 0) {
        // `skindump <path> [nframes]` — capture the ACTUAL resolved per-bone CSAB pose (skin matrices)
        // the renderer draws for Link, over nframes (default 60) consecutive DRAWS, tagged with the
        // resolved CSAB name + the REAL playhead frame (free-run accumulator). This is the Zelda3D side of
        // the #117 direct-vs-oracle per-frame diff: it proves whether the pose actually cycles (slide
        // bug) and exposes the geometry for the bone-rotation diff. Unlike linkjointdump (the N64 input
        // jointTable) this is the OUTPUT geometry after CSAB resolution.
        int n = 60;
        if (sscanf(line, "%*s %1023s %d", path, &n) >= 1) {
            if (P().modelId < 0) {
                Zelda3D_ReplReply(outPath, "skindump: Link model not drawn yet (run `link 1` and let it draw)");
            } else {
                if (n < 1)
                    n = 1;
                Zelda3D_SkinDumpArm(P().modelId, path, n);
                Zelda3D_ReplReply(outPath, "skindump -> %s (%d draws, model %d)", path, n, P().modelId);
            }
        } else {
            Zelda3D_ReplReply(outPath, "usage: skindump <path> [nframes]");
        }
    } else if (strcmp(cmd, "linkmid") == 0) {
        // `linkmid <arg>` — debug override of Link's mesh_id visibility mask (childlink_v2 bakes all
        // hand/equipment variants on distinct mesh_ids). Used to identify each mesh_id by rendering it
        // alone. `only <n>` shows just mesh_id n; `add <n>`/`del <n>` toggle one; `0xHEX` sets the raw
        // mask; `all` shows everything; `auto` releases the override (back to the computed policy).
        char arg[32] = "";
        int n = 0;
        if (sscanf(line, "%*s %31s", arg) == 1) {
            if (strcmp(arg, "auto") == 0) {
                P().midmask.overrideSet = 0;
            } else if (strcmp(arg, "all") == 0) {
                P().midmask.overrideMask = ~0ull;
                P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "only") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                P().midmask.overrideMask = (n >= 0 && n < 64) ? (1ull << n) : 0ull;
                P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "add") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64)
                    P().midmask.overrideMask |= (1ull << n);
                P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "del") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64)
                    P().midmask.overrideMask &= ~(1ull << n);
                P().midmask.overrideSet = 1;
            } else {
                P().midmask.overrideMask = strtoull(arg, NULL, 0);
                P().midmask.overrideSet = 1;
            }
        }
        Zelda3D_ReplReply(outPath, "linkmid override=%s mask=0x%llx", P().midmask.overrideSet ? "ON" : "OFF(auto)",
                          P().midmask.overrideMask);
    } else if (strcmp(cmd, "linkgear") == 0) {
        // `linkgear <sword 0-3> <shield 0-3>` — equip a sword (0=none,1=kokiri,2=master,3=biggoron)
        // + shield (0=none,1=deku,2=hylian,3=mirror) on Link so the boy/adult equipment mids can be
        // verified (the debug save spawns child with deku+kokiri; adult has no Hylian shield/master
        // sword by default). Marks them owned, equips them, sets B to the sword; the player recomputes
        // currentShield from CUR_EQUIP_VALUE next frame (z_player_lib.c ~679).
        int sw = 2, sh = 2;
        sscanf(line, "%*s %i %i", &sw, &sh);
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BIGGORON) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR);
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, sw);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, sh);
        {
            static const s16 swItem[] = { ITEM_NONE, ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER, ITEM_SWORD_BGS };
            if (sw >= 0 && sw < 4)
                gSaveContext.equips.buttonItems[0] = swItem[sw];
        }
        // currentShield/currentSword are cached on the Player and only refreshed on equip events;
        // force the refresh now so the change takes effect this frame.
        Player_SetEquipmentData(play, GET_PLAYER(play));
        Zelda3D_ReplReply(outPath, "linkgear sword=%d shield=%d (equipped)", sw, sh);
    } else if (strcmp(cmd, "linksrc") == 0) {
        // `linksrc n64|3ds` — choose Link's animation source. n64 = retarget the live blended jointTable
        // (walk/run + everything), 3ds = the OoT3D rig's own named CSABs.
        char name[16] = "";
        if (sscanf(line, "%*s %15s", name) == 1) {
            gZelda3dLinkAnimSrc = (name[0] == '0' || name[0] == '3') ? 0 : 1;
        }
        Zelda3D_ReplReply(outPath, "linksrc=%s",
                          gZelda3dLinkAnimSrc == 1 ? "n64 (live jointTable retarget)" : "3ds (own CSAB)");
    } else {
        return 0; // not a link command
    }
    return 1;
}
