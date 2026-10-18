// runWalkDemo() split: city cell/ground streaming and the --collision wiring.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupStreaming(WalkSession& sess) {
    auto& tintMode = sess.tintMode;
    auto& textures = sess.textures;
    auto& outlines = sess.outlines;
    auto& v2 = sess.v2;
    auto& collisionEnabled = sess.collisionEnabled;
    auto& archives = sess.archives;
    auto& cells = sess.cells;
    auto& loaded = sess.loaded;
    auto& totalLoads = sess.totalLoads;
    auto& totalEvicts = sess.totalEvicts;
    auto& groundTiles = sess.groundTiles;
    auto& loadCell = sess.loadCell;
    auto& lastWanted = sess.lastWanted;
    auto& firstReconcile = sess.firstReconcile;
    auto& reconcile = sess.reconcile;
    auto& pm = *sess.pm_;
    auto& followCam = *sess.followCam_;
    auto& device = *sess.device_;
    // Real ground/terrain, reconciled at TILE granularity - COPIED VERBATIM
    // from runCity() just above, same as loadCell/reconcile below (see
    // CityGroundTile's own "GROUND/TERRAIN" top comment for the root cause).

    // --collision (see runWalkDemo()'s own doc comment above for the full
    // citation breakdown): collisionWorld/collisionDriver are declared here,
    // not inside the `if`, so they outlive the frame loop below -
    // ClmeshTriangleCollisionWorld holds a REFERENCE to `loaded`, so it must
    // be declared after `loaded` itself (just above) and genuinely
    // re-queries whatever is currently streamed in each frame, not a
    // snapshot. Default radius/height/groundY (0.4/1.8/0.0) match
    // character_collision.h's own defaults - groundY=0.0 matches this
    // scene's fake-grid y=0 ground.
    //
    // 2026-10-08: real `.clmesh_pc` collision triangles (ClmeshTriangleCollisionWorld,
    // see its own doc comment above) replace the former render-mesh-AABB
    // stand-in (CityPropAabbCollisionWorld, kept defined above for
    // reference/history - no longer constructed anywhere in this file).
    sess.collisionWorld_.emplace(loaded);
    auto& collisionWorld = *sess.collisionWorld_;
    sess.collisionDriver_.emplace(collisionWorld);
    auto& collisionDriver = *sess.collisionDriver_;
    // Real camera collision (2026-10-08 castRay/shape-cast follow-up task,
    // see RealCameraCollision's own doc comment above for the full
    // citation): declared here, alongside collisionWorld just above, for
    // the SAME reason that one is - it holds a reference to `loaded` and
    // must outlive the frame loop below, genuinely re-querying whatever is
    // currently streamed in each frame rather than a load-time snapshot.
    sess.realCameraCollision_.emplace(loaded);
    auto& realCameraCollision = *sess.realCameraCollision_;
    if (v2 && collisionEnabled) {
        // Supersedes the NoHitCameraCollision wiring set just above (search
        // "followCam.collision = &followCamCollision" earlier in this
        // function) - gated on the SAME --collision flag as the character
        // proxy's own real-geometry wiring, so walk_demo_v2's frozen golden
        // (--v2 without --collision) is byte-for-byte unaffected; only
        // `walk --v2 --collision` actually exercises the real swept-sphere
        // camera query.
        followCam.collision = &realCameraCollision;
    }
    if (collisionEnabled) {
        pm.collisionHook = [&](sr3player::Vec3 from, sr3player::Vec3 vel,
                               double dt) -> sr3player::PlayerMovement::CollisionHookResult {
            const sr3player::CapsuleStepResult r = collisionDriver.step(from, vel, dt);
            if (!r.hits.empty()) {
                for (const auto& h : r.hits) {
                    std::printf("[collision] frame-local wall hit: surface %u at (%.2f %.2f %.2f) normal (%.2f "
                                "%.2f %.2f) passes=%d\n",
                                h.surfaceId, h.point.x, h.point.y, h.point.z, h.normal.x, h.normal.y, h.normal.z,
                                r.passesRun);
                }
            }
            return sr3player::PlayerMovement::CollisionHookResult{r.position, r.velocity, r.grounded};
        };
    }

    // loadCell/reconcile: COPIED VERBATIM from runCity() just above (same
    // lambda bodies, same capture set) - deliberately not re-derived, so
    // this command's own streaming behaviour cannot silently drift from
    // `city`'s already-frozen one.
    loadCell = [&](size_t cellIndex, int frame) {
        const CityCell& c = cells[cellIndex];
        CityArchive& a = *archives[c.archiveIndex];
        auto lc = std::make_unique<CityLoadedCell>();
        lc->cellIndex = cellIndex;
        const double t0 = cityNowMs();
        try {
            vpp::Container str2 = a.top->openNested(c.entryIndex);
            std::vector<std::string> clNames;
            for (const auto& e : str2.entries())
                if (endsWithNoCase(e.name, ".clmesh_pc")) clNames.push_back(e.name);
            lc->propEntries = clNames.size();
            for (const auto& nm : clNames) {
                CityProp p;
                std::string why;
                // --collision (2026-10-08 real-collision follow-up task):
                // ALSO capture render-group-0's real local-space collision
                // triangles when the flag is set - nullptr (no extra work)
                // otherwise, matching every other caller of loadCityProp().
                std::vector<std::array<float, 3>> localCollisionTris;
                if (loadCityProp(str2, nm, archives, c.archiveIndex, device.device(), textures && !tintMode, p, why,
                                 collisionEnabled ? &localCollisionTris : nullptr)) {
                    lc->texturesWanted += p.texturesWanted;
                    lc->texturesFound += p.texturesFound;
                    lc->texturesFoundElsewhere += p.texturesFoundElsewhere;
                    if (collisionEnabled) p.collisionLocalTrisStash = std::move(localCollisionTris);
                    lc->props.push_back(std::move(p));
                } else {
                    ++lc->propsFailed;
                    std::printf("[stream]   %s: prop %s SKIPPED: %s\n", c.name.c_str(), nm.c_str(), why.c_str());
                }
            }
        } catch (const std::exception& ex) {
            std::printf("[stream]   %s: container open failed: %s\n", c.name.c_str(), ex.what());
        }
        const size_t m = lc->props.size();
        if (m > 0) {
            const int cols = (std::max)(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(m) *
                                                                                 kFineCellHalfX / kFineCellHalfZ))));
            const int rows = static_cast<int>((m + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
            const float slotX = 2.0f * kFineCellHalfX / static_cast<float>(cols);
            const float slotZ = 2.0f * kFineCellHalfZ / static_cast<float>(rows);
            for (size_t i = 0; i < m; ++i) {
                CityProp& p = lc->props[i];
                const int ci = static_cast<int>(i % static_cast<size_t>(cols));
                const int ri = static_cast<int>(i / static_cast<size_t>(cols));
                const float sx = c.centreX - kFineCellHalfX + (static_cast<float>(ci) + 0.5f) * slotX;
                const float sz = c.centreZ + kFineCellHalfZ - (static_cast<float>(ri) + 0.5f) * slotZ;
                const float lcx = (p.localMin[0] + p.localMax[0]) * 0.5f;
                const float lcz = (p.localMin[2] + p.localMax[2]) * 0.5f;
                if (p.localMax[0] - p.localMin[0] > slotX || p.localMax[2] - p.localMin[2] > slotZ)
                    ++lc->propsOverflowSlot;
                sr3render::buildWorldMatrix(sx - lcx, 0.0f, sz - lcz, 0.0f, 1.0f, p.world);
                // --collision: build this prop's real-triangle BVH NOW that
                // its world translation (the fake per-cell grid slot the
                // comment above this lambda already covers - see
                // runCity()'s own top comment for the honest "props inside
                // a cell are on a FAKE local grid" caveat, which applies
                // here unchanged) is finally known. ClmeshTriangleCollisionWorld
                // below reads p.collisionBvh, never p.collisionLocalTrisStash
                // directly.
                if (collisionEnabled && !p.collisionLocalTrisStash.empty()) {
                    p.collisionBvh = BuildCollisionBvh(p.collisionLocalTrisStash, p.world[12], p.world[13],
                                                       p.world[14]);
                    std::vector<std::array<float, 3>>().swap(p.collisionLocalTrisStash);
                }
            }
        }
        static const float kPalette[6][3] = {{0.90f, 0.35f, 0.30f}, {0.30f, 0.75f, 0.35f}, {0.30f, 0.50f, 0.95f},
                                             {0.95f, 0.80f, 0.25f}, {0.80f, 0.40f, 0.85f}, {0.25f, 0.85f, 0.85f}};
        const int pi = (c.col % 3) + 3 * (c.row % 2);
        for (int k = 0; k < 3; ++k) lc->tint[k] = kPalette[pi][k];
        if (outlines) {
            lc->outline = std::make_unique<sr3render::MeshRenderer>();
            std::string oerr;
            if (!uploadCityOutline(*lc->outline, device.device(), c.centreX, c.centreZ, kFineCellHalfX,
                                   kFineCellHalfZ, true, oerr))
                lc->outline.reset();
        }
        lc->loadMs = cityNowMs() - t0;
        ++totalLoads;
        std::printf("[stream] frame %d LOAD  %-8s (%s, col %d row %d N %d) centre (%.1f, %.1f): %zu/%zu props "
                    "uploaded, %zu failed, %zu larger than their fake-grid slot, textures %zu/%zu (%zu of them "
                    "from another archive), %.0f ms\n",
                    frame, c.name.c_str(), a.label.c_str(), c.col, c.row, c.n, static_cast<double>(c.centreX),
                    static_cast<double>(c.centreZ), lc->props.size(), lc->propEntries, lc->propsFailed,
                    lc->propsOverflowSlot, lc->texturesFound, lc->texturesWanted, lc->texturesFoundElsewhere,
                    lc->loadMs);
        loaded[cellIndex] = std::move(lc);
    };

    // FIX 3 (--v2 only, see runWalkDemo()'s own "V2" doc comment): `focusX`/
    // `focusZ` select which cells stream in. Without --v2 the call site
    // below still passes eye[0]/eye[2] (the free-fly debug camera) exactly
    // as before - byte-for-byte unchanged. With --v2 the call site passes
    // brad's OWN live position instead, so the loaded cells follow wherever
    // the scripted walk actually puts him rather than a separately-chosen
    // debug eye.
    reconcile = [&](int frame, float focusX, float focusZ) {
        std::vector<size_t> wanted = cityWantedCells(cells, focusX, focusZ, 1.0f);
        if (wanted.size() > kLevel4Capacity) {
            std::printf("[stream] frame %d WARNING: %zu cells overlap the level-4 window - Sec10.4 says this "
                        "cannot exceed %zu; lattice/window constants are wrong somewhere\n",
                        frame, wanted.size(), kLevel4Capacity);
        }
        if (!firstReconcile && wanted == lastWanted) return;
        firstReconcile = false;
        lastWanted = wanted;
        std::string names;
        for (size_t idx : wanted) names += (names.empty() ? "" : ",") + cells[idx].name;
        std::printf("[stream] frame %d focus (%.1f, %.1f): wanted = {%s}\n", frame, static_cast<double>(focusX),
                    static_cast<double>(focusZ), names.c_str());
        const std::set<size_t> wantedSet(wanted.begin(), wanted.end());
        for (auto it = loaded.begin(); it != loaded.end();) {
            if (wantedSet.count(it->first) == 0) {
                const CityCell& c = cells[it->first];
                std::printf("[stream] frame %d EVICT %-8s (col %d row %d N %d) - %zu props released\n", frame,
                            c.name.c_str(), c.col, c.row, c.n, it->second->props.size());
                it = loaded.erase(it);
                ++totalEvicts;
            } else {
                ++it;
            }
        }
        for (size_t idx : wanted)
            if (loaded.count(idx) == 0) loadCell(idx, frame);

        // Ground/terrain reconciliation at TILE granularity - COPIED
        // VERBATIM from runCity()'s own reconcile() above (see
        // CityGroundTile's "GROUND/TERRAIN" top comment for the root cause).
        std::set<std::pair<int, int>> wantedTiles;
        for (size_t idx : wanted) wantedTiles.insert({cells[idx].col, cells[idx].row});
        for (auto it = groundTiles.begin(); it != groundTiles.end();) {
            if (wantedTiles.count(it->first) == 0) {
                std::printf("[ground] frame %d EVICT tile %02d%02d\n", frame, it->first.first, it->first.second);
                it = groundTiles.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& t : wantedTiles) {
            if (groundTiles.count(t)) continue;
            auto gt = std::make_unique<CityGroundTile>();
            std::string why;
            // buildCollision = collisionEnabled (street-collision-0x2237-
            // mopp follow-up task, 2026-10-11): --collision now also wires
            // real `0x2237` ground-pavement collision triangles, matching
            // the prop-level `collisionEnabled` gate above.
            if (loadCityGroundTile(t.first, t.second, archives, device.device(), *gt, why, collisionEnabled)) {
                std::printf("[ground] frame %d LOAD  tile %02d%02d: %s, %zu range(s), %u triangles\n", frame,
                            t.first, t.second, gt->cznName.c_str(), gt->rangesRendered, gt->triangles);
            } else {
                std::printf("[ground] frame %d tile %02d%02d: SKIPPED (no real ground rendered): %s\n", frame,
                            t.first, t.second, why.c_str());
            }
            groundTiles[t] = std::move(gt);
        }
        std::fflush(stdout);
    };
    return -1;
}

}  // namespace sr3viewer
