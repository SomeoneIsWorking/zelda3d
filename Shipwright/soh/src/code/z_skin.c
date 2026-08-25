#include "global.h"

MtxF gSkinLimbMatrices[60]; // holds matrices for each limb of the skeleton currently being drawn

static s32 sUnused;

void Skin_UpdateVertices(MtxF* mtx, SkinVertex* skinVertices, SkinLimbModif* modifEntry, Vtx* vtxBuf, Vec3f* pos) {
    Vtx* vtx;
    SkinVertex* vertexEntry;
    f32 xwTemp;
    f32 ywTemp;
    f32 zwTemp;
    Vec3f normal;
    Vec3f sp64;

    for (vertexEntry = skinVertices; vertexEntry < &skinVertices[modifEntry->vtxCount]; vertexEntry++) {
        vtx = &vtxBuf[vertexEntry->index];

        vtx->n.ob[0] = pos->x;
        vtx->n.ob[1] = pos->y;
        vtx->n.ob[2] = pos->z;

        xwTemp = mtx->xw;
        ywTemp = mtx->yw;
        zwTemp = mtx->zw;

        mtx->xw = mtx->yw = mtx->zw = 0.0f;

        sp64.x = vertexEntry->normX;
        sp64.y = vertexEntry->normY;
        sp64.z = vertexEntry->normZ;

        SkinMatrix_Vec3fMtxFMultXYZ(mtx, &sp64, &normal);

        vtx->n.n[0] = normal.x;
        vtx->n.n[1] = normal.y;
        vtx->n.n[2] = normal.z;

        mtx->xw = xwTemp;
        mtx->yw = ywTemp;
        mtx->zw = zwTemp;
    }
}

void Skin_ApplyLimbModifications(GraphicsContext* gfxCtx, Skin* skin, s32 limbIndex, s32 arg3) {
    s32 modifCount;
    SkinLimb** skeleton;
    SkinLimb* limb;
    SkinAnimatedLimbData* data;
    SkinLimbModif* modif;
    SkinLimbVtx* vtxEntry;
    s32 transformCount;
    f32 scale;
    SkinVertex* skinVertices;
    SkinTransformation* limbTransformations;
    Vtx* vtxBuf;
    SkinLimbModif* modifications;
    Vec3f vtxPoint;
    Vec3f spD0;
    SkinTransformation* transformationEntry;

    OPEN_DISPS(gfxCtx);

    skeleton = (SkinLimb**)SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);
    data = SEGMENTED_TO_VIRTUAL(((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]))->segment);
    modifications = (SkinLimbModif*)SEGMENTED_TO_VIRTUAL(data->limbModifications);

    vtxEntry = &skin->vtxTable[limbIndex];
    vtxBuf = vtxEntry->buf[vtxEntry->index];
    modifCount = data->limbModifCount;

    for (modif = modifications; modif < modifications + modifCount; modif++) {
        transformCount = modif->transformCount;
        skinVertices = (SkinVertex*)SEGMENTED_TO_VIRTUAL(modif->skinVertices);
        limbTransformations = (SkinTransformation*)SEGMENTED_TO_VIRTUAL(modif->limbTransformations);

        if (transformCount == 1) {
            Vec3f spAC;

            spAC.x = limbTransformations[0].x;
            spAC.y = limbTransformations[0].y;
            spAC.z = limbTransformations[0].z;

            SkinMatrix_Vec3fMtxFMultXYZ(&gSkinLimbMatrices[limbTransformations[0].limbIndex], &spAC, &vtxPoint);
        } else if (arg3 == 1) {
            Vec3f spA0;

            transformationEntry = &limbTransformations[modif->unk_4];

            spA0.x = transformationEntry->x;
            spA0.y = transformationEntry->y;
            spA0.z = transformationEntry->z;

            SkinMatrix_Vec3fMtxFMultXYZ(&gSkinLimbMatrices[transformationEntry->limbIndex], &spA0, &vtxPoint);
        } else {
            Vec3f phi_f20;
            Vec3f sp88;

            phi_f20.x = phi_f20.y = phi_f20.z = 0.0f;

            for (transformationEntry = &limbTransformations[0];
                 transformationEntry < &limbTransformations[transformCount]; transformationEntry++) {
                scale = transformationEntry->scale * 0.01f;

                sp88.x = transformationEntry->x;
                sp88.y = transformationEntry->y;
                sp88.z = transformationEntry->z;

                SkinMatrix_Vec3fMtxFMultXYZ(&gSkinLimbMatrices[transformationEntry->limbIndex], &sp88, &spD0);

                spD0.x *= scale;
                spD0.y *= scale;
                spD0.z *= scale;

                phi_f20.x += spD0.x;
                phi_f20.y += spD0.y;
                phi_f20.z += spD0.z;
            }

            vtxPoint.x = phi_f20.x;
            vtxPoint.y = phi_f20.y;
            vtxPoint.z = phi_f20.z;
        }

        Skin_UpdateVertices(&gSkinLimbMatrices[limbTransformations[modif->unk_4].limbIndex], skinVertices, modif,
                            vtxBuf, &vtxPoint);
    }

    gSPSegment(POLY_OPA_DISP++, 0x08, vtxEntry->buf[vtxEntry->index]);

    vtxEntry->index = (vtxEntry->index == 0) ? 1 : 0;

    CLOSE_DISPS(gfxCtx);
}

