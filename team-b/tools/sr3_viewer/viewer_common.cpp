#include "viewer_platform.h"
#include "viewer_common.h"
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

using namespace sr3viewer;

namespace sr3viewer {


std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return buf;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize(static_cast<size_t>(size));
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    fclose(f);
    return buf;
}


bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}


// Pulls an entry's bytes out of a container whether it is stored raw or
// zlib-compressed, accepting the recovery statuses the container layer
// already treats as usable.
bool entryBytes(const vpp::Container& container, size_t index, std::vector<uint8_t>& out) {
    const vpp::Entry& entry = container.entries()[index];
    if (entry.payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView raw = container.rawEntryBytes(index);
        out.assign(raw.data(), raw.data() + raw.size());
        return true;
    }
    vpp::DecompressResult r = container.decompressEntry(index);
    bool usable = r.status == vpp::DecodeStatus::Ok ||
                  r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                  r.status == vpp::DecodeStatus::ContentValidated ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}


// Finds `name` in a container, descending into nested containers, and
// returns its bytes. Also returns the container the entry was found in, so
// a paired g-file can be looked up alongside it.
bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].name == name) {
            if (!entryBytes(container, i, out)) return false;
            // Look for the paired file in this same container.
            for (size_t j = 0; j < container.entries().size(); ++j) {
                if (container.entries()[j].name == siblingName) {
                    entryBytes(container, j, siblingOut);
                    break;
                }
            }
            return true;
        }
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            if (findEntry(nested, name, out, siblingOut, siblingName)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}


std::string toLowerLocal(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace sr3viewer

namespace {

// Walks `container` ONCE, decompressing every real `.cpeg_pc`/`.cvbm_pc`
// pack it finds (regardless of whether anything has asked for one of its
// names yet) and recording every texture name it carries. Same recursive
// shape as findAndUploadTexture()'s own walk, but visiting each pack once
// rather than once per wanted name. First writer for a given real record
// wins on a later duplicate (matches the original code's own "first match
// in walk order" tie-break, which a caller relies on for determinism).
void buildTextureSearchCache(const vpp::Container& container, TextureSearchCache& cache,
                             std::vector<size_t>& path) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (entryBytes(container, i, cbytes) && !cbytes.empty()) {
                try {
                    auto cbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(cbytes));
                    sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                        sr3texture::ByteView(cbytesPtr->data(), cbytesPtr->size()));
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    size_t gIdx = container.entries().size();
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) { gIdx = j; break; }
                    }
                    if (gIdx < container.entries().size()) {
                        auto pathPtr = std::make_shared<std::vector<size_t>>(path);
                        for (size_t r = 0; r < pair.records().size(); ++r) {
                            TextureSearchCache::Located loc;
                            loc.cpegBytes = cbytesPtr;
                            loc.nestPath = pathPtr;
                            loc.gpegIndex = gIdx;
                            loc.pair = pair;
                            loc.recordIndex = r;
                            loc.recordNameLower = toLowerLocal(pair.records()[r].name);
                            loc.foundIn = name;
                            cache.all.push_back(std::move(loc));
                        }
                    }
                } catch (const std::exception&) {
                }
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                path.push_back(i);
                try { buildTextureSearchCache(nested, cache, path); } catch (...) { path.pop_back(); throw; }
                path.pop_back();
            } catch (const std::exception&) {
            }
        }
    }
}


// Re-fetches a Located's gpeg payload (bounded LRU, 192 MB cap).
std::shared_ptr<std::vector<uint8_t>> fetchGpeg(const vpp::Container& top, TextureSearchCache& cache,
                                                const TextureSearchCache::Located& loc) {
    const void* key = loc.nestPath.get();
    for (size_t k = 0; k < cache.gpegLru.size(); ++k) {
        if (cache.gpegLru[k].pathKey == key && cache.gpegLru[k].index == loc.gpegIndex) {
            auto hit = cache.gpegLru[k];
            cache.gpegLru.erase(cache.gpegLru.begin() + k);
            cache.gpegLru.insert(cache.gpegLru.begin(), hit);
            return hit.bytes;
        }
    }
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    try {
        // Nested Containers are non-owning windows on the (mapped) top archive, so a chain of
        // temporaries is valid while we walk it.
        std::vector<vpp::Container> chain;
        chain.reserve(loc.nestPath->size());
        const vpp::Container* cur = &top;
        for (size_t idx : *loc.nestPath) {
            chain.push_back(cur->openNested(idx));
            cur = &chain.back();
        }
        if (!entryBytes(*cur, loc.gpegIndex, *bytes) || bytes->empty()) return nullptr;
    } catch (const std::exception&) {
        return nullptr;
    }
    cache.gpegLruBytes += bytes->size();
    cache.gpegLru.insert(cache.gpegLru.begin(), {key, loc.gpegIndex, bytes});
    while (cache.gpegLru.size() > 1 && cache.gpegLruBytes > (size_t(192) << 20)) {
        cache.gpegLruBytes -= cache.gpegLru.back().bytes->size();
        cache.gpegLru.pop_back();
    }
    return bytes;
}

} // namespace

