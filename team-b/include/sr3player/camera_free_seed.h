#pragma once

// sr3player/camera_free_seed.h - seeds the follow camera (camera.h) from the
// real camera_free.xtbl through this project's EXISTING reader
// (sr3tables_environment::ParseCameraFree, spec-tables-environment.md
// Sec12.2 / Sec17.5). Header-only on purpose, like movement_tweak_table.h:
// sr3player itself stays free of the sr3xtbl/sr3tables_environment
// dependency; a caller that includes this header links sr3tables_environment.
//
// What the loader does that this reproduces (spec-tables-environment.md
// Sec12.2, CONFIRMED - disassembly 2026-10-01): each submode row is matched by
// name (case-insensitive) against the 61-name table; an unknown name is
// skipped; a row whose Aspect is "widescreen" is skipped when the display
// flag 0x01493661 is clear and a "standard" row when it is set (any other
// text passes); a later row with the same name overwrites the slot (last
// wins). min/max/default_elevation arrive in RADIANS (the reader already
// converts). z_dist/y_dist go to 0x0056ad50 (spec-player-runtime.md Sec4.4:
// base elevation +0x24 and follow distance +0x28).
//
// ABSENT VALUES. The "always write" accessor stores an UNSPECIFIED value
// when its element is absent (sr3xtbl/xtbl.h); shipped tables always supply
// them. So a scalar that is absent is NOT seeded (stays OPEN), and a submode
// row missing any always-write field the camera reads leaves its slot OPEN
// (counted in `submodesIncomplete`). default_elevation and
// override_exit_blend_time are write-if-present: absent = flag clear.

#include <cctype>
#include <string>
#include <string_view>

#include "sr3player/camera.h"
#include "sr3tables_environment/camera_free.h"

