#include "OTRGlobals.h"
#include "OTRAudio.h"
#include "host/app_identity.h"
#include "host/archive_extension_cache.h"
#include "host/archive_state.h"
#include "host/audio_lifecycle.h"
#include "host/camera_strings.h"
#include "host/config_drop.h"
#include "host/controller_buttons.h"
#include "host/item_randomizer_bridge.h"
#include "host/save_file.h"
#include "host/texture_cache_bridge.h"
#include "host/window_session.h"
#include "port/core_boot_error.h"
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <optional>
#include <imgui.h>

#include "ResourceManagerHelpers.h"
#include <fast/Fast3dWindow.h>
#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/window/Window.h>
#include <soh/GameVersions.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "Enhancements/gameconsole.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif
#include <ship/audio/AudioPlayer.h>
#include "Enhancements/speechsynthesizer/SpeechSynthesizer.h"
#include "Enhancements/audio/AudioCollection.h"
#include "Enhancements/debugconsole.h"
#include "Enhancements/randomizer/randomizer.h"
#include "Enhancements/randomizer/randomizer_generation_lifecycle.h"
#include "Enhancements/randomizer/randomizer_entrance_tracker.h"
#include "Enhancements/randomizer/randomizer_check_tracker.h"
#include "Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/settings.h"
#include "Enhancements/gameplaystats.h"
#include "soh/Enhancements/savestates.h"
#include "frame_interpolation.h"
#include "SohGui/SohMenu.h"
#include "SohGui/SohGui.hpp"
#include "variables.h"
#include "z64.h"
#include "macros.h"
#include <ship/window/gui/Fonts.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/gui/resource/Font.h>
#include <ship/utils/StringHelper.h>
#include "Enhancements/custom-message/CustomMessageManager.h"
#include "host/rom_auto_extraction.h"
#include "util.h"

#include <fast/Fast3dGui.h>
#include <fast/debug/GfxDebugger.h>

#if not defined(__SWITCH__) && not defined(__WIIU__)
#include "extractor/Extract.h"
#include "soh/Enhancements/Presets/Presets.h"
#endif

#include <fast/interpreter.h>

#include <SDL3/SDL_scancode.h>

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#elif defined(__WIIU__)
#include <port/wiiu/WiiUImpl.h>
#include <coreinit/debug.h> // OSFatal
#endif

#include "functions/ui.h"
#include "Enhancements/item-tables/ItemTableManager.h"
#include "Enhancements/Lang/Lang.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "ActorDB.h"
#include "SaveManager.h"
#include "soh/Network/CrowdControl/CrowdControl.h"
#include "soh/Network/Sail/Sail.h"
#include "soh/Network/Anchor/Anchor.h"
#include "Enhancements/game-interactor/GameInteractor.h"
#include "Enhancements/randomizer/draw.h"
#include <libultraship/libultraship.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <fast/resource/ResourceType.h>

// Resource Types/Factories
#include "soh/resource/type/Array.h"
#include <ship/resource/type/Blob.h>
#include <fast/resource/type/DisplayList.h>
#include <fast/resource/type/Matrix.h>
#include <fast/resource/type/Texture.h>
#include <fast/resource/type/Vertex.h>
#include "soh/resource/type/SohResourceType.h"
#include "soh/resource/type/Animation.h"
#include "soh/resource/type/AudioSample.h"
#include "soh/resource/type/AudioSequence.h"
#include "soh/resource/type/AudioSoundFont.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/resource/type/Cutscene.h"
#include "soh/resource/type/Path.h"
#include "soh/resource/type/PlayerAnimation.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/Skeleton.h"
#include "soh/resource/type/SkeletonLimb.h"
#include "soh/resource/type/Text.h"
#include <ship/resource/factory/BlobFactory.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include "soh/resource/importer/ArrayFactory.h"
#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/importer/AudioSampleFactory.h"
#include "soh/resource/importer/AudioSequenceFactory.h"
#include "soh/resource/importer/AudioSoundFontFactory.h"
#include "soh/resource/importer/CollisionHeaderFactory.h"
#include "soh/resource/importer/CutsceneFactory.h"
#include "soh/resource/importer/PathFactory.h"
#include "soh/resource/importer/PlayerAnimationFactory.h"
#include "soh/resource/importer/SceneFactory.h"
#include "soh/resource/importer/SkeletonFactory.h"
#include "soh/resource/importer/SkeletonLimbFactory.h"
#include "soh/resource/importer/TextFactory.h"
#include "soh/resource/importer/BackgroundFactory.h"