namespace sr3viewer {

// Finds the .cpeg_pc/.cvbm_pc pair holding `tgaName` and uploads that
// texture. A mesh's material block names its textures as .tga source
// names, which is not where the pixels live - so the archive has to be
// searched for the container that actually holds them.
//
// `cache`, when non-null, routes through TextureSearchCache instead of the
// original per-call archive walk (see that struct's own comment) - building
// it once on first use, then reusing it for every subsequent call through
// the SAME cache object. Every pre-existing call passes nullptr (the
// default) and takes the ORIGINAL, unmodified code path below.
bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName,
                          ID3D11Device* device, sr3render::UploadedTexture& out,
                          std::string& foundIn, TextureSearchCache* cache) {
    if (cache != nullptr) {
        if (!cache->built) {
            std::vector<size_t> nestPath;
            buildTextureSearchCache(container, *cache, nestPath);
            cache->built = true;
        }
        const std::string wantedLower = toLowerLocal(tgaName);
        auto memoIt = cache->uploadedByWantedName.find(wantedLower);
        if (memoIt != cache->uploadedByWantedName.end()) {
            out = memoIt->second;
            if (out.srv == nullptr) return false; // memoised "not found"
            for (const auto& loc : cache->all) {
                if (endsWithNoCase(loc.recordNameLower, wantedLower)) { foundIn = loc.foundIn; break; }
            }
            return true;
        }
        for (const auto& loc : cache->all) {
            if (!endsWithNoCase(loc.recordNameLower, wantedLower)) continue;
            std::string error;
            auto gbytes = fetchGpeg(container, *cache, loc);
            if (!gbytes) continue;
            if (sr3render::uploadTexture(device, loc.pair, loc.recordIndex,
                                         sr3texture::ByteView(gbytes->data(), gbytes->size()), out,
                                         error)) {
                foundIn = loc.foundIn;
                cache->uploadedByWantedName[wantedLower] = out;
                return true;
            }
        }
        sr3render::UploadedTexture notFound;
        cache->uploadedByWantedName[wantedLower] = notFound; // memoise the miss too - still saves the rescan
        return false;
    }

    // ---- ORIGINAL code path, byte-for-byte unchanged from before this
    // session's caching addition - every pre-existing caller hits exactly
    // this. ----------------------------------------------------------
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (!entryBytes(container, i, cbytes) || cbytes.empty()) continue;
            try {
                sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                    sr3texture::ByteView(cbytes.data(), cbytes.size()));
                for (size_t r = 0; r < pair.records().size(); ++r) {
                    if (!endsWithNoCase(pair.records()[r].name, tgaName)) continue;
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    std::vector<uint8_t> gbytes;
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) {
                            entryBytes(container, j, gbytes);
                            break;
                        }
                    }
                    if (gbytes.empty()) continue;
                    std::string error;
                    if (sr3render::uploadTexture(device, pair, r,
                                                 sr3texture::ByteView(gbytes.data(), gbytes.size()),
                                                 out, error)) {
                        foundIn = name;
                        return true;
                    }
                }
            } catch (const std::exception&) {
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                if (findAndUploadTexture(nested, tgaName, device, out, foundIn)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}


// ===========================================================================
// `city` command (2026-10-03): the first multi-cell STREAMING viewer - loads
// and draws the real hN ("Zone (High LOD)", engine level 4) fine-cell
// containers around a moving fly camera, acquiring and releasing them by the
// engine's own level-4 box rule. The step from "one hN cell on its own"
// (`clmesh-tile`) to "the city".
//
// WHAT IS SOLID GROUND HERE (and where each number comes from):
//
//   * Cell naming: `CCRRhN` = fine cell N of tile (col CC, row RR),
//     spec-world-streaming.md Sec10.1/Sec10.3/Sec10.6(e). The CC=col /
//     RR=row reading is NOT taken on trust: `--lattice-check` re-measures it
//     against this archive's own `.czh_pc` FAST headers (header origin +
//     SR3Z record position words, the exact Sec10.6(g) method - header data
//     only, nothing from `.czn_pc`) and prints the residuals for BOTH the
//     CC=col and the swapped CC=row parse.
//   * Tile-to-tile world position: Sec10.6(g), VALIDATED-BY-DATA -
//       centre_x = 320*col - 4000 - 160*(row mod 2)
//       centre_z = 3640 - 280*row
//     and the fine cell centre = tile centre + (-80,+70) / (+80,+70) /
//     (-80,-70) / (+80,-70) for N = 0/1/2/3. Fine cell = 160 x 140 m
//     (Sec10.3 level-4 row).
//   * Which cells are wanted: Sec10.4 steps 1-3 / Sec11.1 - an axis-aligned
//     box centred on the focus, half-extent = level-4 window / 2 =
//     (79.75, 69.75) m; every cell whose box overlaps it (touching does not
//     count - Sec10.4 says the engine shrinks the box by "a small epsilon"
//     whose value is not given; strict inequality is used here, which is
//     immaterial at this scale since the window is 0.5 m smaller than a cell
//     in each axis); only cells with a real registered container are kept
//     (here: a real `CCRRhN.str2_pc` entry in the archive). Level 4 never
//     overlaps more than 4 cells, so its capacity-4 sort/truncate never
//     fires (Sec10.4) - asserted at run time, not assumed.
//   * Reconciliation: the SAME box drives acquire and release - no keep
//     margin / hysteresis (Sec11.2 confirmed negative); release of a dropped
//     cell is immediate (Sec11.4: refcount-to-zero frees in the same call);
//     there is no per-tick acquire budget (Sec11.4) - every newly-wanted cell
//     is loaded in the same pass it becomes wanted. Release is done before
//     acquire, the order Sec10.4 describes the reconciliation in.
//   * Focus point: this viewer's own fly-camera eye (x, z). This is a
//     standalone offline viewer, so the engine's own OPEN "what feeds/calls
//     the reconciliation every frame" question (Sec10.4/Sec11.3/Sec11.5) is
//     not ours to answer: this render loop IS the per-frame driver, by
//     construction.
//
// CHOSEN ENGINEERING DECISIONS (not spec facts):
//
//   * Cadence: the wanted set is recomputed EVERY rendered frame (a few
//     hundred box tests - negligible), and loading/evicting happens only on
//     the delta, exactly the diff the engine's reconciliation does. No
//     throttle is needed because an unchanged wanted set already costs
//     nothing; loads are synchronous in the frame that needs them (a visible
//     hitch on crossing a boundary - async loading is deferred, see below).
//   * Only the DEFAULT variant `CCRRhN` of each cell is loaded. The engine
//     acquires a cell record's SELECTED variant (Sec10.3); which `CCRR^name`
//     variant is selected at run time is game-state, not modelled here.
//     Variant counts are reported by `--list`.
//   * Textures: TextureSearchCache, one per archive, built once on first use
//     and kept for the whole run (shared by every cell, never released on
//     cell eviction) - the same reuse runClmeshTile() does across props.
//     A material's texture is looked up in the cell's own archive first,
//     then in every other archive given (the city is split across
//     sr3_city_0/1; a cell's textures need not sit in its own file).
//     In the real engine textures belong to the tile bundle (level 3, Low
//     Mips, Sec10.6(a)), a level this viewer does not stream.
//   * `--window-scale S` (default 1 = the engine's rule) is a DEBUG-ONLY
//     override that widens the window for an overview capture of several
//     tiles at once; any run using it prints a WARNING that its wanted set
//     is not the engine's level-4 rule.
//
// WHAT IS NOT SOLID GROUND, AND STAYS EXACTLY AS HONEST AS `clmesh-tile`:
// WHERE EACH PROP SITS INSIDE ITS CELL. Real per-instance placement of
// `.clmesh_pc` objects was investigated and did NOT resolve (see
// runClmeshTile()'s own top comment and tools/tile1018_placement_probe.cpp).
// So inside each cell the props are laid out on the same kind of FAKE
// fixed local grid `clmesh-tile` uses - each prop's own bounding-box centre
// moved to an arbitrary grid slot - with ONE change: the grid is fitted to
// the cell's own real 160 x 140 m footprint (cols = ceil(sqrt(n*160/140)))
// and translated to the cell's REAL world centre, instead of
// `clmesh-tile`'s footprint-sized slots at a shared local origin (which
// would make adjacent cells' grids overlap by hundreds of metres and hide
// the lattice). A prop larger than its slot spills into its neighbours
// (counted in the log). Props keep their real local Y. Nothing here claims
// real within-cell placement; the stdout banner says so on every run.
//
// DELIBERATELY DEFERRED (with reason):
//   * real within-cell placement - unresolved, see above (not this pass's to
//     solve; it needs the cross-manifest name-index investigation);
//   * levels 0 / 3 (`awld_compact` ~L2, tile bundle ~L1 PROPS) - only level 4
//     is streamed for PROPS, so at distance there is no LOD backdrop. [UPDATE
//     2026-10-07, ground/terrain follow-up task: this does NOT mean no
//     ground is ever drawn - see CityGroundTile's own "GROUND/TERRAIN" top
//     comment below. The real terrain geometry (.czh_pc/.czn_pc/.gzn_pc) for
//     a wanted cell's own tile is now additionally located - recursing into
//     nested `.str2_pc` containers exactly as findEntry() already does for
//     every other caller (the SAME real search --lattice-check uses for
//     .czh_pc; an earlier pass of this fix wrongly assumed "top-level
//     archive entry" and was corrected same-day, see findTileZoneEntryName()'s
//     own doc comment) - and rendered - this is NOT the same thing as streaming level
//     3's own container/windowing machinery (its ~L1 props/textures/variant
//     selection stay exactly as deferred as before); it is a direct,
//     targeted read of that one real file trio, reconciled at tile
//     granularity alongside the hN cells above.]
//   * `^name` variant selection, see above;
//   * asynchronous loading (a worker-thread parse + upload) - loads are
//     synchronous; the per-cell load time is logged so the hitch is visible;
//   * terrain / ground HEIGHT for PROP placement specifically - props still
//     keep their own local Y and the outlines still sit at y = 0; the real
//     ground MESH itself is no longer in this deferred list (see the UPDATE
//     above) but nothing here derives a per-prop height from it;
//   * lighting / material consistency across cells - same plain
//     diffuse-textured fallback shader `clmesh`/`clmesh-tile` use;
//   * a memory budget (Sec9.6 high-LOD allocator budgets) - up to 4 resident
//     cells is small enough that no budget is enforced;
//   * a mesh duplicated into several cells (Sec10.5/Sec10.6(c): a mesh
//     spanning several sub-boxes is registered in each) is drawn once per
//     resident cell - de-duplication needs real placement first.
//
// STRICTLY ADDITIVE: every type/function below is new and only reached from
// the new `city` branch in main(). The per-prop load below MIRRORS
// runClmeshTile()'s per-prop block step by step (same parse, same
// render-group-0 g-cursor chaining, same per-channel masked upload, same
// texture lookup) rather than refactoring that function's body.
// ===========================================================================

// 64-bit-safe whole-file read: readFile() above uses ftell()'s 32-bit
// `long`, which cannot size `sr3_city_1.vpp_pc` (2,611,376,351 bytes).
std::vector<uint8_t> readFileLargeC(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return buf;
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize(static_cast<size_t>(size));
        size_t done = 0;
        while (done < buf.size()) {
            const size_t want = (std::min)(buf.size() - done, static_cast<size_t>(256u) << 20);
            const size_t got = fread(buf.data() + done, 1, want, f);
            if (got == 0) break;
            done += got;
        }
        if (done != buf.size()) buf.clear();
    }
    fclose(f);
    return buf;
}