/**
 * Draw a limb of type SKIN_LIMB_TYPE_ANIMATED, of the skeleton `skin` at index `limbIndex`
 * The vertices of this limb are modified dynamically
 */
void Skin_DrawAnimatedLimb(GraphicsContext* gfxCtx, Skin* skin, s32 limbIndex, s32 arg3, s32 drawFlags) {
    SkinLimb** skeleton;
    SkinAnimatedLimbData* data;

    OPEN_DISPS(gfxCtx);

    skeleton = SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);
    data = SEGMENTED_TO_VIRTUAL(((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]))->segment);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_TRANSFORMS)) {
        Skin_ApplyLimbModifications(gfxCtx, skin, limbIndex, arg3);
    }

    gSPDisplayList(POLY_OPA_DISP++, data->dlist);

    CLOSE_DISPS(gfxCtx);
}

/**
 * Draw a limb of type SKIN_LIMB_TYPE_NORMAL, of the skeleton `skin` at index `limbIndex`
 */
void Skin_DrawLimb(GraphicsContext* gfxCtx, Skin* skin, s32 limbIndex, Gfx* dlistOverride, s32 drawFlags) {
    Gfx* gfx = dlistOverride;
    SkinLimb** skeleton;
    s32 pad;

    OPEN_DISPS(gfxCtx);

    skeleton = SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);

    if (dlistOverride == NULL) {
        gfx = ((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]))->segment;
    }

    if (gfx != NULL) {
        Mtx* mtx = SkinMatrix_MtxFToNewMtx(gfxCtx, &gSkinLimbMatrices[limbIndex]);

        if (mtx != NULL) {
            gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, gfx);
            gSPPopMatrix(POLY_OPA_DISP++, G_MTX_MODELVIEW);
            gDPPipeSync(POLY_OPA_DISP++);
        }
    }

    CLOSE_DISPS(gfxCtx);
}

