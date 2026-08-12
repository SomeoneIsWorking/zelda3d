#include "ObjectExtension.h"

#include <cstdio>

ObjectExtension& ObjectExtension::GetInstance() {
    static ObjectExtension instance;
    return instance;
}

ObjectExtension::Id ObjectExtension::RegisterId() {
    return NextId++;
}

void ObjectExtension::Free(const void* object) {
    if (object == nullptr) {
        return;
    }

    std::erase_if(Data, [&object](const auto& iter) {
        auto const& [key, value] = iter;
        return key.first == object;
    });
}

extern "C" void ObjectExtension_Free(const void* object) {
    ObjectExtension::GetInstance().Free(object);
}

// Drop every object extension this run created.
//
// `Data` is keyed by the OBJECT POINTER (an Actor*, in practice), and entries leave it only when
// ObjectExtension_Free is called for that object -- i.e. from Actor_Cleanup. A run that ends with
// actors still live therefore leaves their entries behind, and the next run allocates actors from a
// rebuilt arena at the SAME addresses, so a lookup hits the previous run's data. The values are held
// by value (std::any), so nothing dangles; what you get is a fresh actor silently wearing a dead
// one's state, which is harder to notice than a crash.
//
// NextId is deliberately NOT reset. The ids it hands out are stored in
// `ObjectExtension::Register<T>::Id` -- process-lifetime template statics that no run resets -- so
// rewinding the counter would issue ids that existing registrations already hold, and two unrelated
// extensions would share a key. The counter is engine-scoped precisely because its consumers are.
std::size_t ObjectExtension::ClearAll() {
    const std::size_t dropped = Data.size();
    Data.clear();
    return dropped;
}

extern "C" void ObjectExtension_ResetRunState(void) {
    const size_t dropped = ObjectExtension::GetInstance().ClearAll();

    // Count printed pass or fail: run 1 must report 0, and "cleared" on its own could not tell that
    // apart from a run inheriting a full table -- which is the only thing this function exists for.
    fprintf(stderr, "ZELDA3D SHARED: object extensions reset -- dropped %zu entr%s left by the previous run.\n",
            dropped, dropped == 1 ? "y" : "ies");
    fflush(stderr);
}
