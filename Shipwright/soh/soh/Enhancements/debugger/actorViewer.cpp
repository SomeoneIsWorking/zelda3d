#include "actorViewer.h"
#include "actorViewerParams.h"
#include "../../util.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
#include "init/ShipInit.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <unordered_map>
#include <string>
#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <spdlog/fmt/fmt.h>

#include "soh/cvar_prefixes.h"
#include "soh/ObjectExtension/ActorListIndex.h"

extern "C" {
#include <z64.h>
#include "z64math.h"
#include "variables.h"
#include "functions/actors.h"
#include "functions/audio.h"
#include "functions/math.h"
#include "macros.h"
extern PlayState* gPlayState;

#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"
}

#define DEBUG_ACTOR_NAMETAG_TAG "debug_actor_viewer"

#define CVAR_ACTOR_NAME_TAGS(val) CVAR_DEVELOPER_TOOLS("ActorViewer.NameTags." val)
#define CVAR_ACTOR_NAME_TAGS_ENABLED_NAME CVAR_ACTOR_NAME_TAGS("Enabled")
#define CVAR_ACTOR_NAME_TAGS_ENABLED CVarGetInteger(CVAR_ACTOR_NAME_TAGS("Enabled"), 0)

typedef struct {
    u16 id;
    u16 params;
    Vec3f pos;
    Vec3s rot;
} ActorInfo;

std::array<const char*, 12> acMapping = {
    "Switch",      "Background (Prop type 1)",
    "Player",      "Bomb",
    "NPC",         "Enemy",
    "Prop type 2", "Item/Action",
    "Misc.",       "Boss",
    "Door",        "Chest",
};

using namespace UIWidgets;

typedef enum {
    ACTORVIEWER_NAMETAGS_NONE,
    ACTORVIEWER_NAMETAGS_DESC,
    ACTORVIEWER_NAMETAGS_NAME,
    ACTORVIEWER_NAMETAGS_BOTH,
} ActorViewerNameTagsType;

const std::string GetActorDescription(u16 id) {
    return ActorDB::Instance->RetrieveEntry(id).entry.valid ? ActorDB::Instance->RetrieveEntry(id).entry.desc : "???";
}

const std::string GetActorDebugName(u16 id) {
    return ActorDB::Instance->RetrieveEntry(id).entry.valid ? ActorDB::Instance->RetrieveEntry(id).entry.name : "???";
}

template <typename T> void DrawGroupWithBorder(T&& drawFunc, std::string section) {
    // First group encapsulates the inner portion and border
    ImGui::BeginChild(std::string("##" + section).c_str(), ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX |
                          ImGuiChildFlags_AutoResizeY);

    // Second group encapsulates just the inner portion
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    drawFunc();
    ImGui::EndGroup();

    ImGui::EndChild();
}

void PopulateActorDropdown(int i, std::vector<Actor*>& data) {
    if (data.size() != 0) {
        data.clear();
    }
    if (gPlayState != nullptr) {
        ActorListEntry currList = gPlayState->actorCtx.actorLists[i];
        Actor* currAct = currList.head;
        if (currAct != nullptr) {
            while (currAct != nullptr) {
                data.push_back(currAct);
                currAct = currAct->next;
            }
        }
    }
}

std::vector<u16> GetActorsWithDescriptionContainingString(std::string s) {
    std::locale loc;
    for (size_t i = 0; i < s.length(); i += 1) {
        s[i] = std::tolower(s[i], loc);
    }

    std::vector<u16> actors;
    for (int i = 0; i < ActorDB::Instance->GetEntryCount(); i += 1) {
        ActorDB::Entry actorEntry = ActorDB::Instance->RetrieveEntry(i);
        std::string desc = actorEntry.desc;
        for (size_t j = 0; j < desc.length(); j += 1) {
            desc[j] = std::tolower(desc[j], loc);
        }
        if (desc.find(s) != std::string::npos) {
            actors.push_back((u16)i);
        }
    }
    return actors;
}

