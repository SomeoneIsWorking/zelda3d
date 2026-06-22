// charcompare — character index table (see cc_index.h). The data array is generated.
#include "cc_index.h"

namespace cc {

#include "charcompare_index.inc" // defines kCcEntries[] / kCcEntryCount using IndexAnim/IndexEntry

const IndexEntry* CcIndex() {
    return kCcEntries;
}
int CcIndexCount() {
    return kCcEntryCount;
}

} // namespace cc