void Skin_DrawImpl(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                   SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation, s32 arg6, s32 drawFlags) {
    s32 i;
    s32 segmentType;
    SkinLimb** skeleton;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    s32 pad;

    OPEN_DISPS(gfxCtx);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_TRANSFORMS)) {
        Skin_ApplyAnimTransformations(skin, gSkinLimbMatrices, actor, setTranslation);
    }

    skeleton = SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);

    // Zelda3D: the Skin system (En_Horse/En_fHG/En_Horse_* and En_Viewer's procedural skinned
    // meshes) has its own limb walk distinct from z_skelanime.c's SkelAnime_Draw* family, so it
    // never hit any Zelda3D choke point -> these actors always drew the native N64 mesh even when
    // Zelda3D_TryAuto had already classified them "skinned"/AUTO-replaceable (dead classification;
    // see oot3d-decomp/docs/en_horse_epona_render_gap.md). Skin embeds a REAL `SkelAnime
    // skelAnime` (z64skin.h), driven every frame by the actor's own SkelAnime_Update/
    // Animation_Change calls exactly like any SkelAnime-based actor (z_en_horse.c switches
    // `skin.skelAnime.animation` per live gait: idle/walk/trot/gallop) -- so it carries the same
    // .animation/.curFrame/.animLength/.morphWeight/.skeleton/.jointTable fields the existing
    // SkelAnime*-bearing choke point (Zelda3D_SkelAnimeDraw) already consumes to phase-lock AND
    // gait-select the OoT3D CSAB (Zelda3D_UpdateAnimAuto's N64-anim-name -> CSAB map, e.g.
    // gEponaGallopingAnim -> hl_anim_fastrun2_30). SkinLimb's jointPos/child/sibling fields also
    // share StandardLimb's exact byte layout (see z64skin.h vs z64animation.h), so the walk/retarget
    // machinery needs no Skin-specific variant. Run this AFTER Skin_ApplyAnimTransformations (so
    // postDraw's Skin_GetLimbPos reads still see a valid gSkinLimbMatrices, e.g. EnHorse_PostDraw's
    // hoof-dust spawn) but BEFORE any N64 limb Gfx is emitted.
    if (Zelda3D_SkelAnimeDraw(play, &skin->skelAnime)) {
        if (postDraw != NULL) {
            postDraw(actor, play, skin);
        }
        goto close_disps;
    }

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_MATRIX)) {
        Mtx* mtx;

        gSPMatrix(POLY_OPA_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        mtx = SkinMatrix_MtxFToNewMtx(gfxCtx, &skin->mtx);

        if (mtx == NULL) {
            goto close_disps;
        }

        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    }

    for (i = 0; i < skin->skeletonHeader->limbCount; i++) {
        s32 shouldDraw = true;

        if (overrideLimbDraw != NULL) {
            shouldDraw = overrideLimbDraw(actor, play, i, skin);
        }

        segmentType = ((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[i]))->segmentType;

        if (segmentType == SKIN_LIMB_TYPE_ANIMATED && shouldDraw == true) {
            Skin_DrawAnimatedLimb(gfxCtx, skin, i, arg6, drawFlags);
        } else if (segmentType == SKIN_LIMB_TYPE_NORMAL && shouldDraw == true) {
            Skin_DrawLimb(gfxCtx, skin, i, NULL, drawFlags);
        }
    }

    if (postDraw != NULL) {
        postDraw(actor, play, skin);
    }

close_disps:
    CLOSE_DISPS(gfxCtx);
}

// allows specifying PostLimbDraw and setTranslation
void func_800A6330(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw, s32 setTranslation) {
    Skin_DrawImpl(actor, play, skin, postDraw, NULL, setTranslation, false, 0);
}

// allows specifying OverrideLimbDraw, PostLimbDraw and setTranslation
void func_800A6360(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                   SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation) {
    Skin_DrawImpl(actor, play, skin, postDraw, overrideLimbDraw, setTranslation, false, 0);
}

// allows specifying OverrideLimbDraw, PostLimbDraw, setTranslation, and arg6
void func_800A6394(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                   SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation, s32 arg6) {
    Skin_DrawImpl(actor, play, skin, postDraw, overrideLimbDraw, setTranslation, arg6, 0);
}

// allows specifying all variables
void func_800A63CC(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                   SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation, s32 arg6, s32 drawFlags) {
    Skin_DrawImpl(actor, play, skin, postDraw, overrideLimbDraw, setTranslation, arg6, drawFlags);
}

void Skin_GetLimbPos(Skin* skin, s32 limbIndex, Vec3f* offset, Vec3f* dst) {
    MtxF mtxf;

    SkinMatrix_MtxFMtxFMult(&skin->mtx, &gSkinLimbMatrices[limbIndex], &mtxf);
    SkinMatrix_Vec3fMtxFMultXYZ(&mtxf, offset, dst);
}
