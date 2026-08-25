// Draw-frame player pose discontinuity observation.
#ifndef ZELDA3D_PLAYER_POSE_SCAN_H
#define ZELDA3D_PLAYER_POSE_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_LinkModelId(void);
void Zelda3D_PoseScanSetActive(int on);
int Zelda3D_PoseScanCount(void);
float Zelda3D_PoseScanGet(int index, int* bone, float* frame, const char** csab);

#ifdef __cplusplus
}

namespace Zelda3D {

class LinkPoseScan {
  public:
    void setActive(int on);
    int count() const {
        return mCount;
    }
    float get(int index, int* bone, float* frame, const char** csab);
    void record(int modelId, const char* csab, float frame);

  private:
    struct Rec {
        float deg;
        int bone;
        float frame;
        char csab[28];
    };

    Rec mLog[512];
    int mCount = 0;
    int mActive = 0;
};

} // namespace Zelda3D
#endif

#endif // ZELDA3D_PLAYER_POSE_SCAN_H
