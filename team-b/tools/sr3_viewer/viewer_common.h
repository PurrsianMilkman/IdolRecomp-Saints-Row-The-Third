#pragma once

#include "viewer_platform.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3texture/texture_pair.h"
#include "vpp/container.h"
#include "vppcache/shader_cache.h"
#include "vppcache/file_blob.h"

namespace sr3viewer {
std::vector<uint8_t> readFile(const std::string& path);

bool endsWithNoCase(const std::string& s, const std::string& suffix);

bool entryBytes(const vpp::Container& container, size_t index, std::vector<uint8_t>& out);

bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName);

std::string toLowerLocal(std::string s);


// Opt-in per-render-call cache (follow-on task, 2026-09-30): the ORIGINAL
// findAndUploadTexture() below re-walks and re-decompresses the WHOLE
// archive tree from scratch for every distinct texture name it is asked
// for - fine for a handful of materials, but measured to become genuinely
// slow (multiple minutes, not finished within a reasonable session budget)
// against `sr3_city_0.vpp_pc` (1.5 GB) for a many-material prop like
// `airport_controltower.clmesh_pc` (19 materials, mostly DISTINCT texture
// names - a simple "skip a name already looked up" memo alone would not
// have fixed this, since most names are only asked for once; the real cost
// is the repeated whole-archive walk+decompress itself).
//
// TextureSearchCache turns that into "walk + decompress every real
// `.cpeg_pc`/`.cvbm_pc` pack in the archive EXACTLY ONCE, indexing every
// texture name it carries", so a caller doing many lookups against the
// SAME archive (one `clmesh` render call, one or many materials) pays the
// walk once and then does an in-memory scan per lookup - still real work
// (a real GPU upload still happens per distinct texture), just not a
// repeated archive walk.
//
// STRICTLY OPT-IN, so every PRE-EXISTING call site (both inside runMesh(),
// and the recursive call inside findAndUploadTexture() itself) is
// byte-for-byte UNCHANGED: they pass no cache argument, which hits the
// exact original code path below, unmodified - not just "expected to
// behave the same", provably the same instructions. This is what makes
// "existing callers still behave identically" true by construction rather
// than by re-verification alone (re-verified anyway - see this session's
// report for the brad re-run).
struct TextureSearchCache {
    bool built = false;
    struct Located {
        std::shared_ptr<std::vector<uint8_t>> cpegBytes; // kept alive for TexturePair's own lifetime
        // The (large) .gpeg_pixel payload is NOT retained: decompressing every pack in a multi-GB
        // city archive and keeping the bytes pushed private bytes past 10 GB. It is located by
        // (nested-container index path, entry index) and re-fetched on demand through gpegLru.
        std::shared_ptr<std::vector<size_t>> nestPath;
        size_t gpegIndex = 0;
        sr3texture::TexturePair pair;
        size_t recordIndex = 0;
        std::string recordNameLower; // the FULL real record name, lowercased - suffix-matched at lookup,
                                     // same semantic as the original endsWithNoCase() search
        std::string foundIn;
    };
    // Not keyed for O(1) exact lookup on purpose: the ORIGINAL search
    // matches by SUFFIX (endsWithNoCase(recordName, wantedName)), which a
    // plain equality-keyed map cannot reproduce if a real record name ever
    // carries a path-like prefix. A linear scan over this (in-memory, a few
    // thousand entries at most) is still vastly cheaper than one whole-
    // archive walk+decompress per material, and preserves the exact
    // original match rule rather than a narrower approximation of it.
    std::vector<Located> all;
    // Bounded LRU of recently fetched gpeg payloads (key = nestPath ptr + index); capped by bytes.
    struct GpegSlot { const void* pathKey; size_t index; std::shared_ptr<std::vector<uint8_t>> bytes; };
    std::vector<GpegSlot> gpegLru; // front = most recent
    size_t gpegLruBytes = 0;
    std::map<std::string, sr3render::UploadedTexture> uploadedByWantedName; // memo: a name asked for twice reuses one GPU upload
};

bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName,
                          ID3D11Device* device, sr3render::UploadedTexture& out,
                          std::string& foundIn, TextureSearchCache* cache = nullptr);


// Multi-object scene composition + free-roaming camera (HANDOFF.md §9's
// engine/renderer-infrastructure task, deliverables 2-4): every reader
// below (sr3mesh, sr3geometry, sr3vehicle) is used exactly as the existing
// `mesh` command already uses it - nothing here decodes any file format,
// this is purely a placement/camera layer built on top of already-
// confirmed readers.
//
// SCENE COMPOSITION (deliverable 2) is deliberately just PlacedObject +
// std::vector<PlacedObject> below: a small in-memory list of {uploaded
// renderer, world transform}. No file format, no serialization - the task
// brief is explicit that inventing a "scene file format" here would be
// exactly the kind of unfounded invention this project avoids elsewhere,
// so placement is a fixed, documented layout (see the buildWorldMatrix
// calls in runScene()), not something read from disk.
//
// FREE CAMERA (deliverable 3): keyboard fly controls polled once per frame
// via GetAsyncKeyState, NOT hooked into sr3render::Window's WM_KEYDOWN/
// WM_KEYUP handling. window.h's own header comment is explicit that
// Window's message loop has exactly one deliberate input behaviour
// (Escape-to-close) and nothing else; polling here needed no change to
// that already-working, already-used-by-every-other-command class at all.
// buildOrbitViewProjection - and every command that relies on it (mesh/
// pose/animpose) - is completely untouched; this is an ADDITIONAL camera
// mode via the new buildFreeViewProjection(), not a replacement.

struct PlacedObject {
    std::unique_ptr<sr3render::MeshRenderer> renderer;
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::string label;
};

std::vector<uint8_t> readFileLargeC(const std::string& path);



// Read-only file-backed mapping (FileBlob): pages are not committed memory,
// unlike readFileLargeC()'s heap copy. Empty (size 0) on failure.
struct MappedBytes {
    vppcache::FileBlob blob;
    const uint8_t* data() const { return blob.data(); }
    size_t size() const { return blob.size(); }
    bool empty() const { return blob.size() == 0; }
};

MappedBytes mapFileLargeC(const std::string& path);

void buildViewProjectionFromCamera(const float eye[3], const float right[3], const float up[3],
                                   const float forward[3], float fovYRadians, float aspect, float nearZ,
                                   float farZ, float outMatrix[16]);

} // namespace sr3viewer