#include "soh/config/ConfigUpdaters.h"
#include "init/ShipInit.hpp"

// Check this game's definitions against the SHARED port ABI.
//
// port/zelda3d_port_api.h is included by the port-shell header inside `#ifndef __cplusplus`, so
// this C++ file -- where the functions are actually DEFINED -- would otherwise never see those
// declarations, and a definition could drift from them silently. Pulling it in here under
// extern "C" gives every definition below a prototype to be checked against, which is the point of
// having one declaration site at all.
extern "C" {
#include "port/zelda3d_port_api.h"
}

#ifdef __WIIU__
const uint32_t defaultImGuiScale = 3;
#else
const uint32_t defaultImGuiScale = 1;
#endif

const float imguiScaleOptionToValue[4] = { 0.75f, 1.0f, 1.5f, 2.0f };

OTRGlobals* OTRGlobals::Instance;
SaveManager* SaveManager::Instance;
CustomMessageManager* CustomMessageManager::Instance;
ItemTableManager* ItemTableManager::Instance;
GameInteractor* GameInteractor::Instance;
AudioCollection* AudioCollection::Instance;
SpeechSynthesizer* SpeechSynthesizer::Instance;
CrowdControl* CrowdControl::Instance;
Sail* Sail::Instance;
Anchor* Anchor::Instance;

extern "C" void PadMgr_ThreadEntry(PadMgr* padMgr);

Color_RGB8 kokiriColor = { 0x1E, 0x69, 0x1B };
Color_RGB8 goronColor = { 0x64, 0x14, 0x00 };
Color_RGB8 zoraColor = { 0x00, 0xEC, 0x64 };

int32_t previousImGuiScaleIndex;
float previousImGuiScale;

bool prevAltAssets = false;

// zelda3d: boot breadcrumbs. Each logs AND flushes immediately, so if a startup step hangs
// (e.g. an ImGui font-atlas build or a Vulkan wait on MoltenVK) the log shows the exact last
// step reached instead of stalling silently. Grep the run log for "[zelda3d boot]".
#define ZELDA3D_BOOT(...)                           \
    do {                                            \
        SPDLOG_INFO("[zelda3d boot] " __VA_ARGS__); \
        if (auto _lg = spdlog::default_logger())    \
            _lg->flush();                           \
    } while (0)

