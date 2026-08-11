#include <libultraship/libultraship.h>
#include "soh/resource/type/Scene.h"
#include <ship/utils/StringHelper.h>
#include "global.h"
#include "vt.h"
#include "soh/resource/type/Text.h"
#include <message_data_static.h>
#include "Enhancements/custom-message/CustomMessageManager.h"
#include "Enhancements/custom-message/CustomMessageTypes.h"

extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
extern "C" char* _message_0xFFFC_nes;

static void SetMessageEntry(MessageTableEntry& entry, const SOH::MessageEntry& msgEntry) {
    entry.textId = msgEntry.id;
    entry.typePos = (msgEntry.textboxType << 4) | msgEntry.textboxYPos;
    entry.segment = msgEntry.msg.c_str();
    entry.msgSize = static_cast<u32>(msgEntry.msg.size());
}

static void OTRMessage_LoadCustom(const std::string& folderPath, MessageTableEntry*& table, size_t tableSize) {
    auto lst = *Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(folderPath).get();

    for (auto& tPath : lst) {
        auto file = std::static_pointer_cast<SOH::Text>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(tPath));

        for (size_t j = 0; j < file->messages.size(); ++j) {
            // Check if same text ID exists already
            auto existingEntry = std::find_if(table, table + tableSize, [id = file->messages[j].id](const auto& entry) {
                return entry.textId == id;
            });

            if (existingEntry != table + tableSize) {
                // Replace existing message
                SetMessageEntry(*existingEntry, file->messages[j]);
            }
        }
    }
}

MessageTableEntry* OTRMessage_LoadTable(const std::string& filePath, bool isNES) {
    auto file = std::static_pointer_cast<SOH::Text>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(filePath));

    if (file == nullptr)
        return nullptr;

    // Allocate room for an additional message
    // OTRTODO: Should not be malloc'ing here. It's fine for now since we check elsewhere that the message table is
    // already null.
    MessageTableEntry* table = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * file->messages.size());

    for (size_t i = 0; i < file->messages.size(); i++) {
        SetMessageEntry(table[i], file->messages[i]);

        if (isNES && file->messages[i].id == 0xFFFC)
            _message_0xFFFC_nes = (char*)file->messages[i].msg.c_str();
    }
    OTRMessage_LoadCustom("override/" + filePath.substr(0, filePath.find_last_of('/')) + "/*", table,
                          file->messages.size() + 1);

    // Assert that the first message starts at the first text ID
    assert(table[0].textId == 0x0001);

    return table;
}

// Run-scoped reset, called from Zelda3D_CoreRunBegin. See zelda3d/core/zelda3d_core_lifecycle.c.
//
// The NULL checks below are what make these tables process-lifetime: build them once and every
// later call is a no-op. That was written to avoid re-malloc'ing a table in one game's lifetime,
// and under the launcher it means a SECOND RUN KEEPS THE FIRST RUN'S TABLES. Every `segment` in
// them is `msg.c_str()` -- a pointer into a SOH::Text resource owned by the ResourceManager that
// the previous game's session took down. Font_LoadOrderedFontNTSC then memcpy'd from freed memory
// during Play_Init.
//
// So the tables are freed here, at the start of a run, and OTRMessage_Init rebuilds them against
// THIS run's ResourceManager. Only the table arrays are ours to free; the strings they point at
// belong to the resources.
extern "C" void Zelda3D_MessageResetRunState(void) {
    MessageTableEntry** tables[] = {
        &sNesMessageEntryTablePtr, &sGerMessageEntryTablePtr,   &sFraMessageEntryTablePtr,
        &sJpnMessageEntryTablePtr, &sStaffMessageEntryTablePtr,
    };
    const int total = (int)(sizeof tables / sizeof tables[0]);
    int inherited = 0;

    for (int i = 0; i < total; i++) {
        if (*tables[i] != NULL) {
            inherited++;
            free(*tables[i]);
            *tables[i] = NULL;
        }
    }
    _message_0xFFFC_nes = NULL;

    fprintf(stderr, "ZELDA3D CORE: message tables reset -- %d of %d inherited from a previous run and freed.\n",
            inherited, total);
    fflush(stderr);
}

extern "C" void OTRMessage_Init() {
    // OTRTODO: Added a lot of null checks here so that we don't malloc the table multiple times causing a memory leak.
    // We really ought to fix the implementation such that we aren't malloc'ing new tables.
    // Once we fix the implementation, remove these NULL checks.
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr = OTRMessage_LoadTable("text/nes_message_data_static/nes_message_data_static", true);
    }
    if (sGerMessageEntryTablePtr == NULL) {
        sGerMessageEntryTablePtr = OTRMessage_LoadTable("text/ger_message_data_static/ger_message_data_static", false);
    }
    if (sFraMessageEntryTablePtr == NULL) {
        sFraMessageEntryTablePtr = OTRMessage_LoadTable("text/fra_message_data_static/fra_message_data_static", false);
    }
    if (sJpnMessageEntryTablePtr == NULL) {
        sJpnMessageEntryTablePtr = OTRMessage_LoadTable("text/jpn_message_data_static/jpn_message_data_static", false);
    }
    // Note: Make sure this loads after PAL nes_message_data_static, so that message 0xFFFC is definitely loaded if it
    // exists
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr =
            OTRMessage_LoadTable("text/nes_message_data_static/ntsc_nes_message_data_static", false);
    }

    if (sStaffMessageEntryTablePtr == NULL) {
        auto file2 =
            std::static_pointer_cast<SOH::Text>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
                "text/staff_message_data_static/staff_message_data_static"));
        // OTRTODO: Should not be malloc'ing here. It's fine for now since we check that the message table is already
        // null.
        sStaffMessageEntryTablePtr = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * file2->messages.size());

        for (size_t i = 0; i < file2->messages.size(); i++) {
            SetMessageEntry(sStaffMessageEntryTablePtr[i], file2->messages[i]);
        }
        OTRMessage_LoadCustom("override/text/staff_message_data_static/*", sStaffMessageEntryTablePtr,
                              file2->messages.size());

        // Assert staff credits start at the first credits ID
        assert(sStaffMessageEntryTablePtr[0].textId == 0x0500);
    }
}
