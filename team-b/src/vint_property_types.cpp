// Vint element types and property-descriptor tables - see
// include/sr3luahost/vint_property_types.h for the citations and for what
// each row's code source (Spec / FieldType / Open) means.
#include "sr3luahost/vint_property_types.h"

#include <cctype>
#include <cstring>

#include "sr3save/save_crc.h"

namespace sr3luahost {

namespace {

constexpr VintCodeSource S = VintCodeSource::Spec;
constexpr VintCodeSource F = VintCodeSource::FieldType;
constexpr VintCodeSource O = VintCodeSource::Open;

VintPropertyDescriptor row(const char* name, uint8_t code, VintCodeSource src) {
    return VintPropertyDescriptor{name, sr3save::nameHash(name), code, src};
}
VintPropertyDescriptor open(const char* name) { return row(name, kVintCodeOpen, O); }

std::vector<VintElementTypeInfo> buildTypes() {
    std::vector<VintElementTypeInfo> t;
    // element (Sec9.2, 17 rows). Field types: spec-vint-render.md Sec1
    // (tint 3 floats, anchor/offset/unscaled_size/scale 2 floats, rotation/
    // alpha float, render_mode/auto_offset dword enum - 0xd per Sec9.2a,
    // mouse_depth/depth int - depth signed per Sec6.2, mask/background/
    // visible bool byte); screen_* are 2-value results (Sec7.2).
    t.push_back({"element", nullptr, {
        row("render_mode", kVintCodeEnum, S), row("visible", kVintCodeBool, F), row("mask", kVintCodeBool, F),
        row("offset", kVintCodeVec2, F), row("anchor", kVintCodeVec2, F), row("tint", kVintCodeVec3, F),
        row("alpha", kVintCodeFloat, F), row("depth", kVintCodeSignedInt, F),
        row("mouse_depth", kVintCodeSignedInt, F), row("screen_size", kVintCodeVec2, F),
        row("screen_nw", kVintCodeVec2, F), row("screen_se", kVintCodeVec2, F), row("rotation", kVintCodeFloat, F),
        row("scale", kVintCodeVec2, F), row("auto_offset", kVintCodeEnum, S), row("unscaled_size", kVintCodeVec2, F),
        row("background", kVintCodeBool, F)}});
    // tween (16 rows): 5 enums (Sec9.2a), start/end_value variant (Sec9.2/
    // Sec20.1), end_event callback (Sec20.1 case 8, vint_lib.lua:1172).
    t.push_back({"tween", nullptr, {
        open("target_handle"), open("target_name"), open("target_property"), row("state", kVintCodeEnum, S),
        open("start_time"), open("duration"), row("start_value", kVintCodeVariant, S),
        row("end_value", kVintCodeVariant, S), row("loop_mode", kVintCodeEnum, S), row("algorithm", kVintCodeEnum, S),
        open("max_loops"), open("start_event"), row("end_event", kVintCodeCallback, S), open("per_frame_event"),
        row("start_value_type", kVintCodeEnum, S), row("end_value_type", kVintCodeEnum, S)}});
    t.push_back({"animation", nullptr, {open("start_time"), open("is_paused"), open("target_handle")}});
    // bitmap (5): vint-doc Sec8 item 7 - image 0xa, source_nw/se vec2 (7),
    // custom_source_coords bool.
    t.push_back({"bitmap", "element", {
        row("custom_source_coords", kVintCodeBool, S), row("image", kVintCodeImageName, S), open("image_crc"),
        row("source_nw", kVintCodeVec2, S), row("source_se", kVintCodeVec2, S)}});
    t.push_back({"gradient", "bitmap", {
        open("gradient_nw"), open("gradient_ne"), open("gradient_sw"), open("gradient_se"),
        open("alpha_nw"), open("alpha_ne"), open("alpha_sw"), open("alpha_se")}});
    // text (21): insert_values 0xe (Sec9.2), horz_align/force_case 0xd
    // (Sec9.2a); screen_size/background re-list element's own fields
    // (spec-vint-render.md Sec1: one field layout across the element
    // classes; text overrides only the screen_size getter body).
    t.push_back({"text", "element", {
        open("font"), open("text_tag"), open("text_tag_crc"), open("text_scale"), open("word_wrap"),
        open("wrap_width"), row("horz_align", kVintCodeEnum, S), row("force_case", kVintCodeEnum, S),
        open("leading"), open("kerning"), row("insert_values", kVintCodeVariant, S),
        row("screen_size", kVintCodeVec2, F), open("line_frame_enable"), open("line_frame_w"), open("line_frame_m"),
        open("line_frame_e"), open("shadow_enabled"), open("shadow_offset"), open("shadow_tint"),
        open("shadow_alpha"), row("background", kVintCodeBool, F)}});
    // group (4): "all re-listed from element, identical contents" (Sec9.2).
    t.push_back({"group", "element", {
        row("screen_size", kVintCodeVec2, F), row("screen_se", kVintCodeVec2, F),
        row("screen_nw", kVintCodeVec2, F), row("offset", kVintCodeVec2, F)}});
    t.push_back({"document", "element", {row("document_name", kVintCodeDocumentName, S)}});
    t.push_back({"point", "element", {row("screen_size", kVintCodeVec2, F)}});
    t.push_back({"clip", "element", {open("clip_size"), open("clip_enabled")}});
    // bitmap_circle (7): image is "the identical property" to bitmap's
    // (vint-doc Sec8 item 7); screen_size re-lists element's.
    t.push_back({"bitmap_circle", "element", {
        row("image", kVintCodeImageName, S), row("screen_size", kVintCodeVec2, F), open("source_nw"),
        open("source_se"), open("start_angle"), open("end_angle"), open("num_wedges")}});
    // zoom/map_mode (2): spec-hud-runtime.md Sec6 (2026-10-08 pass) reads
    // both real get/set bodies directly - zoom get=+0xc8 (0x00e34420, reads
    // this+0xa0 as a float)/set=+0xcc (0x00e34460, validates and writes
    // this+0xa0); map_mode get=+0xd0/set=+0xd4, a switch over the 0xd
    // enumeration below. CORRECTED 2026-10-08 (minimap live-transform task):
    // zoom was previously left Open (code not stated in spec-lua-bindings.md
    // Sec9.2's own table, which lists property NAMES only); spec-hud-
    // runtime.md Sec6 now states the code directly ("float"), so this is a
    // Spec row like map_mode, not a FieldType deduction.
    t.push_back({"sr2_map", "element", {row("zoom", kVintCodeFloat, S), row("map_mode", kVintCodeEnum, S)}});
    t.push_back({"video", "element", {
        open("vid_id_handle"), open("vid_name"), open("is_paused"), open("is_stopped"), open("is_looped"),
        open("frame"), open("end_event"), open("frame_event"), open("frame_event_num")}});
    return t;
}

bool iequals(const char* a, const std::string& b) {
    size_t n = std::strlen(a);
    if (n != b.size()) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

struct EnumEntry { const char* name; int32_t value; };
struct EnumBinding {
    const char* type;
    const char* property;
    bool complete;
    std::vector<EnumEntry> entries;
};

// Sec9.2a, all 10 tables as listed there.
const std::vector<EnumBinding>& enumBindings() {
    static const std::vector<EnumBinding> b = {
        {"element", "auto_offset", true,
         {{"none", 0}, {"c", 1}, {"w", 2}, {"n", 3}, {"e", 4}, {"s", 5}, {"nw", 6}, {"ne", 7}, {"se", 8}, {"sw", 9}}},
        {"element", "render_mode", true,
         {{"default", 0}, {"filter", 1}, {"additive", 2}, {"additive_alpha", 3}, {"subtractive", 4}, {"multiply", 5}}},
        {"tween", "loop_mode", true, {{"once", 0}, {"cycle", 1}, {"bounce", 2}}},
        // 25 entries; only the two named in Sec9.2a are known here.
        {"tween", "algorithm", false, {{"linear", 0}, {"ease_in_out_bounce", 24}}},
        {"tween", "state", true,
         {{"idle", 0}, {"running", 1}, {"paused", 2}, {"disabled", 3}, {"waiting", 0}, {"finished", 4}}},
        {"tween", "start_value_type", true, {{"absolute", 0}, {"stage", 1}, {"dynamic", 2}}},
        {"tween", "end_value_type", true, {{"absolute", 0}, {"stage", 1}, {"dynamic", 2}}},
        {"text", "horz_align", true, {{"left", 0}, {"center", 1}, {"right", 2}}},
        {"text", "force_case", true, {{"none", 0}, {"upper", 1}, {"lower", 2}}},
        {"sr2_map", "map_mode", true, {{"pause_map", 0}, {"minimap", 1}}},
    };
    return b;
}

} // namespace

const std::vector<VintElementTypeInfo>& vintElementTypes() {
    static const std::vector<VintElementTypeInfo> types = buildTypes();
    return types;
}

const VintElementTypeInfo* findVintElementType(const std::string& typeName) {
    for (const auto& t : vintElementTypes()) {
        if (typeName == t.name) return &t;
    }
    return nullptr;
}

bool vintTypeIsA(const std::string& typeName, const char* ancestor) {
    const VintElementTypeInfo* t = findVintElementType(typeName);
    while (t) {
        if (std::strcmp(t->name, ancestor) == 0) return true;
        t = t->parent ? findVintElementType(t->parent) : nullptr;
    }
    return false;
}

std::optional<VintDescriptorHit> findVintPropertyDescriptor(const std::string& typeName, uint32_t nameHash) {
    const VintElementTypeInfo* t = findVintElementType(typeName);
    while (t) {
        for (const auto& r : t->rows) {
            if (r.nameHash == nameHash) return VintDescriptorHit{&r, t};
        }
        t = t->parent ? findVintElementType(t->parent) : nullptr;
    }
    return std::nullopt;
}

int vintCodeArity(uint8_t code) {
    switch (code) {
        case kVintCodeSignedInt: case kVintCodeUnsignedInt: case kVintCodeFloat: case kVintCodeString:
        case kVintCodeBool: case kVintCodeCallback: case kVintCodeDocumentName: case kVintCodeImageName:
        case kVintCodeFontName: case kVintCodeEnum:
            return 1;
        case kVintCodeVec2: return 2;
        case kVintCodeVec3: return 3;
        default: return 0;
    }
}

VintEnumLookup lookupVintEnum(const std::string& typeName, const std::string& propertyName,
                              const std::string& valueName, int32_t* valueOut) {
    bool sawIncomplete = false;
    bool sawBinding = false;
    for (const auto& b : enumBindings()) {
        if (!iequals(b.property, propertyName)) continue;
        // Two-directional ancestor-or-self type match (Sec9.2a).
        if (!vintTypeIsA(typeName, b.type) && !vintTypeIsA(b.type, typeName.c_str())) continue;
        sawBinding = true;
        for (const auto& e : b.entries) {
            if (iequals(e.name, valueName)) {
                if (valueOut) *valueOut = e.value;
                return VintEnumLookup::Found;
            }
        }
        if (!b.complete) sawIncomplete = true;
    }
    if (sawIncomplete || !sawBinding) return VintEnumLookup::Open;
    return VintEnumLookup::Miss;
}

} // namespace sr3luahost
