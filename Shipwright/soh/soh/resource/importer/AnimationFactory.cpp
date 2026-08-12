#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/type/Animation.h"
#include <ship/resource/ResourceManager.h>
#include "spdlog/spdlog.h"
#include "soh/resource/type/PlayerAnimation.h"
#include <ship/Context.h>

namespace SOH {
std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAnimationV0::ReadResource(std::shared_ptr<Ship::File> file,
                                               std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto animation = std::make_shared<Animation>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    AnimationType animType = (AnimationType)reader->ReadUInt32();
    animation->type = animType;

    if (animType == AnimationType::Normal) {
        // Set frame count
        animation->animationData.animationHeader.common.frameCount = reader->ReadInt16();

        // Populate frame data
        uint32_t rotValuesCnt = reader->ReadUInt32();
        animation->rotationValues.reserve(rotValuesCnt);
        for (uint32_t i = 0; i < rotValuesCnt; i++) {
            animation->rotationValues.push_back(reader->ReadUInt16());
        }
        animation->animationData.animationHeader.frameData = (int16_t*)animation->rotationValues.data();

        // Populate joint indices
        uint32_t rotIndCnt = reader->ReadUInt32();
        animation->rotationIndices.reserve(rotIndCnt);
        for (size_t i = 0; i < rotIndCnt; i++) {
            uint16_t x = reader->ReadUInt16();
            uint16_t y = reader->ReadUInt16();
            uint16_t z = reader->ReadUInt16();
            animation->rotationIndices.push_back(RotationIndex(x, y, z));
        }
        animation->animationData.animationHeader.jointIndices = (JointIndex*)animation->rotationIndices.data();

        // Set static index max
        animation->animationData.animationHeader.staticIndexMax = reader->ReadInt16();
    } else if (animType == AnimationType::Curve) {
        // Read frame count (unused in this animation type)
        reader->ReadInt16();

        // Set refIndex
        uint32_t refArrCnt = reader->ReadUInt32();
        animation->refIndexArr.reserve(refArrCnt);
        for (uint32_t i = 0; i < refArrCnt; i++) {
            animation->refIndexArr.push_back(reader->ReadUByte());
        }
        animation->animationData.transformUpdateIndex.refIndex = animation->refIndexArr.data();

        // Populate transform data
        uint32_t transformDataCnt = reader->ReadUInt32();
        animation->transformDataArr.reserve(transformDataCnt);
        for (uint32_t i = 0; i < transformDataCnt; i++) {
            TransformData data;
            data.unk_00 = reader->ReadUInt16();
            data.unk_02 = reader->ReadInt16();
            data.unk_04 = reader->ReadInt16();
            data.unk_06 = reader->ReadInt16();
            data.unk_08 = reader->ReadFloat();

            animation->transformDataArr.push_back(data);
        }
        animation->animationData.transformUpdateIndex.transformData = animation->transformDataArr.data();

        // Populate copy values
        uint32_t copyValuesCnt = reader->ReadUInt32();
        animation->copyValuesArr.reserve(copyValuesCnt);
        for (uint32_t i = 0; i < copyValuesCnt; i++) {
            animation->copyValuesArr.push_back(reader->ReadInt16());
        }
        animation->animationData.transformUpdateIndex.copyValues = animation->copyValuesArr.data();
    } else if (animType == AnimationType::Link) {
        // Initialize segment to nullptr (important for alt asset fallback)
        animation->animationData.linkAnimationHeader.segment = nullptr;

        // Read the frame count
        animation->animationData.linkAnimationHeader.common.frameCount = reader->ReadInt16();

        // Read the segment pointer (always 32 bit, doesn't adjust for system pointer size)
        std::string path = reader->ReadString();
        // CHECKED cast. This was `static_pointer_cast<Animation>`, which asserts a type rather than
        // testing one: if that path resolves to any other resource (PlayerAnimation is the obvious
        // candidate -- a different class, in the same subsystem, for the same kind of data), the
        // object is silently reinterpreted with Animation's layout and `GetPointer()` hands back an
        // address computed from the wrong offsets. Nothing downstream can tell.
        //
        // Wanted while chasing issue 0018 (ASAN: heap-use-after-free reading animation frame data
        // out of a freed resource FILE buffer). It does not yet explain that read -- `GetPointer()`
        // returns `&animationData`, inside the resource, which the strong resource cache keeps alive
        // -- so this is not being claimed as the fix. It closes the one way this line could produce a
        // pointer nobody could account for, and if the type IS wrong the next run says so by name
        // instead of computing a bad address in silence.
        auto animResource = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(path.c_str());
        auto animData = std::dynamic_pointer_cast<Animation>(animResource);

        // A Link animation's frame data is a PlayerAnimation resource, not an Animation one --
        // `misc/link_animetion/gPlayerAnimData_*`, the same resources ResourceMgr_LoadPlayerAnimByName
        // loads. This line used to `static_pointer_cast<Animation>` the result, which asserts a type
        // rather than testing one: the PlayerAnimation object was reinterpreted with Animation's
        // layout, and `GetPointer()` returned `&animationData` computed from the wrong offsets --
        // an address the object does not own. Nothing downstream could tell, and
        // AnimationContext_SetLoadFrame then memcpy'd frame data out of it every time the animation
        // played. Measured on one ordinary OoT run: THREE animations per run.
        //
        // Handled rather than merely refused, because refusing would leave those three unanimated.
        std::shared_ptr<PlayerAnimation> playerAnimData;
        if (animData == nullptr) {
            playerAnimData = std::dynamic_pointer_cast<PlayerAnimation>(animResource);
        }
        if (animResource != nullptr && animData == nullptr && playerAnimData == nullptr) {
            SPDLOG_ERROR("Link animation data at \"{}\" is neither an Animation nor a PlayerAnimation"
                         " resource -- refusing to reinterpret it, so its segment stays null.",
                         path);
        }

        // If direct load failed and alt assets are enabled, try with alt/ prefix
        bool triedAltPath = false;
        if (animData == nullptr && Ship::Context::GetRawInstance()->GetResourceManager()->IsAltAssetsEnabled()) {
            triedAltPath = true;
            std::string altPath = path;
            if (altPath.find("__OTR__") == 0) {
                altPath = altPath.substr(7); // Strip __OTR__
            }
            altPath = "alt/" + altPath;
            animData = std::dynamic_pointer_cast<Animation>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResourceProcess(altPath.c_str()));
        }

        // WHICH BRANCH AND WHICH TYPE, for every Link animation, gated on ZELDA3D_ANIMTYPE_LOG=1.
        //
        // Written this way because the question is comparative: `oot` alone resolves these cleanly
        // while `oot` as the second core of `mm,oot,mm` does not (issue 0018), and no report that
        // only fires on the bad case can establish what differs. So it prints on EVERY resolution,
        // names the branch taken and the resource Type actually returned, and says explicitly when a
        // load returned nothing -- a silent line here would be indistinguishable from "never asked".
        {
            static const bool kLog = getenv("ZELDA3D_ANIMTYPE_LOG") != nullptr;
            if (kLog) {
                const char* branch = animData != nullptr             ? "Animation"
                                     : playerAnimData != nullptr     ? "PlayerAnimation"
                                                                     : "NONE";
                const auto typeOf = [](const std::shared_ptr<Ship::IResource>& r) -> long {
                    return (r != nullptr && r->GetInitData() != nullptr) ? (long)r->GetInitData()->Type : -1;
                };
                // frameCount vs the size of the buffer that has to satisfy it. ASAN caught the
                // consumer reading frame 28 out of a 24-frame buffer
                // (`memcpy(ram, animData + (sizeof(Vec3s)*limbCount + 2) * frame, ...)`), so the
                // header and the data it points at disagree -- and neither value is visible at the
                // crash site. Both are printed for EVERY animation, not just suspicious ones,
                // because the useful comparison is between the clean run and the failing one.
                // limbCount is not known here, so bytes-per-frame cannot be derived; the raw pair is
                // what makes a mismatch legible.
                const size_t dataBytes = (playerAnimData != nullptr) ? playerAnimData->limbRotData.size() * 2 : 0;
                SPDLOG_INFO("ANIMTYPE path=\"{}\" firstLoad={} (type={}) altTried={} altResolved={} -> using {}"
                            " frameCount={} dataBytes={}",
                            path, animResource != nullptr ? "hit" : "MISS", typeOf(animResource),
                            triedAltPath ? "yes" : "no", animData != nullptr ? "yes" : "no", branch,
                            (int)animation->animationData.linkAnimationHeader.common.frameCount, dataBytes);
            }
        }

        if (animData != nullptr) {
            animation->animationData.linkAnimationHeader.segment = animData->GetPointer();
        } else if (playerAnimData != nullptr && !playerAnimData->limbRotData.empty()) {
            // The resource is cached with a STRONG shared_ptr for as long as the ResourceManager
            // lives, so a pointer into its vector is valid for the whole game session -- the same
            // guarantee ResourceMgr_LoadPlayerAnimByName already relies on.
            animation->animationData.linkAnimationHeader.segment = playerAnimData->limbRotData.data();
        } else {
            SPDLOG_WARN("Animation data segment not found: {}", path);
        }
    } else if (animType == AnimationType::Legacy) {
        SPDLOG_DEBUG("BEYTAH ANIMATION?!");
    }

    return animation;
}
} // namespace SOH