void ActorViewer_AddTagForActor(Actor* actor) {
    if (!CVarGetInteger(CVAR_ACTOR_NAME_TAGS("Enabled"), 0)) {
        return;
    }

    std::vector<std::string> parts;

    if (CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayID"), 0)) {
        parts.push_back(GetActorDebugName(actor->id));
    }
    if (CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayDescription"), 0)) {
        parts.push_back(GetActorDescription(actor->id));
    }
    if (CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayCategory"), 0)) {
        parts.push_back(acMapping[actor->category]);
    }
    if (CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayParams"), 0)) {
        parts.push_back(fmt::format("0x{:04X} ({})", (u16)actor->params, actor->params));
    }

    std::string tag = "";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i != 0) {
            tag += "\n";
        }
        tag += parts.at(i);
    }

    bool withZBuffer = CVarGetInteger(CVAR_ACTOR_NAME_TAGS("WithZBuffer"), 0);

    NameTag_RegisterForActorWithOptions(actor, tag.c_str(),
                                        { .tag = DEBUG_ACTOR_NAMETAG_TAG, .noZBuffer = !withZBuffer });
}

void ActorViewer_AddTagForAllActors() {
    if (gPlayState == nullptr) {
        return;
    }

    for (size_t i = 0; i < ARRAY_COUNT(gPlayState->actorCtx.actorLists); i++) {
        ActorListEntry currList = gPlayState->actorCtx.actorLists[i];
        Actor* currAct = currList.head;
        while (currAct != nullptr) {
            ActorViewer_AddTagForActor(currAct);
            currAct = currAct->next;
        }
    }
}

