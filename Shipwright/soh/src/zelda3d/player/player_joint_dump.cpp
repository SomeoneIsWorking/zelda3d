#include "player_joint_dump.h"

#include <cstring>

Zelda3D::LinkJointDump::~LinkJointDump() {
    stop();
}

bool Zelda3D::LinkJointDump::start(const char* path, int frameCount) {
    stop();
    mFile = fopen(path, "w");
    if (mFile == nullptr) {
        return false;
    }

    fprintf(mFile, "# player jointTable capture; bonemap value m -> limb (m+1)\n");
    fprintf(mFile, "cap,curFrame,animLength,anim,limb,x,y,z\n");
    mRemaining = frameCount < 1 ? 1 : frameCount;
    mCaptureIndex = 0;
    return true;
}

void Zelda3D::LinkJointDump::capture(const Player* player) {
    if (mFile == nullptr || mRemaining <= 0 || player == nullptr || player->skelAnime.jointTable == nullptr ||
        player->skelAnime.limbCount <= 0) {
        return;
    }

    const char* animation = reinterpret_cast<const char*>(player->skelAnime.animation);
    const char* baseName = animation == nullptr ? "(null)" : strrchr(animation, '/');
    baseName = baseName == nullptr ? animation : baseName + 1;
    for (int limb = 1; limb <= player->skelAnime.limbCount; ++limb) {
        const Vec3s& joint = player->skelAnime.jointTable[limb];
        fprintf(mFile, "%d,%.3f,%.1f,%s,%d,%d,%d,%d\n", mCaptureIndex, player->skelAnime.curFrame,
                player->skelAnime.animLength, baseName, limb, joint.x, joint.y, joint.z);
    }
    ++mCaptureIndex;
    if (--mRemaining == 0) {
        stop();
    }
}

void Zelda3D::LinkJointDump::stop() {
    if (mFile != nullptr) {
        fclose(mFile);
        mFile = nullptr;
    }
    mRemaining = 0;
}
