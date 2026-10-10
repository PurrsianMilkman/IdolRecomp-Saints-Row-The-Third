#include "vppcache/file_blob.h"

#include <fstream>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vppcache {

FileBlob::~FileBlob() { release(); }

FileBlob::FileBlob(FileBlob&& other) noexcept
    : data_(other.data_), size_(other.size_), fileHandle_(other.fileHandle_),
      mappingHandle_(other.mappingHandle_), owned_(std::move(other.owned_)) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.fileHandle_ = nullptr;
    other.mappingHandle_ = nullptr;
}

FileBlob& FileBlob::operator=(FileBlob&& other) noexcept {
    if (this == &other) return *this;
    release();
    data_ = other.data_;
    size_ = other.size_;
    fileHandle_ = other.fileHandle_;
    mappingHandle_ = other.mappingHandle_;
    owned_ = std::move(other.owned_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.fileHandle_ = nullptr;
    other.mappingHandle_ = nullptr;
    return *this;
}

void FileBlob::release() {
#ifdef _WIN32
    if (data_ && mappingHandle_) {
        // data_ points at the mapped view only when owned_ is empty (the
        // mapping path) - the fallback path points data_ at owned_'s own
        // buffer instead and never sets mappingHandle_.
        UnmapViewOfFile(data_);
    }
    if (mappingHandle_) CloseHandle(reinterpret_cast<HANDLE>(mappingHandle_));
    if (fileHandle_) CloseHandle(reinterpret_cast<HANDLE>(fileHandle_));
#endif
    fileHandle_ = nullptr;
    mappingHandle_ = nullptr;
    data_ = nullptr;
    size_ = 0;
    owned_.clear();
    owned_.shrink_to_fit();
}

#ifndef _WIN32
FileBlob FileBlob::loadViaHeapRead(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("FileBlob::load: cannot open '" + path.string() + "'");
    in.seekg(0, std::ios::end);
    std::streamoff len = in.tellg();
    in.seekg(0, std::ios::beg);
    if (len < 0) throw std::runtime_error("FileBlob::load: cannot determine size of '" + path.string() + "'");

    FileBlob blob;
    blob.owned_.resize(static_cast<size_t>(len));
    if (len > 0 && !in.read(reinterpret_cast<char*>(blob.owned_.data()), len)) {
        throw std::runtime_error("FileBlob::load: short read on '" + path.string() + "'");
    }
    blob.data_ = blob.owned_.data();
    blob.size_ = blob.owned_.size();
    return blob;
}
#endif // !_WIN32

FileBlob FileBlob::load(const std::filesystem::path& path) {
#ifdef _WIN32
    // FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE (not just
    // FILE_SHARE_READ): this is a read-only mapping (FILE_MAP_READ below),
    // so there is no correctness reason to exclusive-lock the SOURCE file
    // against writers - doing so would make this cache layer a well-
    // behaved reader turn into something that can block an unrelated
    // writer/replacer of the same path for as long as any OpenedArchive
    // (and its FileBlob) stays alive, e.g. across a long-lived
    // process/registry entry (item 2). Confirmed necessary, not just
    // theoretical: the content-hash self-invalidation synthetic test
    // (tests/synthetic_vppcache_test.cpp) rewrites its fixture archive
    // in place while an earlier FileBlob from the same path is still
    // open, and failed with FILE_SHARE_READ alone (the rewrite silently
    // no-op'd under a sharing violation) until this was added.
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("FileBlob::load: CreateFileW failed for '" + path.string() + "'");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        throw std::runtime_error("FileBlob::load: GetFileSizeEx failed for '" + path.string() + "'");
    }
    if (size.QuadPart == 0) {
        // CreateFileMappingW rejects a zero-length mapping outright - an
        // empty file is legitimate (rare, but not a reason to fail the
        // whole load), handled as a zero-size, non-null-dangling view.
        FileBlob blob;
        blob.fileHandle_ = file;
        blob.data_ = reinterpret_cast<const uint8_t*>(&blob); // non-null sentinel, never dereferenced (size 0)
        blob.size_ = 0;
        return blob;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        CloseHandle(file);
        throw std::runtime_error("FileBlob::load: CreateFileMappingW failed for '" + path.string() + "'");
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw std::runtime_error("FileBlob::load: MapViewOfFile failed for '" + path.string() + "'");
    }
    FileBlob blob;
    blob.fileHandle_ = file;
    blob.mappingHandle_ = mapping;
    blob.data_ = static_cast<const uint8_t*>(view);
    blob.size_ = static_cast<size_t>(size.QuadPart);
    return blob;
#else
    return loadViaHeapRead(path);
#endif
}

} // namespace vppcache
