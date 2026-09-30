// Shared helpers for the libFuzzer harnesses (cloud phase, 2026-09-30).
//
// Every harness feeds untrusted bytes to one reader and catches only the
// exceptions a reader is documented to throw on malformed input
// (FormatError subclasses of std::runtime_error, std::out_of_range from
// ByteView, std::length_error/invalid_argument from the standard library).
// Anything else - a sanitizer report, a crash, a hang, or an allocation over
// libFuzzer's -rss_limit_mb / -malloc_limit_mb - is a finding.
//
// Multi-buffer readers take a small fixed prefix: see each harness. The
// seed-capture wrapper (seed_capture.cpp) writes seeds in the same layout.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "vpp/byte_view.h"

namespace fuzzcommon {

inline uint32_t u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Runs `f`, swallowing the documented malformed-input exceptions.
template <typename F>
void guarded(F&& f) {
    try {
        f();
    } catch (const std::runtime_error&) {
    } catch (const std::logic_error&) { // out_of_range, length_error, invalid_argument
    }
}

} // namespace fuzzcommon
