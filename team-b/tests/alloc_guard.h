// Test-only allocation cap (cloud phase, 2026-09-30). Include in exactly one
// translation unit of a test executable: it replaces the global operator
// new/delete. While an AllocCap is alive, any single allocation above the cap
// throws std::bad_alloc, so a reader that allocates from an untrusted count
// (the class of bug fuzzing found) fails deterministically instead of
// depending on the OS's overcommit behaviour.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

namespace allocguard {
inline std::size_t g_cap = 0; // 0 = unlimited
struct AllocCap {
    explicit AllocCap(std::size_t cap) { g_cap = cap; }
    ~AllocCap() { g_cap = 0; }
};
} // namespace allocguard

void* operator new(std::size_t n) {
    if (allocguard::g_cap != 0 && n > allocguard::g_cap) throw std::bad_alloc();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
