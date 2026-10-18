// lua_engine_state_vint.cpp
// EngineState member bodies: Vint documents, data items, typed Vint properties,
// geometry queries and the property get/set path, with their file-local helpers.
//
// Moved verbatim out of lua_engine_state.cpp (Team C refactor, 2026-10-17); see
// engine_state.h for every member's own documentation.

#include "sr3luahost/engine_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

#include "sr3luahost/save_game_bridge.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"

namespace sr3luahost {

namespace {

// One stored file property as the Lua values vint_get_property pushes (see
// EngineState::loadVintDocument's doc comment for the CHOSEN mapping).
std::vector<VintTaggedValue> fileValueToTagged(const sr3vintdoc::Property& p, const sr3vintdoc::Document& doc) {
    auto number = [](double d) {
        VintTaggedValue v;
        v.kind = VintTaggedValue::Kind::Number;
        v.number = d;
        return v;
    };
    std::vector<VintTaggedValue> out;
    switch (p.tag) {
        case 1: out.push_back(number(static_cast<double>(static_cast<int32_t>(p.rawU32())))); break; // signed (67455c3)
        case 2: out.push_back(number(static_cast<double>(p.rawU32()))); break;                       // unsigned (67455c3)
        case 3: out.push_back(number(p.f32(0))); break;
        case 4: {
            uint32_t idx = p.rawU32();
            if (idx < doc.strings.strings.size()) {
                VintTaggedValue v;
                v.kind = VintTaggedValue::Kind::String;
                v.text = doc.strings.strings[idx];
                out.push_back(v);
            }
            // An out-of-range index would resolve to null in the loader
            // (0x00e1ed90); no shipped file has one - nothing is stored.
            break;
        }
        case 5: {
            VintTaggedValue v;
            v.kind = VintTaggedValue::Kind::Boolean;
            v.boolean = p.boolean();
            out.push_back(v);
            break;
        }
        case 6: for (size_t i = 0; i < 3; ++i) out.push_back(number(p.f32(i))); break;
        case 7: for (size_t i = 0; i < 2; ++i) out.push_back(number(p.f32(i))); break;
        default: break; // parseDocument never yields another tag
    }
    return out;
}

// --- typed Vint property helpers (2026-10-03) ------------------------------

VintTaggedValue numberValue(double d) {
    VintTaggedValue v;
    v.kind = VintTaggedValue::Kind::Number;
    v.number = d;
    return v;
}
VintTaggedValue boolValue(bool b) {
    VintTaggedValue v;
    v.kind = VintTaggedValue::Kind::Boolean;
    v.boolean = b;
    return v;
}

// 32-bit float storage (spec-vint-render.md Sec1: every float field is a
// 4-byte float; Sec5.2: products are rounded back to float on store).
double toF32(double d) { return static_cast<double>(static_cast<float>(d)); }

bool isWholeNumber(double d) { return std::isfinite(d) && std::trunc(d) == d; }

bool ieq(const std::string& a, const char* b) {
    size_t n = std::char_traits<char>::length(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

const char* kindName(VintTaggedValue::Kind k) {
    switch (k) {
        case VintTaggedValue::Kind::Number: return "number";
        case VintTaggedValue::Kind::Boolean: return "boolean";
        case VintTaggedValue::Kind::String: return "string";
        default: return "nil/other";
    }
}

enum class ParseOutcome { Typed, Raw, Refuse };
struct ParseResult {
    ParseOutcome outcome = ParseOutcome::Raw;
    std::vector<VintTaggedValue> values;
    std::string why; // for Refuse
};

// vint_set_property's per-code value extraction (Sec20.1) for a known code -
// see EngineState::setVintProperty's header doc comment for every rule.
ParseResult parseVintValuesByCode(uint8_t code, const std::vector<VintTaggedValue>& in,
                                  const std::string& typeName, const char* propName) {
    ParseResult r;
    const int n = vintCodeArity(code);
    if (n == 0) return r; // OPEN / variant: raw
    auto refuse = [&](const std::string& why) {
        r.outcome = ParseOutcome::Refuse;
        r.values.clear();
        r.why = why;
        return r;
    };
    if (static_cast<int>(in.size()) < n) {
        return refuse(std::to_string(in.size()) + " trailing Lua value(s) for a code that takes " + std::to_string(n));
    }
    auto needKind = [&](int i, VintTaggedValue::Kind k) { return in[i].kind == k; };
    switch (code) {
        case kVintCodeSignedInt:
        case kVintCodeUnsignedInt: {
            if (!needKind(0, VintTaggedValue::Kind::Number)) return refuse(std::string("a ") + kindName(in[0].kind) + " value");
            double d = in[0].number;
            if (!isWholeNumber(d)) return refuse("a non-whole number (the double-to-integer rounding is not specified)");
            if (code == kVintCodeUnsignedInt && d < 0) return refuse("a negative number for an unsigned code");
            if (std::fabs(d) >= 18446744073709551616.0) return refuse("a number outside the 64-bit integer range");
            r.values.push_back(numberValue(d));
            break;
        }
        case kVintCodeFloat:
        case kVintCodeVec2:
        case kVintCodeVec3:
            for (int i = 0; i < n; ++i) {
                if (!needKind(i, VintTaggedValue::Kind::Number)) {
                    return refuse(std::string("a ") + kindName(in[i].kind) + " value at component " + std::to_string(i + 1));
                }
                r.values.push_back(numberValue(toF32(in[i].number)));
            }
            break;
        case kVintCodeBool:
            if (!needKind(0, VintTaggedValue::Kind::Boolean)) return refuse(std::string("a ") + kindName(in[0].kind) + " value");
            r.values.push_back(in[0]);
            break;
        case kVintCodeString:
        case kVintCodeCallback:
        case kVintCodeDocumentName:
        case kVintCodeImageName:
        case kVintCodeFontName:
            if (!needKind(0, VintTaggedValue::Kind::String)) return refuse(std::string("a ") + kindName(in[0].kind) + " value");
            r.values.push_back(in[0]);
            break;
        case kVintCodeEnum:
            if (in[0].kind == VintTaggedValue::Kind::Number) {
                if (!isWholeNumber(in[0].number)) return refuse("a non-whole number for an enumeration");
                r.values.push_back(numberValue(in[0].number));
                break;
            }
            if (in[0].kind == VintTaggedValue::Kind::String) {
                int32_t v = 0;
                if (lookupVintEnum(typeName, propName, in[0].text, &v) == VintEnumLookup::Found) {
                    r.values.push_back(numberValue(v));
                    break;
                }
                r.outcome = ParseOutcome::Raw; // unresolvable name: stored as given
                return r;
            }
            return refuse(std::string("a ") + kindName(in[0].kind) + " value for an enumeration");
        default:
            return r; // raw
    }
    r.outcome = ParseOutcome::Typed;
    return r;
}

// A file record's stored form for a row of `code` (see loadVintDocument's
// header doc comment). typedAs 0 = keep raw.
uint8_t fileRecordTypedAs(uint8_t code, const sr3vintdoc::Property& p, const std::string& typeName,
                          const char* propName, std::vector<VintTaggedValue>& values) {
    if (code == kVintCodeOpen) return 0;
    if (code >= kVintCodeSignedInt && code <= kVintCodeVec2) return p.tag == code ? code : 0;
    if ((code == kVintCodeDocumentName || code == kVintCodeImageName || code == kVintCodeFontName) && p.tag == 4) return code;
    if (code == kVintCodeEnum) {
        if (p.tag == 1 || p.tag == 2) return code;
        if (p.tag == 4 && !values.empty()) {
            int32_t v = 0;
            if (lookupVintEnum(typeName, propName, values[0].text, &v) == VintEnumLookup::Found) {
                values = {numberValue(v)};
                return code;
            }
        }
    }
    return 0;
}

bool isQueryGeometryRow(const char* name) {
    return std::strcmp(name, "screen_size") == 0 || std::strcmp(name, "screen_nw") == 0 ||
           std::strcmp(name, "screen_se") == 0;
}
// Fail-stub setters in every element class (spec-vint-render.md Sec7.3).
bool isFailStubRow(const char* name) {
    return std::strcmp(name, "screen_nw") == 0 || std::strcmp(name, "screen_se") == 0 ||
           std::strcmp(name, "unscaled_size") == 0;
}
// Types whose unscaled_size is written by their own size path
// (spec-vint-render.md Sec5.4: bitmap/text/bitmap_circle; gradient derives
// from bitmap).
bool sizeSetByTypePath(const std::string& typeName) {
    return vintTypeIsA(typeName, "bitmap") || vintTypeIsA(typeName, "text") || vintTypeIsA(typeName, "bitmap_circle");
}

constexpr const char* kRenderSpec = "spec-vint-render.md Sec7";
constexpr const char* kSetSpec = "spec-lua-bindings.md Sec20.1";
constexpr const char* kGetSpec = "spec-lua-bindings.md Sec20.2";
constexpr int kMaxVintTreeDepth = 1024; // host guard against a malformed (cyclic) tree

} // namespace

uint32_t EngineState::loadVintDocument(const std::string& docName, const sr3vintdoc::Document& doc,
                                       const std::string& activeResolution,
                                       const VintImageSizeResolver& imageSize) {
    LoadedVintDocument ld;
    ld.name = docName;
    ld.handle = nextVintDocumentHandle_++;
    if (const std::string* s = doc.metadataValue("lua_script_file")) ld.luaScriptFile = *s;

    // Pre-order, handle issued before the children are visited, so handles
    // ascend in file order (findVdoObject's tie-break relies on it).
    std::function<uint32_t(const sr3vintdoc::ElementNode&, uint32_t)> add =
        [&](const sr3vintdoc::ElementNode& n, uint32_t parent) -> uint32_t {
        uint32_t h = nextVdoObjectHandle_++;
        {
            VdoObject obj;
            obj.name = n.name;
            obj.nameHash = sr3save::nameHash(n.name);
            obj.parentHandle = parent;
            obj.docHandle = ld.handle;
            obj.typeName = n.type;
            obj.fromDocument = true;
            vdoObjects_[h] = obj;
        }
        ++ld.objectCount;
        const bool knownType = findVintElementType(n.type) != nullptr;
        if (knownType) seedVintElementDefaults(h, n.type);
        for (const auto& p : n.effectiveProperties(activeResolution)) {
            std::vector<VintTaggedValue> values = fileValueToTagged(p, doc);
            if (values.empty()) continue;
            VintStoredProperty sp;
            if (knownType) {
                auto hit = findVintPropertyDescriptor(n.type, p.nameHash);
                if (!hit) { ++vintPropertyStats_.docRecordsSkippedNoDescriptor; continue; } // CONFIRMED skip
                if (isFailStubRow(hit->row->name) || std::strcmp(hit->row->name, "screen_size") == 0) {
                    // Fail-stub setter (Sec7.3); a document-carried screen_size
                    // (flag-0, never seen in shipped files) is not run through
                    // the real setter at load time either - counted here.
                    ++vintPropertyStats_.docRecordsSkippedReadOnly;
                    continue;
                }
                sp.typedAs = fileRecordTypedAs(hit->row->code, p, n.type, hit->row->name, values);
                ++(sp.typedAs ? vintPropertyStats_.docRecordsTyped : vintPropertyStats_.docRecordsRaw);
            } else {
                ++vintPropertyStats_.docRecordsRaw;
            }
            // spec-vint-render.md Sec12.1: the bitmap image setter's side
            // effects, run where the real loader runs that setter (see the
            // header doc comment). Only with a caller-supplied resolver.
            std::string imageName;
            const bool isBitmapImage = imageSize && knownType && vintTypeIsA(n.type, "bitmap") &&
                                       p.nameHash == sr3save::nameHash("image") &&
                                       values[0].kind == VintTaggedValue::Kind::String;
            if (isBitmapImage) imageName = values[0].text;
            sp.values = std::move(values);
            vintProperties_[h][p.nameHash] = std::move(sp);
            ++ld.propertyCount;
            if (isBitmapImage) {
                std::optional<std::pair<double, double>> wh = imageSize(imageName);
                if (!wh) {
                    ++vintPropertyStats_.docImageUnresolved;
                } else {
                    const double w = toF32(wh->first), ht = toF32(wh->second);
                    const VintStoredProperty* cs = findVintStoredProperty(h, "custom_source_coords");
                    const bool custom = cs && !cs->values.empty() && cs->values[0].kind == VintTaggedValue::Kind::Boolean &&
                                        cs->values[0].boolean; // absent = clear (CHOSEN, see header)
                    if (!custom) {
                        vintProperties_[h][sr3save::nameHash("source_nw")] =
                            VintStoredProperty{{numberValue(0), numberValue(0)}, kVintCodeVec2};
                        vintProperties_[h][sr3save::nameHash("source_se")] =
                            VintStoredProperty{{numberValue(w), numberValue(ht)}, kVintCodeVec2};
                        ++vintPropertyStats_.docImageSourceCoordsReset;
                    }
                    vintProperties_[h][sr3save::nameHash("unscaled_size")] =
                        VintStoredProperty{{numberValue(w), numberValue(ht)}, kVintCodeVec2};
                    ++vintPropertyStats_.docImageSizesApplied;
                    vintApplyAutoOffset(h);
                }
            }
        }
        uint32_t prev = 0;
        for (const auto& c : n.children) {
            uint32_t ch = add(c, h);
            if (prev == 0) vdoObjects_[h].firstChildHandle = ch;
            else vdoObjects_[prev].nextSiblingHandle = ch;
            prev = ch;
        }
        return h;
    };
    auto addList = [&](const std::vector<sr3vintdoc::ElementNode>& list, std::vector<uint32_t>& handles) {
        uint32_t prev = 0;
        for (const auto& n : list) {
            uint32_t h = add(n, 0);
            if (prev != 0) vdoObjects_[prev].nextSiblingHandle = h;
            prev = h;
            handles.push_back(h);
        }
    };
    addList(doc.elements, ld.elementHandles);
    addList(doc.animations, ld.animationHandles);
    uint32_t handle = ld.handle;
    loadedVintDocuments_[handle] = std::move(ld);
    return handle;
}

const LoadedVintDocument* EngineState::loadedVintDocument(uint32_t docHandle) const {
    auto it = loadedVintDocuments_.find(docHandle);
    return it == loadedVintDocuments_.end() ? nullptr : &it->second;
}

double EngineState::vintGetTimeIndex(bool hasExplicitNonZeroDoc, uint32_t explicitDoc) const {
    uint32_t doc = hasExplicitNonZeroDoc ? explicitDoc : currentDefaultDocHandle_.get(); // OPEN propagates on the fallback path only
    return vintTimeIndexByDoc_.get(std::to_string(doc)); // OPEN propagates - see this function's own header doc comment on why "resolution failed" and "not yet known" collapse to the same refusal here
}

uint32_t EngineState::registerVintDataItemForTesting(std::vector<VintTaggedValue> fields) {
    if (fields.size() > 32) fields.resize(32); // CONFIRMED real cap - truncate rather than guess what more would mean
    uint32_t handle = nextVintDataItemHandle_++;
    vintDataItems_[handle] = VintDataItem{std::move(fields)};
    return handle;
}

const std::vector<VintTaggedValue>* EngineState::findVintDataItemFields(uint32_t handle) const {
    auto it = vintDataItems_.find(handle);
    if (it == vintDataItems_.end()) return nullptr; // CONFIRMED (Sec19.2): bad handle -> zero Lua values
    return &it->second.fields;
}

// --- typed Vint properties (2026-10-03) - see engine_state.h's doc comments
// on setVintProperty / getVintProperty / vintQueryBox for every rule. ---

void EngineState::seedVintElementDefaults(uint32_t handle, const std::string& typeName) {
    if (!vintIsElementFamily(typeName)) return;
    // spec-vint-render.md Sec8, the element initializer 0x00e26900.
    auto put = [&](const char* name, uint8_t code, std::vector<VintTaggedValue> v) {
        vintProperties_[handle][sr3save::nameHash(name)] = VintStoredProperty{std::move(v), code};
    };
    put("anchor", kVintCodeVec2, {numberValue(0), numberValue(0)});
    put("offset", kVintCodeVec2, {numberValue(0), numberValue(0)});
    if (!sizeSetByTypePath(typeName)) put("unscaled_size", kVintCodeVec2, {numberValue(1), numberValue(1)});
    put("rotation", kVintCodeFloat, {numberValue(0)});
    put("auto_offset", kVintCodeEnum, {numberValue(0)});
    put("mask", kVintCodeBool, {boolValue(false)});
    put("depth", kVintCodeSignedInt, {numberValue(0)});
    put("visible", kVintCodeBool, {boolValue(true)});
    put("mouse_depth", kVintCodeSignedInt, {numberValue(2147483647.0)}); // 0x7fffffff: "follow depth"
    put("tint", kVintCodeVec3, {numberValue(1), numberValue(1), numberValue(1)});
    put("render_mode", kVintCodeEnum, {numberValue(0)});
    put("alpha", kVintCodeFloat, {numberValue(1)});
    put("scale", kVintCodeVec2, {numberValue(1), numberValue(1)});
    put("background", kVintCodeBool, {boolValue(false)});
    ++vintPropertyStats_.docObjectsSeeded;
}

uint32_t EngineState::registerTypedVdoObjectForTesting(const std::string& name, const std::string& typeName,
                                                       uint32_t parentHandle, uint32_t docHandle) {
    uint32_t handle = registerVdoObjectForTesting(name, parentHandle, docHandle);
    vdoObjects_[handle].typeName = typeName;
    if (parentHandle != 0) {
        auto pit = vdoObjects_.find(parentHandle);
        if (pit != vdoObjects_.end()) {
            if (pit->second.firstChildHandle == 0) {
                pit->second.firstChildHandle = handle;
            } else {
                uint32_t c = pit->second.firstChildHandle;
                while (vdoObjects_[c].nextSiblingHandle != 0) c = vdoObjects_[c].nextSiblingHandle;
                vdoObjects_[c].nextSiblingHandle = handle;
            }
        }
    }
    if (findVintElementType(typeName)) seedVintElementDefaults(handle, typeName);
    return handle;
}

void EngineState::setVintUnscaledSizeForTesting(uint32_t handle, double w, double h) {
    if (!vdoObjects_.count(handle)) return;
    vintProperties_[handle][sr3save::nameHash("unscaled_size")] =
        VintStoredProperty{{numberValue(toF32(w)), numberValue(toF32(h))}, kVintCodeVec2};
}

const VintStoredProperty* EngineState::findVintStoredProperty(uint32_t handle, const std::string& propertyName) const {
    auto objIt = vintProperties_.find(handle);
    if (objIt == vintProperties_.end()) return nullptr;
    auto propIt = objIt->second.find(sr3save::nameHash(propertyName));
    if (propIt == objIt->second.end()) return nullptr;
    return &propIt->second;
}

const std::vector<VintTaggedValue>* EngineState::findVintProperty(uint32_t handle, const std::string& propertyName) const {
    const VintStoredProperty* sp = findVintStoredProperty(handle, propertyName);
    return sp ? &sp->values : nullptr;
}

std::vector<double> EngineState::vintNumbers(uint32_t handle, const char* propertyName, size_t count) const {
    const VintStoredProperty* sp = findVintStoredProperty(handle, propertyName);
    if (sp && sp->typedAs != 0 && sp->values.size() == count) {
        std::vector<double> out;
        for (const auto& v : sp->values) {
            if (v.kind != VintTaggedValue::Kind::Number) break;
            out.push_back(v.number);
        }
        if (out.size() == count) return out;
    }
    const auto it = vdoObjects_.find(handle);
    std::string who = it == vdoObjects_.end() ? std::to_string(handle) : (it->second.typeName + " '" + it->second.name + "'");
    throw OpenStateError(std::string("vint ") + propertyName + " of " + who +
                             (sp ? " (stored in a shape this host does not convert)" : " (no value known)"),
                         kRenderSpec);
}

std::vector<double> EngineState::vintUnscaledSize(uint32_t handle) const {
    const VdoObject& obj = vdoObjects_.at(handle);
    if (vintTypeIsA(obj.typeName, "text")) {
        throw OpenStateError("vint text unscaled_size of '" + obj.name + "' (layout refresh hook, vtable +0x24)",
                             "spec-vint-render.md Sec1/Sec11 item 6");
    }
    const VintStoredProperty* sp = findVintStoredProperty(handle, "unscaled_size");
    if (!sp) {
        throw OpenStateError("vint unscaled_size of " + obj.typeName + " '" + obj.name +
                                 "' (set by the type's own size path, not modelled)",
                             "spec-vint-render.md Sec5.4/Sec7.1");
    }
    return vintNumbers(handle, "unscaled_size", 2);
}

EngineState::VintPosM EngineState::vintQueryPosM(uint32_t handle) const {
    // Recursion depth = tree depth; vintQueryBox bounds it.
    const VdoObject& obj = vdoObjects_.at(handle);
    VintPosM p; // pos 0, M identity: the topmost is-an-element ancestor (Sec7.1/Sec7.2)
    int depth = 0;
    // Walk to the parent first (iteratively collecting the chain).
    std::vector<uint32_t> chain{handle};
    uint32_t cur = obj.parentHandle;
    while (cur != 0) {
        auto it = vdoObjects_.find(cur);
        if (it == vdoObjects_.end()) break;
        if (it->second.typeName.empty() || !findVintElementType(it->second.typeName)) {
            throw OpenStateError("vint parent of '" + obj.name + "' has no registered type (is-an-element unknown)",
                                 kRenderSpec);
        }
        if (!vintIsElementFamily(it->second.typeName)) break;
        chain.push_back(cur);
        cur = it->second.parentHandle;
        if (++depth > kMaxVintTreeDepth) throw std::runtime_error("vint object tree deeper than the host guard (cyclic?)");
    }
    for (auto ci = chain.rbegin(); ci != chain.rend(); ++ci) {
        std::vector<double> anchor = vintNumbers(*ci, "anchor", 2);
        double rot = vintNumbers(*ci, "rotation", 1)[0];
        std::vector<double> scale = vintNumbers(*ci, "scale", 2);
        // pos = P.pos + P.M * anchor
        double px = toF32(p.px + p.m[0] * anchor[0] + p.m[1] * anchor[1]);
        double py = toF32(p.py + p.m[2] * anchor[0] + p.m[3] * anchor[1]);
        // M = (P.M * R) * S, each product in double, rounded to float (Sec5.2)
        double c = std::cos(rot), s = std::sin(rot);
        double t0 = toF32(p.m[0] * c + p.m[1] * s), t1 = toF32(p.m[0] * -s + p.m[1] * c);
        double t2 = toF32(p.m[2] * c + p.m[3] * s), t3 = toF32(p.m[2] * -s + p.m[3] * c);
        p.px = px;
        p.py = py;
        p.m[0] = toF32(t0 * scale[0]);
        p.m[1] = toF32(t1 * scale[1]);
        p.m[2] = toF32(t2 * scale[0]);
        p.m[3] = toF32(t3 * scale[1]);
    }
    return p;
}

EngineState::VintQueryTransform EngineState::vintQueryTransform(uint32_t handle) const {
    if (!vdoObjects_.count(handle)) throw OpenStateError("vint object " + std::to_string(handle), kRenderSpec);
    VintPosM pm = vintQueryPosM(handle);
    VintQueryTransform t;
    t.px = pm.px;
    t.py = pm.py;
    for (int i = 0; i < 4; ++i) t.m[i] = pm.m[i];
    return t;
}

void EngineState::vintApplyAutoOffset(uint32_t handle) {
    // spec-vint-render.md Sec5.4: offset from unscaled_size (w,h) via a
    // 9-entry table; none (0) and values above 9 leave offset untouched.
    const VintStoredProperty* ao = findVintStoredProperty(handle, "auto_offset");
    const VintStoredProperty* us = findVintStoredProperty(handle, "unscaled_size");
    if (!ao || ao->typedAs == 0 || ao->values.empty() || ao->values[0].kind != VintTaggedValue::Kind::Number || !us ||
        us->values.size() != 2) {
        ++vintPropertyStats_.docAutoOffsetNotApplied;
        return;
    }
    const double w = us->values[0].number, h = us->values[1].number;
    double ox = 0, oy = 0;
    switch (static_cast<int64_t>(ao->values[0].number)) {
        case 1: ox = -w / 2; oy = -h / 2; break; // c
        case 2: ox = 0; oy = -h / 2; break;      // w
        case 3: ox = -w / 2; oy = 0; break;      // n
        case 4: ox = -w; oy = -h / 2; break;     // e
        case 5: ox = -w / 2; oy = -h; break;     // s
        case 6: ox = 0; oy = 0; break;           // nw
        case 7: ox = -w; oy = 0; break;          // ne
        case 8: ox = -w; oy = -h; break;         // se
        case 9: ox = 0; oy = -h; break;          // sw
        default: ++vintPropertyStats_.docAutoOffsetNotApplied; return; // none / out of range: untouched
    }
    vintProperties_[handle][sr3save::nameHash("offset")] =
        VintStoredProperty{{numberValue(toF32(ox)), numberValue(toF32(oy))}, kVintCodeVec2};
    ++vintPropertyStats_.docAutoOffsetApplied;
}

std::optional<EngineState::VintBox> EngineState::vintQueryBox(uint32_t handle) const {
    struct Walker {
        const EngineState& es;
        std::optional<VintBox> box(uint32_t h, int depth) const {
            if (depth > kMaxVintTreeDepth) throw std::runtime_error("vint object tree deeper than the host guard (cyclic?)");
            auto it = es.vdoObjects_.find(h);
            if (it == es.vdoObjects_.end()) throw OpenStateError("vint object " + std::to_string(h), kRenderSpec);
            const VdoObject& obj = it->second;
            if (!findVintElementType(obj.typeName) || !vintIsElementFamily(obj.typeName)) {
                throw OpenStateError("vint query geometry of '" + obj.name + "' (type '" + obj.typeName +
                                         "' is not a registered element type)",
                                     kRenderSpec);
            }
            if (vintTypeIsA(obj.typeName, "text")) {
                throw OpenStateError("vint text bounds of '" + obj.name + "' (layout re-run, 0x00e330d0/0x00e31c50)",
                                     "spec-vint-render.md Sec7.1/Sec11 item 6");
            }
            std::optional<VintBox> out;
            auto unite = [&](const VintBox& b) {
                if (!out) { out = b; return; }
                out->minX = std::min(out->minX, b.minX);
                out->minY = std::min(out->minY, b.minY);
                out->maxX = std::max(out->maxX, b.maxX);
                out->maxY = std::max(out->maxY, b.maxY);
            };
            const bool isGroup = vintTypeIsA(obj.typeName, "group");
            if (!isGroup) {
                // Corner builder 0x00e280d0 + bounding box 0x00e28540.
                VintPosM pm = es.vintQueryPosM(h);
                std::vector<double> off = es.vintNumbers(h, "offset", 2);
                std::vector<double> sz = es.vintUnscaledSize(h);
                const double qs[4][2] = {{0, 0}, {sz[0], 0}, {0, sz[1]}, {sz[0], sz[1]}};
                for (const auto& q : qs) {
                    double lx = off[0] + q[0], ly = off[1] + q[1];
                    double x = toF32(pm.px + pm.m[0] * lx + pm.m[1] * ly);
                    double y = toF32(pm.py + pm.m[2] * lx + pm.m[3] * ly);
                    unite(VintBox{x, y, x, y});
                }
            }
            // Every is-an-element child's own box, recursively (tween/animation
            // children skipped - not element-family).
            for (uint32_t c = obj.firstChildHandle; c != 0;) {
                auto cit = es.vdoObjects_.find(c);
                if (cit == es.vdoObjects_.end()) break;
                const VdoObject& child = cit->second;
                if (!findVintElementType(child.typeName)) {
                    throw OpenStateError("vint child '" + child.name + "' of '" + obj.name +
                                             "' has no registered type (is-an-element unknown)",
                                         kRenderSpec);
                }
                if (vintIsElementFamily(child.typeName)) {
                    std::optional<VintBox> cb = box(c, depth + 1);
                    if (!cb) {
                        throw OpenStateError("vint childless group '" + child.name + "' collapsed point (position not given)",
                                             "spec-vint-render.md Sec7.1");
                    }
                    unite(*cb);
                }
                c = child.nextSiblingHandle;
            }
            return out; // nullopt only for a childless group
        }
    };
    return Walker{*this}.box(handle, 0);
}

void EngineState::setVintScreenSize(uint32_t handle, double requestedW, double requestedH) {
    const VdoObject& obj = vdoObjects_.at(handle);
    if (vintTypeIsA(obj.typeName, "group") || vintTypeIsA(obj.typeName, "point")) {
        ++vintPropertyStats_.setReadOnlyNoop; // 0x00e27bf0: return false
        return;
    }
    if (vintTypeIsA(obj.typeName, "text")) {
        throw OpenStateError("vint text screen_size setter of '" + obj.name + "' (layout-ready bit)",
                             "spec-vint-render.md Sec7.3");
    }
    // Shared body 0x00e27c70: clamp, read the current box and scale, rescale.
    double rw = std::max(requestedW, 1e-4), rh = std::max(requestedH, 1e-4);
    std::optional<VintBox> b = vintQueryBox(handle); // never nullopt: not a group
    double cw = toF32(b->maxX - b->minX), ch = toF32(b->maxY - b->minY);
    std::vector<double> scale = vintNumbers(handle, "scale", 2);
    if (cw == 0.0 || ch == 0.0) {
        ++vintPropertyStats_.screenSizeDivZeroGuards; // HOST-SAFETY - see engine_state.h
        return;
    }
    vintProperties_[handle][sr3save::nameHash("scale")] =
        VintStoredProperty{{numberValue(toF32(scale[0] * rw / cw)), numberValue(toF32(scale[1] * rh / ch))}, kVintCodeVec2};
    ++vintPropertyStats_.screenSizeSets;
}

std::optional<uint8_t> EngineState::vintTweenBorrowedCode(uint32_t tweenHandle) const {
    const VintStoredProperty* th = findVintStoredProperty(tweenHandle, "target_handle");
    const VintStoredProperty* tp = findVintStoredProperty(tweenHandle, "target_property");
    if (!th || th->values.empty() || th->values[0].kind != VintTaggedValue::Kind::Number) return std::nullopt;
    if (!tp || tp->values.empty() || tp->values[0].kind != VintTaggedValue::Kind::String) return std::nullopt;
    auto tit = vdoObjects_.find(static_cast<uint32_t>(th->values[0].number));
    if (tit == vdoObjects_.end() || !findVintElementType(tit->second.typeName)) return std::nullopt;
    auto hit = findVintPropertyDescriptor(tit->second.typeName, sr3save::nameHash(tp->values[0].text));
    if (!hit) return std::nullopt;
    uint8_t code = hit->row->code;
    if (code == kVintCodeOpen || code == kVintCodeVariant) return std::nullopt;
    return code;
}

void EngineState::setVintProperty(uint32_t handle, const std::string& propertyName, std::vector<VintTaggedValue> values) {
    auto objIt = vdoObjects_.find(handle);
    if (objIt == vdoObjects_.end()) {
        ++vintPropertyStats_.setBadHandle;
        return; // CONFIRMED (Sec20.1): an unresolvable handle simply fails to resolve later - silent no-op
    }
    const VdoObject& obj = objIt->second;
    const uint32_t nameHash = sr3save::nameHash(propertyName);
    if (!findVintElementType(obj.typeName)) {
        // Legacy untyped path (CHOSEN - see engine_state.h).
        ++(obj.typeName.empty() ? vintPropertyStats_.setRawUntypedObject : vintPropertyStats_.setRawUnknownType);
        vintProperties_[handle][nameHash] = VintStoredProperty{std::move(values), 0};
        return;
    }
    auto hit = findVintPropertyDescriptor(obj.typeName, nameHash);
    if (!hit) {
        ++vintPropertyStats_.setNameMiss; // CONFIRMED: a name miss writes nothing
        return;
    }
    const VintPropertyDescriptor& row = *hit->row;
    const bool family = vintIsElementFamily(obj.typeName);
    if (family && isFailStubRow(row.name)) {
        ++vintPropertyStats_.setReadOnlyNoop; // spec-vint-render.md Sec7.3 fail-stub
        return;
    }
    if (family && std::strcmp(row.name, "screen_size") == 0) {
        if (vintTypeIsA(obj.typeName, "group") || vintTypeIsA(obj.typeName, "point")) {
            ++vintPropertyStats_.setReadOnlyNoop;
            return;
        }
        ParseResult pr = parseVintValuesByCode(kVintCodeVec2, values, obj.typeName, row.name);
        if (pr.outcome != ParseOutcome::Typed) {
            ++vintPropertyStats_.setShapeRefusals;
            throw OpenStateError("vint_set_property " + obj.typeName + ".screen_size: " + pr.why, kSetSpec);
        }
        setVintScreenSize(handle, pr.values[0].number, pr.values[1].number);
        return;
    }
    uint8_t parseCode = row.code;
    const bool tweenBorrow = obj.typeName == "tween" && row.code == kVintCodeVariant &&
                             (ieq(propertyName, "start_value") || ieq(propertyName, "end_value"));
    if (tweenBorrow) {
        std::optional<uint8_t> borrowed = vintTweenBorrowedCode(handle);
        if (!borrowed) {
            ++vintPropertyStats_.setRawTweenBorrowUnresolved;
            vintProperties_[handle][nameHash] = VintStoredProperty{std::move(values), 0};
            return;
        }
        parseCode = *borrowed;
    }
    if (parseCode == kVintCodeOpen || parseCode == kVintCodeVariant) {
        ++vintPropertyStats_.setRawOpenCode; // CHOSEN pass-through
        vintProperties_[handle][nameHash] = VintStoredProperty{std::move(values), 0};
        return;
    }
    ParseResult pr = parseVintValuesByCode(parseCode, values, obj.typeName, row.name);
    switch (pr.outcome) {
        case ParseOutcome::Refuse:
            ++vintPropertyStats_.setShapeRefusals;
            throw OpenStateError("vint_set_property " + std::string(hit->owner->name) + "." + row.name + " (code " +
                                     std::to_string(parseCode) + "): " + pr.why,
                                 kSetSpec);
        case ParseOutcome::Raw:
            ++vintPropertyStats_.setRawEnumUnresolved;
            vintProperties_[handle][nameHash] = VintStoredProperty{std::move(values), 0};
            return;
        case ParseOutcome::Typed:
            ++vintPropertyStats_.setTyped;
            if (tweenBorrow) ++vintPropertyStats_.setTweenBorrowTyped;
            if (parseCode == kVintCodeCallback) ++vintPropertyStats_.setCallbackNamesStored;
            vintProperties_[handle][nameHash] = VintStoredProperty{std::move(pr.values), parseCode};
            return;
    }
}

std::optional<std::vector<VintTaggedValue>> EngineState::getVintProperty(uint32_t handle,
                                                                         const std::string& propertyName) const {
    auto objIt = vdoObjects_.find(handle);
    const VintStoredProperty* sp = findVintStoredProperty(handle, propertyName);
    if (objIt == vdoObjects_.end()) {
        ++vintPropertyStats_.getBadHandle;
        return std::nullopt; // CONFIRMED (Sec20.2): bad handle -> zero Lua values
    }
    if (!findVintElementType(objIt->second.typeName)) {
        // Legacy untyped path: echo whatever was stored, else zero values.
        if (!sp) {
            ++vintPropertyStats_.getNameMiss;
            return std::nullopt; // CONFIRMED (Sec20.2): bad handle / never-set -> zero Lua values
        }
        ++vintPropertyStats_.getRawEcho;
        return sp->values;
    }
    const VdoObject& obj = objIt->second;
    auto hit = findVintPropertyDescriptor(obj.typeName, sr3save::nameHash(propertyName));
    if (!hit) {
        ++vintPropertyStats_.getNameMiss;
        return std::nullopt; // CONFIRMED: property not found -> zero Lua values
    }
    const VintPropertyDescriptor& row = *hit->row;
    auto refuse = [&](const std::string& what, const char* spec) -> OpenStateError {
        ++vintPropertyStats_.getOpenRefusals;
        return OpenStateError("vint_get_property " + obj.typeName + " '" + obj.name + "'." + row.name + ": " + what, spec);
    };
    const bool family = vintIsElementFamily(obj.typeName);
    try {
        if (family && isQueryGeometryRow(row.name)) {
            const bool size = std::strcmp(row.name, "screen_size") == 0;
            std::vector<VintTaggedValue> out;
            if (size && vintTypeIsA(obj.typeName, "point")) {
                out = {numberValue(1), numberValue(1)}; // Sec7.2: point always reports (1,1)
            } else if (vintTypeIsA(obj.typeName, "text")) {
                throw OpenStateError("vint text " + std::string(row.name) + " of '" + obj.name + "' (text getter override / layout)",
                                     "spec-vint-render.md Sec1/Sec7.1/Sec11 item 6");
            } else {
                std::optional<VintBox> b = vintQueryBox(handle);
                if (!b) {
                    if (!size) {
                        throw OpenStateError("vint childless group '" + obj.name + "' collapsed point (position not given)",
                                             "spec-vint-render.md Sec7.1");
                    }
                    out = {numberValue(0), numberValue(0)}; // a single point has zero extent
                } else if (size) {
                    out = {numberValue(toF32(b->maxX - b->minX)), numberValue(toF32(b->maxY - b->minY))};
                } else if (std::strcmp(row.name, "screen_nw") == 0) {
                    out = {numberValue(b->minX), numberValue(b->minY)};
                } else {
                    out = {numberValue(b->maxX), numberValue(b->maxY)};
                }
            }
            ++vintPropertyStats_.getComputed;
            return out;
        }
        if (family && std::strcmp(row.name, "unscaled_size") == 0) {
            std::vector<double> sz = vintUnscaledSize(handle);
            ++vintPropertyStats_.getTyped;
            return std::vector<VintTaggedValue>{numberValue(sz[0]), numberValue(sz[1])};
        }
    } catch (const OpenStateError&) {
        ++vintPropertyStats_.getOpenRefusals;
        throw;
    }
    const uint8_t effective = (sp && sp->typedAs) ? sp->typedAs : row.code;
    if (effective == kVintCodeCallback || effective == kVintCodeEnum) {
        throw refuse("the value pushed for type code " + std::to_string(effective) + " is not specified", kGetSpec);
    }
    if (!sp) throw refuse("no value known (never set, no spec'd default)", "spec-vint-render.md Sec8 / spec-lua-bindings.md Sec20.2");
    if (family && std::strcmp(row.name, "mouse_depth") == 0 && sp->typedAs && !sp->values.empty() &&
        sp->values[0].kind == VintTaggedValue::Kind::Number && sp->values[0].number == 2147483647.0) {
        // Sec1: the getter returns depth while mouse_depth holds the sentinel.
        const VintStoredProperty* d = findVintStoredProperty(handle, "depth");
        if (!d) throw refuse("depth (followed by an unset mouse_depth) not known", "spec-vint-render.md Sec1");
        ++vintPropertyStats_.getComputed;
        return d->values;
    }
    ++(sp->typedAs ? vintPropertyStats_.getTyped : vintPropertyStats_.getRawEcho);
    return sp->values;
}

} // namespace sr3luahost