OTRGlobals::OTRGlobals() {
    context = Ship::Context::CreateUninitializedInstance("Ship of Harkinian", kSohAppShortName, "shipofharkinian.json");

    Zelda3D_InitializePortArchiveState();
    const bool sohArchiveVersionMatch = Zelda3D_PortArchiveVersionMatches();

    context->InitConfiguration();
    context->InitConsoleVariables();

    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>({
        BTN_CUSTOM_MODIFIER1,
        BTN_CUSTOM_MODIFIER2,
        BTN_CUSTOM_OCARINA_NOTE_D4,
        BTN_CUSTOM_OCARINA_NOTE_F4,
        BTN_CUSTOM_OCARINA_NOTE_A4,
        BTN_CUSTOM_OCARINA_NOTE_B4,
        BTN_CUSTOM_OCARINA_NOTE_D5,
        BTN_CUSTOM_OCARINA_DISABLE_SONGS,
        BTN_CUSTOM_OCARINA_PITCH_UP,
        BTN_CUSTOM_OCARINA_PITCH_DOWN,
    }));
    context->InitControlDeck(controlDeck);
    context->InitResourceManager({ Zelda3D_PortArchivePath() }, {}, 3, true);
    context->InitConsole();

    // ADOPT the engine's window if one is already up; construct one only when there is none.
    //
    // A core must not construct engine objects that already exist, and this is the call site that
    // proved why. Under the launcher running two games back to back, InitWindow correctly SKIPS
    // (the window is engine-lifetime and shared on purpose) -- so the Context never took ownership,
    // this local shared_ptr became the sole owner, and destroying it at end of scope ran
    // ~Fast3dWindow -> Fast::Interpreter::Destroy on the process-global renderer state. Measured:
    // SIGSEGV immediately after "OTRGlobals constructor complete".
    //
    // What is per-game here is not the window but its GuiWindow list, so the adopt path installs
    // THIS game's windows into the existing Gui (Context::BeginGameSession cleared the previous
    // game's). See docs/MM_NATIVE.md N3.
    auto fast3dWindow = std::dynamic_pointer_cast<Fast::Fast3dWindow>(context->GetWindow());
    if (fast3dWindow == nullptr) {
        // The Context takes ownership; we keep only a raw observer (see sohFast3dWindow's comment).
        fast3dWindow = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}));
        context->InitWindow(fast3dWindow);
    } else {
        SPDLOG_INFO("Adopting the engine's existing window rather than constructing a second one.");
    }
    Zelda3D_SetFast3dWindow(fast3dWindow.get());

    SohGui::SetupMenu();
    ZELDA3D_BOOT("ctor: SetupMenu done; sohArchiveVersionMatch={}", sohArchiveVersionMatch);

    if (sohArchiveVersionMatch) {

        auto overlay = context->GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay();
        ZELDA3D_BOOT("ctor: overlay->LoadFont PressStart2P");
        overlay->LoadFont("Press Start 2P", 12.0f, "fonts/PressStart2P-Regular.ttf");
        ZELDA3D_BOOT("ctor: overlay->LoadFont Fipps");
        overlay->LoadFont("Fipps", 32.0f, "fonts/Fipps-Regular.otf");
        overlay->SetCurrentFont(CVarGetString(CVAR_GAME_OVERLAY_FONT, "Press Start 2P"));

        ZELDA3D_BOOT("ctor: CreateFontWithSize Inconsolata x4");
        fontMonoSmall = CreateFontWithSize(14.0f, "fonts/Inconsolata-Regular.ttf");
        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        ZELDA3D_BOOT("ctor: CreateFontWithSize Montserrat x3");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
        ZELDA3D_BOOT("ctor: CreateFontWithSize NotoSansJP (full CJK atlas)");
        fontJapanese = CreateFontWithSize(24.0f, "fonts/NotoSansJP-Regular.ttf", true);
        ZELDA3D_BOOT("ctor: fonts loaded");
        ImGui::GetIO().FontDefault = fontStandardLarger;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = defaultImGuiScale;
    ZELDA3D_BOOT("ctor: ScaleImGui");
    ScaleImGui();
    ZELDA3D_BOOT("ctor: OTRGlobals constructor complete");
}

void InitGfxDebugger() {
    auto dbg =
        std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow())->GetGfxDebugger();

    if (dbg != nullptr) {
        return;
    }

    dbg = std::make_shared<Fast::GfxDebugger>();

    if (dbg != nullptr) {
        SPDLOG_ERROR("Failed to initialize gfx debugger");
    }
}

void OTRGlobals::Initialize() {
    std::string mqPath = Ship::Context::LocateFileAcrossAppDirs("oot-mq.o2r", kSohAppShortName);
    if (std::filesystem::exists(mqPath)) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(mqPath);
    }
    std::string ootPath = Ship::Context::LocateFileAcrossAppDirs("oot.o2r", kSohAppShortName);
    if (std::filesystem::exists(ootPath)) {
        context->GetResourceManager()->GetArchiveManager()->AddArchive(ootPath);
    }

    std::unordered_set<uint32_t> ValidHashes = {
        OOT_PAL_MQ,     OOT_NTSC_JP_MQ, OOT_NTSC_US_MQ, OOT_PAL_GC_MQ_DBG, OOT_NTSC_US_10,
        OOT_NTSC_US_11, OOT_NTSC_US_12, OOT_PAL_10,     OOT_PAL_11,        OOT_NTSC_JP_GC_CE,
        OOT_NTSC_JP_GC, OOT_NTSC_US_GC, OOT_PAL_GC,     OOT_PAL_GC_DBG1,   OOT_PAL_GC_DBG2,
    };

#if (_DEBUG)
    auto defaultLogLevel = spdlog::level::trace;
#else
    auto defaultLogLevel = spdlog::level::info;
