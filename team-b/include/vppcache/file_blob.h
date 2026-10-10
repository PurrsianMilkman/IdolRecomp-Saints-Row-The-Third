#pragma once

// A read-only view over a whole file's bytes: memory-mapped on Windows
// (this project's primary target - avoids an upfront heap copy of a
// multi-hundred-MB `.vpp_pc`, and lets the OS page cache do the sharing
// work across repeated opens within a short span), falling back to a
// plain heap read on any platform without the Windows mapping API (this
// project also has a portable GCC/Linux build, per STATE.md - see the
// cloud-phase ctest/fuzzing entries). Either way the caller sees the same
// `data()`/`size()` contract, and the memory stays valid for this object's
// lifetime - same non-owning-view contract vpp::Container already expects
// of its backing buffer (see vpp/container.h's class comment).
//
// Added 2026-10-08 for the persistent decoded-asset cache task. Move-only
// (owns a live OS mapping handle on Windows).

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "vpp/byte_view.h"

namespace vppcache {

class FileBlob {
public:
    FileBlob() = default;
    ~FileBlob();
    FileBlob(const FileBlob&) = delete;
    FileBlob& operator=(const FileBlob&) = delete;
    FileBlob(FileBlob&& other) noexcept;
    FileBlob& operator=(FileBlob&& other) noexcept;

    // Maps (or reads) `path` in full. Throws std::runtime_error on failure
    // (file not found, mapping failed, etc.) - same "loud failure, no
    // fabricated empty result" convention the rest of this project's
    // readers use.
    static FileBlob load(const std::filesystem::path& path);

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    vpp::ByteView view() const { return vpp::ByteView(data_, size_); }

private:
    void release();
#ifndef _WIN32
    // The portable (heap-read) fallback used by load() on non-Windows
    // builds - a private static member (not a free function) purely so
    // it can fill owned_/data_/size_ directly without a separate friend
    // declaration.
    static FileBlob loadViaHeapRead(const std::filesystem::path& path);
#endif

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;

    // Windows mapping handles (opaque void* so this header never pulls in
    // <windows.h> for every consumer - mirrors sr3render::Window's own
    // nativeHandle() convention). Both null when the portable heap-buffer
    // fallback is in use instead (owned_ non-empty in that case).
    void* fileHandle_ = nullptr;
    void* mappingHandle_ = nullptr;

    // Portable fallback storage (non-Windows, or if mapping fails and we
    // fall back deliberately): owns the bytes directly.
    std::vector<uint8_t> owned_;
};

} // namespace vppcache