void ActorViewerWindow::DrawElement() {
    ImGui::BeginDisabled(CVarGetInteger(CVAR_SETTING("DisableChanges"), 0));
    static ActorInfo newActor = { 0, 0, { 0, 0, 0 }, { 0, 0, 0 } };
    static ImU16 one = 1;
    static std::string filler = "Please select";
    static std::string searchString = "";
    static s16 currentSelectedInDropdown = -1;
    static std::vector<u16> actorSearchResults;

    if (gPlayState != nullptr) {
        if (ImGui::BeginChild("options", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY)) {
            bool toggled = false;
            bool optionChange = false;

            ImGui::SeparatorText("Options");

            toggled = UIWidgets::CVarCheckbox("Actor Name Tags", CVAR_ACTOR_NAME_TAGS("Enabled"),
                                              { { .tooltip = "Adds \"name tags\" above actors for identification" } });

            ImGui::SameLine();

            UIWidgets::Button("Display Items", { { .tooltip = "Click to add display items on the name tags" } });

            if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft | ImGuiPopupFlags_NoReopen)) {
                optionChange |= UIWidgets::CVarCheckbox("ID", CVAR_ACTOR_NAME_TAGS("DisplayID"));
                optionChange |= UIWidgets::CVarCheckbox("Description", CVAR_ACTOR_NAME_TAGS("DisplayDescription"));
                optionChange |= UIWidgets::CVarCheckbox("Category", CVAR_ACTOR_NAME_TAGS("DisplayCategory"));
                optionChange |= UIWidgets::CVarCheckbox("Params", CVAR_ACTOR_NAME_TAGS("DisplayParams"));

                ImGui::EndPopup();
            }

            optionChange |= UIWidgets::CVarCheckbox(
                "Name tags with Z-Buffer", CVAR_ACTOR_NAME_TAGS("WithZBuffer"),
                { { .tooltip = "Allow name tags to be obstructed when behind geometry and actors" } });

            if (toggled || optionChange) {
                bool tagsEnabled = CVarGetInteger(CVAR_ACTOR_NAME_TAGS("Enabled"), 0);
                bool noOptionsEnabled = !CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayID"), 0) &&
                                        !CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayDescription"), 0) &&
                                        !CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayCategory"), 0) &&
                                        !CVarGetInteger(CVAR_ACTOR_NAME_TAGS("DisplayParams"), 0);

                // Save the user an extra click and prevent adding "empty" tags by enabling,
                // disabling, or setting an option based on what changed
                if (tagsEnabled && noOptionsEnabled) {
                    if (toggled) {
                        CVarSetInteger(CVAR_ACTOR_NAME_TAGS("DisplayID"), 1);
                    } else {
                        CVarSetInteger(CVAR_ACTOR_NAME_TAGS("Enabled"), 0);
                    }
                } else if (optionChange && !tagsEnabled && !noOptionsEnabled) {
                    CVarSetInteger(CVAR_ACTOR_NAME_TAGS("Enabled"), 1);
                }

                NameTag_RemoveAllByTag(DEBUG_ACTOR_NAMETAG_TAG);
                ActorViewer_AddTagForAllActors();
            }
        }
        ImGui::EndChild();

        PushStyleCombobox(THEME_COLOR);
        if (ImGui::BeginCombo("Actor Type", acMapping[category])) {
            for (size_t i = 0; i < acMapping.size(); i++) {
                if (ImGui::Selectable(acMapping[i])) {
                    category = static_cast<int>(i);
                    PopulateActorDropdown(category, list);
                    break;
                }
            }
            ImGui::EndCombo();
        }

        if (display == nullptr) {
            filler = "Please select";
        }

        if (ImGui::BeginCombo("Actor", filler.c_str())) {
            for (size_t i = 0; i < list.size(); i++) {
                std::string label = std::to_string(i) + ": " + ActorDB::Instance->RetrieveEntry(list[i]->id).name;
                std::string description = GetActorDescription(list[i]->id);
                if (description != "")
                    label += " (" + description + ")";

                if (ImGui::Selectable(label.c_str(), list[i] == display)) {
                    display = list[i];
                    filler = label;
                    break;
                }
            }
            ImGui::EndCombo();
        }
        PopStyleCombobox();

        PushStyleHeader(THEME_COLOR);
        if (ImGui::TreeNode("Selected Actor")) {
            if (display != nullptr) {
                DrawGroupWithBorder(
                    [&]() {
                        ImGui::Text("Name: %s", ActorDB::Instance->RetrieveEntry(display->id).name.c_str());
                        ImGui::Text("Description: %s", GetActorDescription(display->id).c_str());
                        ImGui::Text("Category: %s", acMapping[display->category]);
                        ImGui::Text("ID: %d", display->id);
                        ImGui::Text("Parameters: %d", display->params);
                        ImGui::Text("Actor List Index: %d", GetActorListIndex(display));
                    },
                    "Selected Actor");
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetFontSize() * 6);

                DrawGroupWithBorder(
                    [&]() {
                        ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                        PushStyleInput(THEME_COLOR);
                        ImGui::Text("Actor Position");
                        ImGui::InputScalar("X##CurPos", ImGuiDataType_Float, &display->world.pos.x);
                        ImGui::InputScalar("Y##CurPos", ImGuiDataType_Float, &display->world.pos.y);
                        ImGui::InputScalar("Z##CurPos", ImGuiDataType_Float, &display->world.pos.z);
                        ImGui::PopItemWidth();
                        PopStyleInput();
                    },
                    "Actor Position");
                ImGui::SameLine();
                DrawGroupWithBorder(
                    [&]() {
                        PushStyleInput(THEME_COLOR);
                        ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                        ImGui::Text("Actor Rotation");
                        ImGui::InputScalar("X##CurRot", ImGuiDataType_S16, &display->world.rot.x);
                        ImGui::InputScalar("Y##CurRot", ImGuiDataType_S16, &display->world.rot.y);
                        ImGui::InputScalar("Z##CurRot", ImGuiDataType_S16, &display->world.rot.z);
                        ImGui::PopItemWidth();
                        PopStyleInput();
                    },
                    "Actor Rotation");

                if (display->category == ACTORCAT_BOSS || display->category == ACTORCAT_ENEMY) {
                    PushStyleInput(THEME_COLOR);
                    ImGui::InputScalar("Enemy Health", ImGuiDataType_U8, &display->colChkInfo.health);
                    PopStyleInput();
                    UIWidgets::InsertHelpHoverText("Some actors might not use this!");
                }

                DrawGroupWithBorder(
                    [&]() {
                        ImGui::Text("flags");
                        UIWidgets::DrawFlagArray32("flags", display->flags);
                    },
                    "flags");

                ImGui::SameLine();

                DrawGroupWithBorder(
                    [&]() {
                        ImGui::Text("bgCheckFlags");
                        UIWidgets::DrawFlagArray16("bgCheckFlags", display->bgCheckFlags);
                    },
                    "bgCheckFlags");

                if (Button("Go to Actor", ButtonOptions().Color(THEME_COLOR))) {
                    Player* player = GET_PLAYER(gPlayState);
                    Math_Vec3f_Copy(&player->actor.world.pos, &display->world.pos);
                    Math_Vec3f_Copy(&player->actor.home.pos, &player->actor.world.pos);
                }
            } else {
                ImGui::Text("Select an actor to display information.");
            }

            if (Button("Fetch from Target",
                       ButtonOptions()
                           .Color(THEME_COLOR)
                           .Tooltip("Grabs actor with target arrow above it. You might need C-Up for enemies"))) {
                Player* player = GET_PLAYER(gPlayState);
                if (player->talkActor != NULL) {
                    display = player->talkActor;
                    category = display->category;
                    PopulateActorDropdown(category, list);
                }
            }
            if (Button("Fetch from Held",
                       ButtonOptions().Color(THEME_COLOR).Tooltip("Grabs actor that Link is holding"))) {
                Player* player = GET_PLAYER(gPlayState);
                if (player->heldActor != NULL) {
                    display = player->heldActor;
                    category = display->category;
                    PopulateActorDropdown(category, list);
                }
            }
            if (Button("Fetch from Interaction",
                       ButtonOptions().Color(THEME_COLOR).Tooltip("Grabs actor from \"interaction range\""))) {
                Player* player = GET_PLAYER(gPlayState);
                if (player->interactRangeActor != NULL) {
                    display = player->interactRangeActor;
                    category = display->category;
                    PopulateActorDropdown(category, list);
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("New...")) {
            // ImGui::PushItemWidth(ImGui::GetFontSize() * 10);

            if (InputString("Search Actor", &searchString, InputOptions().Color(THEME_COLOR))) {
                actorSearchResults = GetActorsWithDescriptionContainingString(searchString);
                currentSelectedInDropdown = -1;
            }

            if (!SohUtils::IsStringEmpty(searchString) && !actorSearchResults.empty()) {
                std::string preview =
                    currentSelectedInDropdown == -1
                        ? "Please Select"
                        : ActorDB::Instance->RetrieveEntry(actorSearchResults[currentSelectedInDropdown]).desc;
                PushStyleCombobox(THEME_COLOR);
                if (ImGui::BeginCombo("Results", preview.c_str())) {
                    for (u8 i = 0; i < actorSearchResults.size(); i++) {
                        if (ImGui::Selectable(ActorDB::Instance->RetrieveEntry(actorSearchResults[i]).desc.c_str(),
                                              i == currentSelectedInDropdown)) {
                            currentSelectedInDropdown = i;
                            newActor.id = actorSearchResults[i];
                        }
                    }
                    ImGui::EndCombo();
                }
                PopStyleCombobox();
            }

            ImGui::Text("%s", GetActorDescription(newActor.id).c_str());
            if (ImGui::InputScalar("ID", ImGuiDataType_S16, &newActor.id, &one)) {
                newActor.params = 0;
            }

            CVarCheckbox("Advanced mode", CVAR_DEVELOPER_TOOLS("ActorViewer.AdvancedParams"),
                         CheckboxOptions().Tooltip("Changes the actor specific param menus with a direct input"));

            if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("ActorViewer.AdvancedParams"), 0)) {
                PushStyleInput(THEME_COLOR);
                ImGui::InputScalar("params", ImGuiDataType_S16, &newActor.params, &one);
                PopStyleInput();
            } else if (ActorViewerActorUsesParams(newActor.id)) {
                if (!ActorViewerHasCustomParameterEditor(newActor.id)) {
                    PushStyleInput(THEME_COLOR);
                    ImGui::InputScalar("params", ImGuiDataType_S16, &newActor.params, &one);
                    PopStyleInput();
                } else {
                    DrawGroupWithBorder(
                        [&]() {
                            ImGui::Text("Actor Specific Data");
                            newActor.params = ActorViewerDrawCustomParameterEditor(newActor.id, newActor.params);
                        },
                        "Actor Specific Data");
                }
            }

            ImGui::PushItemWidth(ImGui::GetFontSize() * 6);

            DrawGroupWithBorder(
                [&]() {
                    PushStyleInput(THEME_COLOR);
                    ImGui::Text("New Actor Position");
                    ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                    ImGui::InputScalar("X##NewPos", ImGuiDataType_Float, &newActor.pos.x);
                    ImGui::InputScalar("Y##NewPos", ImGuiDataType_Float, &newActor.pos.y);
                    ImGui::InputScalar("Z##NewPos", ImGuiDataType_Float, &newActor.pos.z);
                    ImGui::PopItemWidth();
                    PopStyleInput();
                },
                "New Actor Position");
            ImGui::SameLine();
            DrawGroupWithBorder(
                [&]() {
                    PushStyleInput(THEME_COLOR);
                    ImGui::Text("New Actor Rotation");
                    ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                    ImGui::InputScalar("X##NewRot", ImGuiDataType_S16, &newActor.rot.x);
                    ImGui::InputScalar("Y##NewRot", ImGuiDataType_S16, &newActor.rot.y);
                    ImGui::InputScalar("Z##NewRot", ImGuiDataType_S16, &newActor.rot.z);
                    ImGui::PopItemWidth();
                    PopStyleInput();
                },
                "New Actor Rotation");

            if (Button("Fetch from Link", ButtonOptions().Color(THEME_COLOR))) {
                Player* player = GET_PLAYER(gPlayState);
                Vec3f newPos = player->actor.world.pos;
                Vec3s newRot = player->actor.world.rot;
                newActor.pos = newPos;
                newActor.rot = newRot;
            }

            if (Button("Spawn", ButtonOptions().Color(THEME_COLOR))) {
                if (ActorDB::Instance->RetrieveEntry(newActor.id).entry.valid) {
                    Actor_Spawn(&gPlayState->actorCtx, gPlayState, newActor.id, newActor.pos.x, newActor.pos.y,
                                newActor.pos.z, newActor.rot.x, newActor.rot.y, newActor.rot.z, newActor.params);
                } else {
                    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                }
            }

            if (Button("Spawn as Child", ButtonOptions().Color(THEME_COLOR))) {
                Actor* parent = display;
                if (parent != NULL) {
                    if (newActor.id >= 0 && newActor.id < ACTOR_ID_MAX &&
                        ActorDB::Instance->RetrieveEntry(newActor.id).entry.valid) {
                        Actor_SpawnAsChild(&gPlayState->actorCtx, parent, gPlayState, newActor.id, newActor.pos.x,
                                           newActor.pos.y, newActor.pos.z, newActor.rot.x, newActor.rot.y,
                                           newActor.rot.z, newActor.params);
                    } else {
                        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                    }
                }
            }

            if (Button("Reset", ButtonOptions().Color(THEME_COLOR))) {
                newActor = { 0, 0, { 0, 0, 0 }, { 0, 0, 0 } };
            }

            ImGui::TreePop();
        }
        PopStyleHeader();
    } else {
        ImGui::Text("Global Context needed for actor info!");
    }
    ImGui::EndDisabled();
}

void ActorViewerWindow::InitElement() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorSpawn>([this](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);

        // Reload actor list if the new actor belongs to the selected category
        if (category == actor->category) {
            PopulateActorDropdown(actor->category, list);
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([this](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);

        // If the actor belongs to the selected category, we need to manually remove it, as it has not been removed from
        // the global actor array yet
        if (category == actor->category) {
            list.erase(std::remove(list.begin(), list.end(), actor), list.end());
        }
        if (display == actor) {
            display = nullptr;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([this](int16_t sceneNum) {
        display = nullptr;
        category = ACTORCAT_SWITCH;
        list.clear();
    });
}

void ActorViewer_RegisterNameTagHooks() {
    COND_HOOK(OnActorInit, CVAR_ACTOR_NAME_TAGS_ENABLED,
              [](void* actor) { ActorViewer_AddTagForActor(static_cast<Actor*>(actor)); });
}

static RegisterShipInitFunc initFunc(ActorViewer_RegisterNameTagHooks, { CVAR_ACTOR_NAME_TAGS_ENABLED_NAME });