#endif
    context->InitConfiguration();
    context->InitConsoleVariables();
    auto logLevel =
        static_cast<spdlog::level::level_enum>(CVarGetInteger(CVAR_DEVELOPER_TOOLS("LogLevel"), defaultLogLevel));
    context->InitLogging(logLevel, logLevel);
    Ship::Context::GetRawInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");

    InitGfxDebugger();
    context->InitFileDropMgr();

    // tell LUS to reserve 3 SoH specific threads (Game, Audio, Save)
    Zelda3D_InitializeAltAssets();

    context->InitCrashHandler();

    context->GetWindow()->SetAutoCaptureMouse(CVarGetInteger(CVAR_SETTING("EnableMouse"), 0) &&
                                              CVarGetInteger(CVAR_SETTING("AutoCaptureMouse"), 1));
    context->GetWindow()->SetForceCursorVisibility(CVarGetInteger(CVAR_SETTING("CursorVisibility"), 0));

    context->InitAudio({ .SampleRate = 32000,
                         .SampleLength = 1024,
                         // 4096 frames at 32 kHz (~128 ms) gives enough reservoir for frame
                         // jitter and slow-frame spikes without perceptible audio latency.
                         .DesiredBuffered = 4096 });

    // The menu is set up before audio is initialized, so its list of available audio backends has to be
    // populated here rather than in Menu::InitElement (where the window backends are handled).
    SohGui::GetSohMenu()->UpdateAudioBackendObjects();

    SPDLOG_INFO("Starting Ship of Harkinian version {} (Branch: {} | Commit: {})", (char*)gBuildVersion,
                (char*)gGitBranch, (char*)gGitCommitHash);

    auto loader = context->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "Array", static_cast<uint32_t>(SOH::ResourceType::SOH_Array), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAnimationV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SOH::ResourceType::SOH_Animation), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPlayerAnimationV0>(),
                                    RESOURCE_FORMAT_BINARY, "PlayerAnimation",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Room", static_cast<uint32_t>(SOH::ResourceType::SOH_Room),
                                    0); // Is room scene? maybe?
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSceneV0>(), RESOURCE_FORMAT_XML, "Room",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Room), 0); // Is room scene? maybe?
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCollisionHeaderV0>(),
                                    RESOURCE_FORMAT_BINARY, "CollisionHeader",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLCollisionHeaderV0>(), RESOURCE_FORMAT_XML,
                                    "CollisionHeader", static_cast<uint32_t>(SOH::ResourceType::SOH_CollisionHeader),
                                    0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonV0>(), RESOURCE_FORMAT_XML,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonLimbV0>(),
                                    RESOURCE_FORMAT_BINARY, "SkeletonLimb",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSkeletonLimbV0>(), RESOURCE_FORMAT_XML,
                                    "SkeletonLimb", static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryPathV0>(), RESOURCE_FORMAT_BINARY,
                                    "Path", static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLPathV0>(), RESOURCE_FORMAT_XML, "Path",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Path), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryCutsceneV0>(), RESOURCE_FORMAT_BINARY,
                                    "Cutscene", static_cast<uint32_t>(SOH::ResourceType::SOH_Cutscene), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryTextV0>(), RESOURCE_FORMAT_BINARY,
                                    "Text", static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLTextV0>(), RESOURCE_FORMAT_XML, "Text",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_Text), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSampleV2>(), RESOURCE_FORMAT_BINARY,
                                    "AudioSample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 2);

    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSampleV0>(), RESOURCE_FORMAT_XML,
                                    "Sample", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSoundFontV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSoundFont",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 2);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLSoundFontV0>(), RESOURCE_FORMAT_XML,
                                    "SoundFont", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAudioSequenceV2>(),
                                    RESOURCE_FORMAT_BINARY, "AudioSequence",
                                    static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 2);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryXMLAudioSequenceV0>(), RESOURCE_FORMAT_XML,
                                    "Sequence", static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryBackgroundV0>(), RESOURCE_FORMAT_BINARY,
                                    "Background", static_cast<uint32_t>(SOH::ResourceType::SOH_Background), 0);

    Lang::LoadLangs();

    gSaveStateMgr = std::make_shared<SaveStateMgr>();
    gRandoContext->InitStaticData();
    gRandoContext = Rando::Context::CreateInstance();
    Rando::Settings::GetInstance()->AssignContext(gRandoContext);
    Rando::StaticData::InitItemTable(); // RANDOTODO make this not rely on context's logic so it can be initialised in
                                        // InitStaticData
    gRandomizer = std::make_shared<Randomizer>();

    hasMasterQuest = hasOriginal = false;

    // Move the camera strings from read only memory onto the heap, because db_camera writes into
    // them (digits, cursor arrows). "a place that will only ever be run once at the beginning of
    // startup" is what this used to be; a core can run twice now, so the copy is remade per run --
    // which is also the fix for the second-order bug, that run 2 would otherwise inherit run 1's
    // overwritten text. Freed first: 74 strdups plus the array, the whole of OoT's per-run leak.
    Zelda3D_InitializeCameraStrings();

    auto versions = context->GetResourceManager()->GetArchiveManager()->GetGameVersions();

    for (uint32_t version : versions) {
        if (!ValidHashes.contains(version)) {
#if defined(__SWITCH__)
            SPDLOG_ERROR("Invalid OTR File!");
#elif defined(__WIIU__)
            Ship::WiiU::ThrowInvalidOTR();
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Invalid OTR File",
                                     "Attempted to load an invalid OTR file. Try regenerating.", nullptr);
            SPDLOG_ERROR("Invalid OTR File!");
#endif
            throw Zelda3D::CoreBootError("the game archive is not a valid OTR/O2R file; try regenerating it");
        }
        switch (version) {
            case OOT_PAL_MQ:
            case OOT_NTSC_JP_MQ:
            case OOT_NTSC_US_MQ:
            case OOT_PAL_GC_MQ_DBG:
                hasMasterQuest = true;
                break;
            case OOT_NTSC_US_10:
            case OOT_NTSC_US_11:
            case OOT_NTSC_US_12:
            case OOT_PAL_10:
            case OOT_PAL_11:
            case OOT_NTSC_JP_GC_CE:
            case OOT_NTSC_JP_GC:
            case OOT_NTSC_US_GC:
            case OOT_PAL_GC:
            case OOT_PAL_GC_DBG1:
            case OOT_PAL_GC_DBG2:
                hasOriginal = true;
                break;
            default:
                break;
        }
    }
}

