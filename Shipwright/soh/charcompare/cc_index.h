// charcompare — character index for the cascading selectors (TYPE -> thing -> ANIMATION).
//
// Data is generated from tools/skeldata/animmap.json by tools/gen_charcompare_index.py into
// charcompare_index.inc (compiled by cc_index.cpp). Each entry pairs a 3DS ZAR with its N64
// object and the N64 anims (each carrying the best-matched 3DS CSAB). The N64 *skeleton*
// symbol is NOT stored — cc_n64.cpp derives it at runtime from the object's OTR resources.
#pragma once

namespace cc {

struct IndexAnim {
    const char* n64;        // N64 animation symbol (e.g. "gGerudoRedNeutralAnim")
    const char* otr;        // its OTR resource path (objects/<object>/<symbol>)
    int frameCount;         // N64 frame count
    const char* csab;       // best-matched 3DS CSAB base name ("" if none)
};

struct IndexEntry {
    const char* category;   // TYPE: character/monster/creature/item/object/other
    const char* name;       // short display name (ZAR stem, e.g. "ge1")
    const char* zar;        // 3DS ZAR path (e.g. "/actor/zelda_ge1.zar")
    const char* object;     // N64 object folder (e.g. "object_ge1")
    const IndexAnim* anims; // N64 anims for this character
    int animCount;
};

// The generated table (sorted by category then name) and its length.
const IndexEntry* CcIndex();
int CcIndexCount();

} // namespace cc