MappedBytes mapFileLargeC(const std::string& path) {
    MappedBytes m;
    try { m.blob = vppcache::FileBlob::load(path); } catch (const std::exception&) {}
    return m;
}


// walk_demo v2 (--v2, FIX 2 - see runWalkDemo()'s own "V2" doc comment):
// builds a view-projection matrix from an EXPLICIT eye + right/up/forward
// (the follow camera's own real committed output, spec-player-runtime.md
// Sec4.10), instead of deriving right/up/forward from a yaw/pitch pair the
// way buildFreeViewProjection() does. Everything past that derivation - the
// look-at view matrix and the projection matrix - is byte-for-byte the SAME
// construction buildFreeViewProjection() (src/mesh_renderer.cpp) already
// uses, copied rather than refactored so neither function's already-
// confirmed behaviour is put at risk by the other changing it. `right`/`up`/
// `forward` are trusted as already-orthonormal (the camera system's own
// RotationFromAngles() guarantees this, confirmed by
// tests/synthetic_player_camera_test.cpp's own orthonormality checks) - no
// re-normalisation is done here, unlike buildFreeViewProjection's own
// derivation from a single forward vector.
void buildViewProjectionFromCamera(const float eye[3], const float right[3], const float up[3],
                                   const float forward[3], float fovYRadians, float aspect, float nearZ,
                                   float farZ, float outMatrix[16]) {
    float view[16] = {
        right[0], up[0], forward[0], 0.0f,
        right[1], up[1], forward[1], 0.0f,
        right[2], up[2], forward[2], 0.0f,
        -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]),
        -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]),
        -(forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2]),
        1.0f,
    };
    const float h = 1.0f / std::tan(fovYRadians * 0.5f);
    const float w = h / aspect;
    const float q = farZ / (farZ - nearZ);
    float proj[16] = {
        w, 0, 0, 0,
        0, h, 0, 0,
        0, 0, q, 1,
        0, 0, -nearZ * q, 0,
    };
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += view[r * 4 + k] * proj[k * 4 + c];
            outMatrix[r * 4 + c] = sum;
        }
    }
}

} // namespace sr3viewer