OTRGlobals::~OTRGlobals() {
}

void OTRGlobals::ScaleImGui() {
    int32_t imGuiScaleIndex = CVarGetInteger(CVAR_SETTING("ImGuiScale"), defaultImGuiScale);
    if (imGuiScaleIndex == previousImGuiScaleIndex) {
        return;
    }

    float scale = imguiScaleOptionToValue[imGuiScaleIndex];
    float newScale = scale / previousImGuiScale;
    ImGui::GetStyle().ScaleAllSizes(newScale);
    ImGui::GetIO().FontGlobalScale = scale;
    previousImGuiScale = scale;
    previousImGuiScaleIndex = imGuiScaleIndex;
}

bool OTRGlobals::HasMasterQuest() {
    return hasMasterQuest;
}

bool OTRGlobals::HasOriginal() {
    return hasOriginal;
}

uint32_t OTRGlobals::GetInterpolationFPS() {
    if (CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0)) {
        return Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate();
    } else if (CVarGetInteger(CVAR_VSYNC_ENABLED, 1) ||
               !Ship::Context::GetRawInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetRawInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20));
    }
    return CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 20);
}
// Declared here rather than via zelda3d.h, which this TU does not include and which would pull in
// the whole N64 global header. Implemented in zelda3d/core/zelda3d_hostreg.cpp.
extern "C" void Zelda3D_RegisterHostHooks(void);

// C-callable, so Zelda3D_CoreRunBegin (which is C) can free it FIRST, before any of the other run
// resets look at the state it owns. See the call site for why the order matters.
// Delete the previous run's instance of a per-run singleton, immediately before the new one replaces
// it. Provably safe at that exact point -- the old value is dead by definition on the next line -- and
// safer than a central teardown, which would have to reason about what still reads each of them.
//
// These are `new`ed inside InitOTR, i.e. once per RUN, and upstream never deleted any of them: each
// run leaked a full copy of the message tables, the item tables, the actor DB, the audio collection,
// the save manager's registered sections and the game-interactor instance.
//
// CrowdControl / Sail / Anchor were held back from this list on the grounds that each owns a network
// thread and `Disable()` "is not documented to join". Reading it settles that: Network::Disable()
// ends with `receiveThread.join()`, and the receive thread calls OnDisconnected() on its way out,
// which is where CrowdControl joins its second thread. DeinitOTR now calls Disable() unconditionally,
// so by the time the next run reaches here both threads are finished and the delete is a delete.
template <typename T> static void ReplacePerRunSingleton(T*& instance, const char* what) {
    if (instance == nullptr) {
        return;
    }
    SPDLOG_INFO("Run start: freeing the previous run's {} (it used to leak, once per run).", what);
    delete instance;
    instance = nullptr;
}

