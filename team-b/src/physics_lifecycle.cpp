// sr3physics/lifecycle.h implementation. spec-physics-runtime.md Sec4.1/
// Sec4.5 - see the header's own doc comments for citations.
#include "sr3physics/lifecycle.h"

namespace sr3physics {

namespace {
void call(const std::function<void()>& hook) {
    if (hook) hook();
}
}  // namespace

void PhysicsLifecycle::bringUp() {
    if (broughtUp_) return;  // Sec4.1 preamble: refuses a second call

    call(bringUpMemoryAndBaseSystem);
    call(bringUpWorld);
    call(bringUpJobQueue);
    call(bringUpBorderAndUtility);
    call(bringUpBodyTables);
    call(bringUpPerThreadMemoryAndPhantoms);

    broughtUp_ = true;  // Sec4.1 step 16: the guard byte set last
}

void PhysicsLifecycle::shutdown() {
    if (!broughtUp_) return;  // Sec4.5 preamble: no-op unless the guard byte is set

    call(shutdownGamePool);
    call(shutdownPhantoms);
    call(shutdownListeners);
    call(shutdownJobQueue);   // BEFORE the world (Sec4.5's own explicit emphasis)
    call(shutdownWorld);
    call(shutdownUtility);
    call(shutdownPerThreadMemory);
    call(shutdownBaseSystem);

    broughtUp_ = false;  // Sec4.5 step 9: the guard byte cleared
}

}  // namespace sr3physics
