#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <memory>
#include <string>

class Randomizer;
class SaveStateMgr;

namespace Rando {
class Context;
}

namespace Ship {
class Context;
}

struct ImFont;

class OTRGlobals {
  public:
    static OTRGlobals* Instance;

    Ship::Context* context;
    std::shared_ptr<SaveStateMgr> gSaveStateMgr;
    std::shared_ptr<Randomizer> gRandomizer;
    std::shared_ptr<Rando::Context> gRandoContext;

    ImFont* fontMonoSmall = nullptr;
    ImFont* fontStandard = nullptr;
    ImFont* fontStandardLarger = nullptr;
    ImFont* fontStandardLargest = nullptr;
    ImFont* fontMono = nullptr;
    ImFont* fontMonoLarger = nullptr;
    ImFont* fontMonoLargest = nullptr;
    ImFont* fontJapanese = nullptr;

    OTRGlobals();
    ~OTRGlobals();

    void ScaleImGui();
    void Initialize();
    bool HasMasterQuest();
    bool HasOriginal();
    uint32_t GetInterpolationFPS();

  private:
    bool hasMasterQuest;
    bool hasOriginal;
    ImFont* CreateFontWithSize(float size, std::string fontPath, bool isJapaneseFont = false);
};
#endif