extern "C" void Zelda3D_FreePreviousOTRGlobals(void) {
    if (OTRGlobals::Instance == nullptr) {
        // Said out loud: on run 1 there is nothing to free, and a silent no-op here would be
        // indistinguishable from this never being called at all.
        SPDLOG_INFO("Run start: no previous OTRGlobals to free (first run of this core).");
        return;
    }

    SPDLOG_INFO("Run start: freeing the previous run's OTRGlobals (it used to leak, with its "
                "save-state, randomizer and rando-context references inside it).");
    delete OTRGlobals::Instance;
    OTRGlobals::Instance = nullptr;
}

extern "C" int InitOTR(int argc, char* argv[]) {
    // Returns 0 on success and non-zero when the core cannot boot. See core_boot_error.h:
    // this is the outermost C++ frame on the boot path, and the caller is C, so the throw has
    // to be caught HERE -- an exception unwinding into a C frame is undefined.
    try {
        // Self-test for the boot-failure path (see port/core_boot_error.h). This path only runs when
        // an asset is missing or corrupt, which is exactly the case a gate cannot arrange without
        // moving the user's ROMs around -- so without this hook the return-instead-of-exit contract
        // would ship untested, which is how it silently reverts to exit() the next time someone
        // touches the boot chain. tools/zelda3d_sequence.sh --bootfail drives it.
        if (const char* e = std::getenv("ZELDA3D_BOOTFAIL_TEST"); e != nullptr && e[0] == '1') {
            throw Zelda3D::CoreBootError("ZELDA3D_BOOTFAIL_TEST=1 -- deliberate failure, exercising the "
                                         "return-to-launcher path");
        }
        // Before anything renders or reads input: give libultraship this core's hooks. It can no longer
        // call them by name -- see zelda3d/core/zelda3d_hostreg.cpp.
        Zelda3D_RegisterHostHooks();
        ZELDA3D_BOOT("InitOTR: new OTRGlobals()");
        OTRGlobals::Instance = new OTRGlobals();
        ZELDA3D_BOOT("InitOTR: auto-extract vanilla archive if missing");
        Zelda3D_AutoExtractVanillaArchive();

        ZELDA3D_BOOT("InitOTR: Initialize() (loads oot.o2r)");
        OTRGlobals::Instance->Initialize();
        ZELDA3D_BOOT("InitOTR: Initialize() done; managers/audio next");
        ReplacePerRunSingleton(CustomMessageManager::Instance, "CustomMessageManager");
        CustomMessageManager::Instance = new CustomMessageManager();
        ReplacePerRunSingleton(ItemTableManager::Instance, "ItemTableManager");
        ItemTableManager::Instance = new ItemTableManager();
        ReplacePerRunSingleton(GameInteractor::Instance, "GameInteractor");
        GameInteractor::Instance = new GameInteractor();
        ReplacePerRunSingleton(SaveManager::Instance, "SaveManager");
        SaveManager::Instance = new SaveManager();

        std::shared_ptr<Ship::Config> conf = OTRGlobals::Instance->context->GetConfig();
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion1Updater>());
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion2Updater>());
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion3Updater>());
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion4Updater>());
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion5Updater>());
        conf->RegisterVersionUpdater(std::make_shared<SOH::ConfigVersion6Updater>());
        conf->RunVersionUpdates();

        SohGui::SetupGuiElements();
        SohGui::SetupMenuElements();

        // Preset loading, given a real boot call site. It used to ride along inside a Dear ImGui menu
        // registration (Presets.cpp RegisterPresetsWidgets), which made a user-facing feature depend on
        // a menu that is never drawn.
        Presets_LoadAtBoot();

        ReplacePerRunSingleton(AudioCollection::Instance, "AudioCollection");
        AudioCollection::Instance = new AudioCollection();
        ReplacePerRunSingleton(ActorDB::Instance, "ActorDB");
        ActorDB::Instance = new ActorDB();
