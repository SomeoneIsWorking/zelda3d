// Zelda3D — Link's FACIAL animation (eye + mouth), ported from OoT3D. See zelda3d_link_face.cpp.
#ifndef ZELDA3D_LINK_FACE_H
#define ZELDA3D_LINK_FACE_H

#ifdef __cplusplus
extern "C" {
#endif

// Drive Link's eye/mouth materials for THIS frame from the clip the draw path just resolved.
// Call once per player draw, AFTER the animation update (it reads the resolved CSAB + playhead).
void Zelda3D_LinkFaceUpdate(int modelId);
int Zelda3D_FacebSample(int modelId, const char* animName, float frame, int* outEye, int* outMouth);
int Zelda3D_FacialMaterialIndex(int modelId, int slot);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_LINK_FACE_H