namespace sr3player {

struct CameraFreeSeedReport {
    int scalarsSeeded = 0;
    int submodesSeeded = 0;       // row applications (a later duplicate counts again)
    int submodesIncomplete = 0;   // rows skipped for a missing always-write field
    int submodesUnknownName = 0;
    int submodesAspectFiltered = 0;
};

namespace detail_camera_seed {
inline bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
inline void Seed(OpenValue<double>& dst, const sr3xtbl::Always<float>& src, int& n) {
    if (!src.present) return;
    dst.set(static_cast<double>(src.value));
    ++n;
}
inline void SeedDamp(DampeningPair& dst, const sr3tables_environment::CameraFreePanningGroup::Dampening& src, int& n) {
    Seed(dst.h, src.horiz, n);
    Seed(dst.v, src.vert, n);
}
}  // namespace detail_camera_seed

// `widescreen` = the display flag 0x01493661 (a runtime setting the caller owns).
inline CameraFreeSeedReport SeedCameraFromCameraFree(CameraSystem& cam, const sr3tables_environment::CameraFree& cf,
                                                     bool widescreen) {
    using namespace detail_camera_seed;
    CameraFreeSeedReport rep;
    CameraFreeConstants& c = cam.constants;
    const auto& p = cf.panning;
    int& n = rep.scalarsSeeded;
    Seed(c.fastPanH, p.fastPanHorizontalMultiplier, n);
    Seed(c.fastPanV, p.fastPanVerticalMultiplier, n);
    Seed(c.slowPanH, p.slowPanHorizontalMultiplier, n);
    Seed(c.slowPanV, p.slowPanVerticalMultiplier, n);
    Seed(c.pegAccelHMin, p.pegAccelPanHorizontalMin, n);
    Seed(c.pegAccelHMax, p.pegAccelPanHorizontalMax, n);
    Seed(c.pegAccelVMin, p.pegAccelPanVerticalMin, n);
    Seed(c.pegAccelVMax, p.pegAccelPanVerticalMax, n);
    Seed(c.fastPanInputThreshold, p.fastPanInputThreshold, n);
    Seed(c.hpanThreshold, p.hpanThreshold, n);
    Seed(c.vpanThreshold, p.vpanThreshold, n);
    // Pad set, then the "_mouse" copies (Sec4.6 step 2). The interior pair is
    // stored v-before-h in memory; the reader already names each one.
    SeedDamp(c.pad.fineAim, p.fineAimDampening, n);
    SeedDamp(c.pad.heavyWeapon, p.heavyWeaponDampening, n);
    SeedDamp(c.pad.humanShield, p.humanShieldDampening, n);
    Seed(c.pad.interior.h, p.interiorHDampening, n);
    Seed(c.pad.interior.v, p.interiorVDampening, n);
    Seed(c.pad.skydive.h, p.skydiveHDampening, n);
    Seed(c.pad.skydive.v, p.skydiveVDampening, n);
    Seed(c.pad.freefallH, p.freefallHDampening, n);
    Seed(c.pad.parachuteH, p.parachuteHDampening, n);
    Seed(c.pad.helicopterH, p.helicopterHDampening, n);
    SeedDamp(c.mouse.fineAim, p.fineAimDampeningMouse, n);
    SeedDamp(c.mouse.heavyWeapon, p.heavyWeaponDampeningMouse, n);
    SeedDamp(c.mouse.humanShield, p.humanShieldDampeningMouse, n);
    Seed(c.mouse.interior.h, p.interiorHDampeningMouse, n);
    Seed(c.mouse.interior.v, p.interiorVDampeningMouse, n);
    Seed(c.mouse.skydive.h, p.skydiveHDampeningMouse, n);
    Seed(c.mouse.skydive.v, p.skydiveVDampeningMouse, n);
    Seed(c.mouse.freefallH, p.freefallHDampeningMouse, n);
    Seed(c.mouse.parachuteH, p.parachuteHDampeningMouse, n);
    Seed(c.mouse.helicopterH, p.helicopterHDampeningMouse, n);
    Seed(c.defaultSwingRate, cf.followAggression.defaultSwingRate, n);
    Seed(c.exteriorInteriorBlendTime, cf.miscellany.exteriorInteriorBlendTime, n);
    Seed(c.ragdollBlendTime, cf.miscellany.ragdollBlendTime, n);

    for (const auto& row : cf.submodes) {
        int index = -1;
        if (row.name) {
            for (size_t i = 0; i < sr3tables_environment::kCameraFreeSubmodeNames.size(); ++i)
                if (IEquals(*row.name, sr3tables_environment::kCameraFreeSubmodeNames[i])) {
                    index = static_cast<int>(i);
                    break;
                }
        }
        if (index < 0) {
            ++rep.submodesUnknownName;
            continue;
        }
        if (row.aspect) {
            if ((IEquals(*row.aspect, "widescreen") && !widescreen) || (IEquals(*row.aspect, "standard") && widescreen)) {
                ++rep.submodesAspectFiltered;
                continue;
            }
        }
        if (!row.lookatOffset.complete() || !row.zDist.present || !row.yDist.present || !row.minElevation.present ||
            !row.maxElevation.present || !row.baseFov.present || !row.blendTime.present || !row.xShift.present) {
            ++rep.submodesIncomplete;
            continue;
        }
        SubmodeRecord r;
        const auto v = row.lookatOffset.value();
        r.lookatOffset = Vec3{v.x, v.y, v.z};
        r.minElevation = row.minElevation.value;
        r.maxElevation = row.maxElevation.value;
        r.hasDefaultElevation = row.defaultElevation.has_value();
        r.defaultElevation = row.defaultElevation ? static_cast<double>(*row.defaultElevation) : 0.0;
        r.baseFov = row.baseFov.value;
        r.blendTime = row.blendTime.value;
        r.overrideExitBlendTime = row.overrideExitBlendTime.value_or(false);
        r.xShift = row.xShift.value;
        SetPolarFromAuthored(r, row.zDist.value, row.yDist.value);
        cam.submodes[static_cast<size_t>(index)] = r;
        ++rep.submodesSeeded;
    }
    return rep;
}

}  // namespace sr3player
