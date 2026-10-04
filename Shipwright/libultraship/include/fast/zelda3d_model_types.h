// POD data exchanged between Zelda3D model providers and native render backends.
#ifndef ZELDA3D_FAST_MODEL_TYPES_H
#define ZELDA3D_FAST_MODEL_TYPES_H

#define ZELDA3D_GL_MAX_BONES 64

typedef struct Zelda3DGlVtx {
    float pos[3];
    float nrm[3];
    float uv[2];
    float boneIds[4];
    float weights[4];
    float color[4];
    float uv1[2];
    float uv2[2];
} Zelda3DGlVtx;

typedef struct Zelda3DGlGroup {
    const Zelda3DGlVtx* verts;
    int vertCount;
    int texIndex;
    int alphaTest;
    float alphaRef;
    unsigned minFilter, magFilter;
    unsigned wrapS, wrapT;
    int blendEnable;
    unsigned blendSrcRGB, blendDstRGB, blendEqRGB;
    unsigned blendSrcA, blendDstA, blendEqA;
    float blendColor[4];
    int depthWrite;
    unsigned alphaFunc;
    int depthTest;
    unsigned depthFunc;
    float polygonOffset;
    int cull;
    int faceCull;
    int meshId;
    int materialIndex;
    int vertexLighting;
    int fragmentLighting;
    int hasColor;
    int fogEnabled;
    float matAmbient[3];
    float matDiffuse[4];
    // The rest of the authored CMB colour block, needed only by the PICA fixed-function
    // FRAGMENT lighting path (render.cmb-fragment-lighting). FUN_003fa5d0 forms four per-light
    // colour products per slot -- ambient (+0xA4), diffuse (+0xA8), specular_0 (+0xAC),
    // specular_1 (+0xB0) -- and also writes the material's EMISSION (+0xA0) to PICA's
    // `global_ambient` register. Emission, specular_0 and specular_1 used to stop at the parser
    // (cmb3d/asset/cmb_color_block.cpp), which is why the host had no specular anywhere and
    // FRAGMENT_SECONDARY could only ever be black. Authored, in [0,1], already /255.
    // See fast/zelda3d_fragment_lighting.h for the producer contract.
    float matEmission[3];
    float matSpecular0[3];
    float matSpecular1[3];
    float combScaleRGB;
    float matConstant[6][4];
    int combConstIdx;
    int combUsesConst;
    float combConstScaleRGB;
    int dualTexMode;
    float dualTexScale2;
    float uv0Scale[2];
    float uv0Trans[2];
    int coord0Mapping;
    int tex1Index;
    unsigned min1Filter, mag1Filter;
    unsigned wrap1S, wrap1T;
    float uv1Scale[2];
    float uv1Trans[2];
    int coord1Mapping;
    int tevGeneric;
    int tevStageCount;
    unsigned tevStagePack[6][3];
    int tex2Index;
    unsigned min2Filter, mag2Filter;
    unsigned wrap2S, wrap2T;
    float uv2Scale[2];
    float uv2Trans[2];
    int coord2Mapping;
} Zelda3DGlGroup;

typedef struct Zelda3DGlTex {
    const unsigned char* rgba;
    int w, h;
    int levels;
} Zelda3DGlTex;

#endif // ZELDA3D_FAST_MODEL_TYPES_H
