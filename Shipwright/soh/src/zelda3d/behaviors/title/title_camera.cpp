#include "title_camera.h"

#include "title_activity.h"
#include "../../cutscene/zelda3d_cutscene.h"

extern "C" {
const float kZelda3dTitleEye[3] = { -4071.49f, 57.81f, 5217.30f };
const float kZelda3dTitleAt[3] = { -4939.49f, 252.81f, 5675.30f };
const float kZelda3dTitleUp[3] = { 0.212f, 0.977f, -0.014f };
}

namespace {

int ResolveCamera(float eye[3], float at[3], float up[3], float* fov) {
    const int frame = Zelda3D_TitleCsFrame();
    const float fractionalFrame = static_cast<float>(frame) + Zelda3D_TitleCsSubframe();
    int live = Zelda3D_TitleCsCamera(fractionalFrame, eye, at, up, fov);
    if (!live) {
        live = Zelda3D_TitleCsCamera(frame > 0 ? static_cast<float>(frame - 1) : 1.0f, eye, at, up, fov);
    }
    return live;
}

void UseFallbackCamera(float eye[3], float at[3], float up[3], float* fov) {
    for (int i = 0; i < 3; ++i) {
        eye[i] = kZelda3dTitleEye[i];
        at[i] = kZelda3dTitleAt[i];
        up[i] = kZelda3dTitleUp[i];
    }
    *fov = 48.803f;
}

void ApplyCameraToView(PlayState* play, const float eye[3], const float at[3], const float up[3], float fov) {
    play->view.eye = { eye[0], eye[1], eye[2] };
    play->view.lookAt = { at[0], at[1], at[2] };
    play->view.up = { up[0], up[1], up[2] };
    play->view.fovy = fov;

    const int cameraIndex = play->activeCamera;
    if (cameraIndex < 0 || cameraIndex >= NUM_CAMS) {
        return;
    }
    Camera* camera = play->cameraPtrs[cameraIndex];
    if (camera == nullptr) {
        return;
    }
    camera->eye = play->view.eye;
    camera->eyeNext = camera->eye;
    camera->at = play->view.lookAt;
    camera->up = play->view.up;
    camera->fov = fov;
}

} // namespace

namespace Zelda3D {

void UpdateTitleCamera(PlayState* play) {
    float eye[3];
    float at[3];
    float up[3];
    float fov = 0.0f;
    int live = 0;

    if (Zelda3D_TitleCsLoad()) {
        Zelda3D_TitleCsAdvance();
        live = ResolveCamera(eye, at, up, &fov);
    }
    if (!live) {
        UseFallbackCamera(eye, at, up, &fov);
    }
    ApplyCameraToView(play, eye, at, up, fov);
}

} // namespace Zelda3D

extern "C" int Zelda3D_Title_CameraState(float* outEye, float* outAt, float* outUp, float* outFov) {
    if (!Zelda3D_Title_IsActive()) {
        return 0;
    }

    float eye[3] = {};
    float at[3] = {};
    float up[3] = {};
    float fov = 0.0f;
    const int live = ResolveCamera(eye, at, up, &fov);
    for (int i = 0; i < 3; ++i) {
        if (outEye != nullptr) {
            outEye[i] = eye[i];
        }
        if (outAt != nullptr) {
            outAt[i] = at[i];
        }
        if (outUp != nullptr) {
            outUp[i] = up[i];
        }
    }
    if (outFov != nullptr) {
        *outFov = fov;
    }
    return live ? 1 : 2;
}
