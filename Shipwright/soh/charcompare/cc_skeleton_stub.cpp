// Trimmed SOH::Skeleton resource impl for charcompare.
//
// The real soh/soh/resource/type/Skeleton.cpp drags in the whole game (z64.h,
// OTRGlobals, gPlayState, object headers) via SkeletonPatcher. charcompare only needs
// the resource payload accessors; the patcher is game-only and unreferenced here, so we
// provide just GetPointer()/GetPointerSize() (mirrors the real defs) and omit the rest.
#include "soh/resource/type/Skeleton.h"

namespace SOH {

SkeletonData* Skeleton::GetPointer() {
    return &skeletonData;
}

size_t Skeleton::GetPointerSize() {
    switch (type) {
        case SkeletonType::Normal:
            return sizeof(skeletonData.skeletonHeader);
        case SkeletonType::Flex:
            return sizeof(skeletonData.flexSkeletonHeader);
        case SkeletonType::Curve:
            return sizeof(skeletonData.skelCurveLimbList);
        default:
            return 0;
    }
}

} // namespace SOH