#ifdef __APPLE__
        ReplacePerRunSingleton(SpeechSynthesizer::Instance, "SpeechSynthesizer");
        SpeechSynthesizer::Instance = new DarwinSpeechSynthesizer();
#elif defined(_WIN32)
        ReplacePerRunSingleton(SpeechSynthesizer::Instance, "SpeechSynthesizer");
        SpeechSynthesizer::Instance = new SAPISpeechSynthesizer();
#elif ESPEAK
        ReplacePerRunSingleton(SpeechSynthesizer::Instance, "SpeechSynthesizer");
        SpeechSynthesizer::Instance = new ESpeakSpeechSynthesizer();
#else
        ReplacePerRunSingleton(SpeechSynthesizer::Instance, "SpeechSynthesizer");
        SpeechSynthesizer::Instance = new SpeechLogger();
#endif
        SpeechSynthesizer::Instance->Init();

        ReplacePerRunSingleton(CrowdControl::Instance, "CrowdControl");
        CrowdControl::Instance = new CrowdControl();
        ReplacePerRunSingleton(Sail::Instance, "Sail");
        Sail::Instance = new Sail();
        ReplacePerRunSingleton(Anchor::Instance, "Anchor");
        Anchor::Instance = new Anchor();

        OTRMessage_Init();
        OTRAudio_Init();
        OTRExtScanner();
        VanillaItemTable_Init();
        DebugConsole_Init();

        ActorDB::AddBuiltInCustomActors();
        // #region SOH [Randomizer] TODO: Remove these and refactor spoiler file handling for randomizer
        CVarClear(CVAR_GENERAL("RandomizerNewFileDropped"));
        CVarClear(CVAR_GENERAL("RandomizerDroppedFile"));
        // #endregion

        Ship::Context::GetRawInstance()->GetFileDropMgr()->RegisterDropHandler(SoH_HandleConfigDrop);

        RegisterImGuiItemIcons();

        time_t now = time(NULL);
        tm* tm_now = localtime(&now);
        if (tm_now->tm_mon == 11 && tm_now->tm_mday >= 24 && tm_now->tm_mday <= 25) {
            CVarRegisterInteger(CVAR_GENERAL("LetItSnow"), 1);
        } else {
            CVarClear(CVAR_GENERAL("LetItSnow"));
        }

        // Zelda3D display/range defaults: applied ONCE (persisted), so the user can still change them
        // in-menu afterward. Match the monitor refresh rate (smooth high-FPS interpolation), extend
        // actor draw distance well past the N64 cull, and stop culling actors at the widescreen
        // edges. Aspect ratio already follows the window when unset, so it needs no override. Gated
        // on SOH3D mode (set by run.sh) so a plain SoH build is untouched.
        if (getenv("SOH3D") != nullptr && CVarGetInteger(CVAR_GENERAL("Zelda3DDefaults"), 0) < 1) {
            CVarSetInteger(CVAR_SETTING("MatchRefreshRate"), 1);           // FPS follows monitor refresh
            CVarSetInteger(CVAR_ENHANCEMENT("DisableDrawDistance"), 20);   // 20x actor forward draw/cull range
            CVarSetInteger(CVAR_ENHANCEMENT("WidescreenActorCulling"), 1); // don't cull at widescreen X edges
            CVarSetInteger(CVAR_GENERAL("Zelda3DDefaults"), 1);
            CVarSave();
        }

        srand(static_cast<unsigned int>(now));
        SDLNet_Init();
        if (CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 0)) {
            CrowdControl::Instance->Enable();
        }
        if (CVarGetInteger(CVAR_REMOTE_SAIL("Enabled"), 0)) {
            Sail::Instance->Enable();
        }
        if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
            Anchor::Instance->Enable();
        }
        ShipInit::InitAll();
        Rando::StaticData::InitHashMaps();
        OTRGlobals::Instance->gRandoContext->AddExcludedOptions();
    } catch (const Zelda3D::CoreBootError& e) {
        SPDLOG_ERROR("[soh] cannot boot: {}", e.what());
        if (auto lg = spdlog::default_logger()) {
            lg->flush();
        }
        return 1;
    }

    return 0;
}
// Application shutdown. Runs ONCE on the main thread after the game loop (Main) returns because the
// window was closed. Three clear steps: STOP threads, PERSIST what must survive, then EXIT.
//
// We deliberately do NOT run the engine's GUI/renderer/window destructors here, nor let them run at
// exit() via the static `unique_ptr<Context>`. On a dying process that teardown is pure liability:
//   - The driver's Vulkan swapchain/WSI destroy crashes on multiple machines (RADV/Wayland
//     `wsi_wl_swapchain_destroy` double-free; lavapipe/X11 `xcb_present` buffer-overflow), and
//     RmlUi's static StyleSheetFactory double-frees — all inside library/driver code we don't own.
//   - The OS reclaims the GPU, the window and the heap on exit regardless. Object-graph teardown
//     only matters for swapchain RECREATE (resize), never for shutdown.
// So we persist the only two side-effecting things ~Context would (window layout + config) and exit.
extern "C" void DeinitOTR() {
    // 1. Stop every background thread, so nothing touches the engine, files or network as we exit.
    OTRAudio_Exit();              // stop + join the audio thread (idempotent; main.c stops it first)
    SaveManager_ThreadPoolWait(); // let any in-flight save finish writing to disk
    // Seed generation runs on its own thread and nothing ever joined it -- the helper for that was
    // written, declared, and never called. Joined here, BEFORE anything frees the Rando::Context that
    // an in-flight generation is writing into.
    JoinRandoGenerationThread();
    // Unconditionally, not behind the CVar that was read at Enable() time. Network::Disable() already
    // early-returns when it is not enabled, so the CVar test bought nothing and cost the case that
    // matters: a remote switched on, then its CVar switched back off, left the receive thread running
    // into a game that had ended. Disable() joins that thread (and CrowdControl's second thread, via
    // OnDisconnected on the way out), which is what makes deleting these three at the next run safe.
    CrowdControl::Instance->Disable();
    Sail::Instance->Disable();
    Anchor::Instance->Disable();
    SDLNet_Quit();
    Zelda3D_FreeCameraStrings();

    // 2. Persist the only state ~Context would have written: window layout and the config file.
    if (Ship::Context* ctx = OTRGlobals::Instance->context) {
        if (auto window = ctx->GetWindow()) {
            window->SaveWindowToConfig();
        }
        if (auto config = ctx->GetConfig()) {
            config->Save();
        }
    }

    // 3a. EXPERIMENT (claim C057's falsifier): run the teardown this function exists to avoid, and
    // see whether it still crashes. Every crash cited above is from the Vulkan era; SDL3 GPU is now
    // the only backend, so "it crashes" is an inherited belief rather than a current measurement.
    // ~Context destroys audio/window/console/controldeck/resources in a defined order and saves the
    // config itself, so this is the engine's own teardown, not a hand-rolled one.
    //
    // Surviving this is what in-process game switching needs. If it does survive, the _exit(0)
    // below and this branch both go away; if it crashes, the answer is to stop tearing down at all
    // (launcher owns window+renderer, cores own archives+heaps). Only reachable on explicit request.
    if (Ship::Context::IsFullTeardownRequested()) {
        SPDLOG_INFO("DeinitOTR: FULL TEARDOWN requested -- running the destructors _exit(0) skips");
        if (auto logger = spdlog::default_logger()) {
            logger->flush();
        }
        Ship::Context::DestroyInstance();
        // Reached only if the destructors did not crash. OTRGlobals::Instance->context now dangles,
        // which is acceptable because the caller is unwinding out of the game for good.
        fprintf(stderr, "ZELDA3D TEARDOWN: Context destroyed WITHOUT crashing; returning to caller\n");
        fflush(stderr);
        return;
    }

    // 3. Return to the caller. `_exit(0)` used to live here to skip GUI/renderer/window destructors
    // that crash inside driver code -- and it still would, if this function ended the program. It
    // does not: the only program is the zelda3d launcher, which dlopen'd this core and is waiting
    // for run() to return so it can load whichever game the user picked next. Exiting here would
    // kill the host, not the game.
    //
    // Returning is not a weaker shutdown. Steps 1 and 2 above already stopped every background
    // thread and persisted window layout and config, and the caller still has Heaps_Free() to run.
    // What is deliberately NOT done is Context::DestroyInstance() -- that is the teardown measured
    // as crashing (see Context::RequestExitWithFullTeardown and docs/MM_NATIVE.md), and it belongs
    // to the launcher, once, at process exit. The engine stays up between games by design.
    if (auto logger = spdlog::default_logger()) {
        logger->flush();
    }
}
