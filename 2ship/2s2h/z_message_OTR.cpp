#include "BenPort.h"
#include <cstdio>
#include <cstdlib>
#include <ship/resource/ResourceManager.h>
#include "2s2h/resource/type/Scene.h"
#include <ship/utils/StringHelper.h>
#include "2s2h/resource/type/TextMM.h"
#include <message_data_static.h>

extern "C" MessageTableEntry* sMessageTableNES;
extern "C" MessageTableEntry* sMessageTableCredits;

// Entry counts for the two tables, so the next run can free what this one built. The tables
// themselves are raw `MessageTableEntry*` C globals with no length beside them, which is why these
// exist rather than a walk.
static size_t sMessageTableNESCount = 0;
static size_t sMessageTableCreditsCount = 0;

// Give back the PREVIOUS run's message tables.
//
// OTRMessage_Init mallocs both tables unconditionally and overwrites the globals, so every run leaked
// the last one: measured at 535,924 bytes of MM's 542,301-byte per-run leak, which was all of it bar
// noise. The comment on the malloc in LoadTable says "it's fine for now since we check elsewhere that
// the message table is already null" -- nothing nulls it and nothing checks, so that stopped being
// true whenever a second run became possible.
//
// The NES table owns a malloc'd `segment` per entry and needs a walk. The credits table does NOT: its
// segments are `c_str()` into the resource's own std::strings, so freeing them would be a double free
// of memory the ResourceManager owns -- only the array itself is ours.
static void FreePreviousMessageTables() {
    if (sMessageTableNES != nullptr) {
        for (size_t i = 0; i < sMessageTableNESCount; i++) {
            free((void*)sMessageTableNES[i].segment);
        }
        free(sMessageTableNES);
        sMessageTableNES = nullptr;
    }
    if (sMessageTableCredits != nullptr) {
        free(sMessageTableCredits);
        sMessageTableCredits = nullptr;
    }

    fprintf(stderr, "ZELDA3D CORE: message tables reset -- freed %zu NES and %zu credits entr(y/ies) from the "
                    "previous run.\n",
            sMessageTableNESCount, sMessageTableCreditsCount);
    fflush(stderr);

    sMessageTableNESCount = 0;
    sMessageTableCreditsCount = 0;
}

MessageTableEntry* OTRMessage_LoadTable(const char* filePath, bool isNES) {
    auto file = std::static_pointer_cast<SOH::TextMM>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(filePath));

    if (file == nullptr)
        return nullptr;

    // Allocate room for an additional message
    // OTRTODO: Should not be malloc'ing here. It's fine for now since we check elsewhere that the message table is
    // already null.
    MessageTableEntry* table = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * (file->messages.size() + 1));

    for (size_t i = 0; i < file->messages.size(); i++) {
        table[i].textId = file->messages[i].id;
        table[i].typePos = (file->messages[i].textboxType << 4) | file->messages[i].textboxYPos;
        table[i].segment = (const char*)malloc(file->messages[i].msg.size() + 11);

        auto segment = (char*)table[i].segment;

        segment[0] = file->messages[i].textboxType;
        segment[1] = file->messages[i].textboxYPos;
        segment[2] = file->messages[i].icon;
        segment[3] = (file->messages[i].nextMessageID & 0xFF00) >> 8;
        segment[4] = (file->messages[i].nextMessageID & 0x00FF);
        segment[5] = (file->messages[i].firstItemCost & 0xFF00) >> 8;
        segment[6] = (file->messages[i].firstItemCost & 0x00FF);
        segment[7] = (file->messages[i].secondItemCost & 0xFF00) >> 8;
        segment[8] = (file->messages[i].secondItemCost & 0x00FF);
        segment[9] = 0xFF;
        segment[10] = 0xFF;

        memcpy((void*)(&table[i].segment[11]), file->messages[i].msg.c_str(), file->messages[i].msg.size());

        table[i].msgSize = file->messages[i].msg.size() + 11;

        // if (isNES && file->messages[i].id == 0xFFFC)
        //_message_0xFFFC_nes = (char*)file->messages[i].msg.c_str();
    }

    if (isNES) {
        sMessageTableNESCount = file->messages.size();
    }
    return table;
}

extern "C" void OTRMessage_Init() {
    FreePreviousMessageTables();

    sMessageTableNES = OTRMessage_LoadTable("text/message_data_static/message_data_static", true);

    auto file2 = std::static_pointer_cast<SOH::TextMM>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
        "text/staff_message_data_static/staff_message_data_static"));
    sMessageTableCredits = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * file2->messages.size());
    sMessageTableCreditsCount = file2->messages.size();

    for (size_t i = 0; i < file2->messages.size(); i++) {
        sMessageTableCredits[i].textId = file2->messages[i].id;
        sMessageTableCredits[i].typePos = (file2->messages[i].textboxType << 4) | file2->messages[i].textboxYPos;
        sMessageTableCredits[i].segment = file2->messages[i].msg.c_str();
        sMessageTableCredits[i].msgSize = file2->messages[i].msg.size();
    }
}
