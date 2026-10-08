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

#include "sr3luahost/vint_property_types.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"

namespace sr3luahost {

namespace {

// Character spawn-state's own "round(M * Hit_Points)" (spec-lua-api-
// behaviour.md Sec34.2) does not name a distinct rounding routine, so this
// reuses this project's one CONFIRMED rounding convention already
// established for a bare-global `round()` call (Sec26.27,
// lua_bare_globals.cpp's own bare_round): narrow to float first, then half
// away from zero. Not independently re-derived for this specific call site -
// HIGH CONFIDENCE by convention, not claimed CONFIRMED on its own.
int32_t RoundHalfAwayFromZero(double x) {
    double t = (x >= 0.0) ? std::trunc(x + 0.5) : std::trunc(x - 0.5);
    if (t < -2147483648.0) return INT32_MIN;
    if (t > 2147483647.0) return INT32_MAX;
    return static_cast<int32_t>(t);
}

} // namespace

EngineState::EngineState() {
    // spec-player-runtime.md Sec2.7: the rows-0/1 enter 0x005aa5f0 begins
    // with "clear latched" 0x005bbf60 (Sec1.11.1) on the control context.
    controlStateMachine_.setClearLatched([this] { inputSystem_.clearLatched(); });
    // Sec1.4 step 5: the latch reads the current control state (0x005aa630).
    inputSystem_.setControlStateGetter([this] { return controlStateMachine_.currentState(); });
}

CharacterState& EngineState::getOrCreateCharacter(const std::string& name) {
    auto it = characters_.find(name);
    if (it != characters_.end()) return it->second;
    CharacterState& c = characters_[name]; // default-constructs on first reference
    // CONFIRMED (Sec34.1/Sec34.3): ignore-AI off at construction, re-cleared
    // by the no-bound-script-NPC default path - see CharacterState::ignoreAI's
    // own doc comment. markScriptNpcBoundForTesting() forgets this back to
    // OPEN for the one case (a bound script NPC) where the real answer is
    // zone data this project doesn't have.
    c.ignoreAI.set(false);
    // CONFIRMED (Sec3.7's own single-player resolution, 2026-10-03): the
    // force-flag double-gate always resolves true with no session installed
    // - see CharacterState::forceFlagGatePasses's own doc comment.
    c.forceFlagGatePasses.set(true);
    // CONFIRMED (Sec34.5, 2026-10-03): +0xcc8 is zeroed by the constructor
    // and +0xe3 bit 0x01 is clear from a zero-filled allocation - see
    // CharacterState::lifeState/killFinalizedFlagE3's own doc comments.
    c.lifeState.set(0);
    c.killFinalizedFlagE3.set(false);
    // CHOSEN (not CONFIRMED): the 0x008addb0 single gate of the
    // player_controls_*/player_movement_* setters takes the direct-apply
    // branch - see CharacterState::controlLockGatePasses's own doc comment.
    c.controlLockGatePasses.set(true);
    // HIGH CONFIDENCE (spec-player-runtime.md Sec2.10, 2026-10-03): +0x1d99
    // is all-clear (enabled) at spawn, a zero-filled-allocation default - see
    // CharacterState::controlsDisabled/movementDisabled's own doc comments.
    c.controlsDisabled.set(false);
    c.movementDisabled.set(false);
    return c;
}

bool EngineState::hasCharacter(const std::string& name) const {
    return characters_.count(name) != 0;
}

VehicleState& EngineState::getOrCreateVehicle(const std::string& name) {
    auto it = vehicles_.find(name);
    if (it != vehicles_.end()) return it->second;
    VehicleState& v = vehicles_[name]; // default-constructs on first reference
    // CONFIRMED (Sec3.7's own single-player resolution, cross-ref Sec30.5,
    // 2026-10-03): the force-flag double-gate always resolves true with no
    // session installed - see VehicleState::forceFlagGatePasses's own doc
    // comment.
    v.forceFlagGatePasses.set(true);
    return v;
}

bool EngineState::hasVehicle(const std::string& name) const {
    return vehicles_.count(name) != 0;
}

void EngineState::applyCharacterSpawnDefaults(const std::string& name, uint32_t hitPoints, double multiplier) {
    CharacterState& c = getOrCreateCharacter(name);
    int32_t maxHp = RoundHalfAwayFromZero(multiplier * static_cast<double>(hitPoints));
    c.maxHitPoints.set(maxHp);
    c.currentHitPoints.set(maxHp); // Sec34.1 step 2: spawns at full health
    c.ignoreAI.set(false);          // Sec34.1 step 3 / Sec34.3: re-cleared, redundant with getOrCreateCharacter's own default but explicit here too
}

void EngineState::markScriptNpcBoundForTesting(const std::string& name) {
    CharacterState& c = getOrCreateCharacter(name);
    // Sec34.4: the override question (script_npc_hp / script_npc_flags
    // "ignore_ai") is zone data this project does not have - forget the
    // CONFIRMED defaults back to OPEN rather than guess either way.
    c.ignoreAI.forget();
    c.maxHitPoints.forget();
    c.currentHitPoints.forget();
}

void EngineState::registerSyncedActionForTesting(const std::string& name, int32_t index) {
    syncedActionIndexByName_[name] = index;
}

int32_t EngineState::lookupSyncedActionIndex(const std::string& name) const {
    auto it = syncedActionIndexByName_.find(name);
    return it != syncedActionIndexByName_.end() ? it->second : kSyncedActionNotFound;
}

void EngineState::actionSequenceEnd() {
    // action_sequence_end (Sec33.3): clears the shared sequence object and
    // per-player scripted-camera/target fields; releases two tracked
    // handles (this project's own combined counter, see ActionSequence's
    // own doc comment); host-only broadcasts an opcode-0x49 record.
    actionSequence_.active = false;
    actionSequence_.localScriptedCameraTarget.clear();
    actionSequence_.remoteScriptedCameraTarget.clear();
    actionSequence_.trackedHandleReleaseCount += 2;
    if (coopLocalIsHost()) ++actionSequence_.hostBroadcastCount;
}

void EngineState::replicateStateChange(const std::string& fieldTag, const std::string& objectName) {
    // Stated, explicit no-op (see engine_state.h's own doc comment on this
    // function): a real coop/network-sync commit would happen here on the
    // real engine. This project builds no networking layer at all, so
    // every real call site that would open one of these records instead
    // calls this function - purely so those call sites are visibly marked
    // in the source, not silently dropped - and does nothing further.
    // Parameters intentionally unused beyond documenting the call site.
    (void)fieldTag;
    (void)objectName;
}

uint32_t EngineState::registerVdoObjectForTesting(const std::string& name, uint32_t parentHandle, uint32_t docHandle) {
    uint32_t handle = nextVdoObjectHandle_++;
    VdoObject obj;
    obj.name = name;
    obj.nameHash = sr3save::nameHash(name); // FUN_00D9E740 stand-in - see engine_state.h's own doc comment
    obj.parentHandle = parentHandle;
    obj.docHandle = docHandle;
    vdoObjects_[handle] = obj;
    return handle;
}

const VdoObject* EngineState::vdoObjectForTesting(uint32_t handle) const {
    auto it = vdoObjects_.find(handle);
    return it == vdoObjects_.end() ? nullptr : &it->second;
}

uint32_t EngineState::findVdoObject(const std::string& name, bool hasParent, uint32_t parentHandle,
                                     bool hasDoc, uint32_t docHandle) const {
    uint32_t targetHash = sr3save::nameHash(name);
    // CONFIRMED (Sec15): "parent_handle, if given, resolves through
    // FUN_00e28850 ... then the hashed name is looked up among that
    // resolved parent's children, OR document-wide if no parent was
    // resolved" - "no parent was resolved" covers BOTH an absent/wrong-
    // type argument AND a given-but-unresolvable (stale/bad) handle
    // number; either way this falls through to the document-wide search
    // below, it does NOT short-circuit to "not found" on a bad handle
    // number alone.
    bool parentResolved = hasParent && (vdoObjects_.find(parentHandle) != vdoObjects_.end());
    if (parentResolved) {
        for (const auto& [h, obj] : vdoObjects_) {
            if (obj.parentHandle == parentHandle && obj.nameHash == targetHash) return h;
        }
        return 0; // resolved parent, no matching child - real "not found" path
    }
    // The current default document is OPEN state; read it only when the
    // answer depends on it. With no object registered under this name at
    // all, "not found" holds for every document, so it is not read.
    bool anyWithName = false;
    for (const auto& [h, obj] : vdoObjects_) {
        if (obj.nameHash == targetHash) { anyWithName = true; break; }
    }
    if (!anyWithName) return 0;
    uint32_t targetDoc = hasDoc ? docHandle : currentDefaultDocHandle_.get();
    for (const auto& [h, obj] : vdoObjects_) {
        if (obj.docHandle == targetDoc && obj.nameHash == targetHash) return h;
    }
    return 0;
}

void EngineState::setVdoObjectFirstChildForTesting(uint32_t parentHandle, uint32_t childHandle) {
    auto it = vdoObjects_.find(parentHandle);
    if (it == vdoObjects_.end()) return; // test/setup-only - no-op on an unregistered parent, nothing to attach to
    it->second.firstChildHandle = childHandle;
}

uint32_t EngineState::vdoObjectFirstChild(uint32_t handle) const {
    auto it = vdoObjects_.find(handle);
    if (it == vdoObjects_.end()) return 0; // CONFIRMED (Sec18.1): bad handle -> zero Lua values
    return it->second.firstChildHandle; // 0 either way is "no children" - CONFIRMED, zero Lua values
}

uint32_t EngineState::cloneVdoObject(uint32_t origHandle, bool hasParentArg, uint32_t parentHandleArg) {
    // Step 1 (CONFIRMED order, Sec18.2): the current document is resolved
    // BEFORE touching either handle - an OPEN read here refuses before any
    // other work happens, same "every read before any write" convention
    // already established elsewhere in this file (e.g. zscenePrep).
    uint32_t doc = currentDefaultDocHandle_.get();

    // Step 2: resolve the source object.
    auto origIt = vdoObjects_.find(origHandle);
    if (origIt == vdoObjects_.end()) return 0; // CONFIRMED: bad orig_handle -> 0.0 (the real engine also logs a debug string here, not Lua-visible)
    const VdoObject& orig = origIt->second;

    // Step 3: resolve the requested parent, falling back to the original's
    // own parent (CONFIRMED, Sec18.2).
    uint32_t parent = (hasParentArg && vdoObjects_.count(parentHandleArg)) ? parentHandleArg : orig.parentHandle;

    // Step 4: the real virtual clone call - this project's own minimal
    // stand-in (see cloneVdoObject's own header doc comment for why the
    // new object's content is not copied). No further failure condition is
    // modeled, so this always succeeds once the above resolves.
    uint32_t handle = nextVdoObjectHandle_++;
    VdoObject clone;
    clone.parentHandle = parent;
    clone.docHandle = doc;
    vdoObjects_[handle] = clone;
    return handle;
}

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

void EngineState::registerDataResponderForTesting(const std::string& name, bool finished) {
    vintDataResponders_[sr3save::nameHash(name)] = VintDataResponderRecord{finished, 0};
}

bool EngineState::dataResponderFinished(const std::string& name) const {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    if (it == vintDataResponders_.end()) return true; // CONFIRMED real quirk (Sec21.1): no record at all -> "finished"
    return it->second.finished;
}

void EngineState::dataResponderRequest(const std::string& name, bool validCallback, bool validMax) {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    if (it == vintDataResponders_.end()) return; // CONFIRMED (Sec21.2): no record -> complete, silent no-op
    if (!validCallback || !validMax) return;       // CONFIRMED: wrong/missing arg type -> complete, silent no-op
    ++it->second.dispatchAttempts; // test-observable only - see this function's own header doc comment
}

int EngineState::dataResponderDispatchAttempts(const std::string& name) const {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    return it == vintDataResponders_.end() ? 0 : it->second.dispatchAttempts;
}

void EngineState::registerScriptedRequestForTesting(const std::string& name, int kind, bool done) {
    scriptedRequestDone_[{name, kind}] = done;
}

int EngineState::scriptedRequestStatusCode(const std::string& name, int kind) {
    auto it = scriptedRequestDone_.find({name, kind});
    if (it == scriptedRequestDone_.end()) return 2; // Sec35.2: no matching request
    if (!it->second) return 0;                      // Sec35.2: request exists, still pending
    scriptedRequestDone_.erase(it);                  // Sec35.2: a "done" read releases the record
    return 1;
}

void EngineState::setVehiclePathfindResolvableForTesting(const std::string& vehicleName, bool resolvable) {
    if (resolvable) vehiclePathfindResolvable_.insert(vehicleName);
    else vehiclePathfindResolvable_.erase(vehicleName);
}

double EngineState::vehiclePathfindCheckDoneCode(const std::string& vehicleName) {
    if (!vehiclePathfindResolvable_.count(vehicleName)) return 2.0; // Sec9.10: unresolved vehicle/no slot-0 occupant fallback
    return static_cast<double>(scriptedRequestStatusCode(vehicleName, kScriptedRequestKindMoveOrPathfind));
}

int EngineState::tutorialAdvanceCount(int index) const {
    auto it = tutorialAdvanceCounts_.find(index);
    return it == tutorialAdvanceCounts_.end() ? 0 : it->second;
}

void EngineState::recordTutorialAdvance(int index) {
    ++tutorialAdvanceCounts_[index];
}

// tutorial_lock (Sec52.3, tranche 15, real-hit batch 2026-10-03) - see
// engine_state.h's own doc comment on tutorialLockCount()/
// recordTutorialLock() for the full citation.
int EngineState::tutorialLockCount(int index) const {
    auto it = tutorialLockCounts_.find(index);
    return it == tutorialLockCounts_.end() ? 0 : it->second;
}

void EngineState::recordTutorialLock(int index) {
    ++tutorialLockCounts_[index];
}

// dlc2_m02_clapboards_get/_reset (Sec55.5, tranche 22, real-hit batch
// 2026-10-03) - see engine_state.h's own doc comment for the full citation
// (CONFIRMED shape, HOST-SAFETY lower-bound deviation).
void EngineState::dlc2ClapboardsReset(int32_t rawCount) {
    // CONFIRMED: "clamped to at most 10, no lower clamp" - a negative
    // rawCount is stored as-is (matching the real native exactly; it is
    // harmless here because the zero-fill loop below only runs for a
    // strictly positive count).
    dlc2ClapboardCount_ = rawCount > 10 ? 10 : rawCount;
    for (int32_t i = 0; i < dlc2ClapboardCount_ && i < static_cast<int32_t>(dlc2ClapboardFlags_.size()); ++i) {
        dlc2ClapboardFlags_[static_cast<size_t>(i)] = false;
    }
}

int EngineState::dlc2ClapboardsGet(int32_t clapboardNumber) {
    // CONFIRMED (1-based -> 0-based): index = clapboardNumber - 1, compared
    // against dlc2ClapboardCount_ as a signed value with only the upper
    // bound checked by the real native. HOST-SAFETY DEVIATION (not a spec
    // fact): clapboardNumber <= 0 (the real arbitrary-read trigger) is
    // refused here instead of reproduced - this project does not simulate
    // crashes/arbitrary reads (group_get_next_npc / Sec28.5 precedent).
    if (clapboardNumber <= 0) {
        ++dlc2ClapboardsGetNegativeIndexGuardCount_; // HOST-SAFETY, see doc comment above
        return -1;
    }
    const int32_t index = clapboardNumber - 1;
    // CONFIRMED: out of the upper bound -> "no value at all" (-1 sentinel
    // here, pushed as nil by the stub, not false).
    if (index >= dlc2ClapboardCount_ || index >= static_cast<int32_t>(dlc2ClapboardFlags_.size())) return -1;
    return dlc2ClapboardFlags_[static_cast<size_t>(index)] ? 1 : 0;
}

EngineState::TutorialLookup EngineState::tutorialLookup(const std::string& name) {
    // 0x00717780: case-insensitive linear scan of the 210 names, first match
    // (Sec6.19/Sec26.28, CONFIRMED). ASCII case folding, as the C locale's
    // case-insensitive compare does.
    auto equalsNoCase = [](const char* a, const std::string& b) {
        size_t i = 0;
        for (; a[i] != '\0'; ++i) {
            if (i >= b.size()) return false;
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return i == b.size();
    };
    TutorialLookup out;
    for (int i = 0; i < kTutorialEntryCount; ++i) {
        const char* n = tutorialName(i);
        if (n && equalsNoCase(n, name)) {
            out.index = i;
            break;
        }
    }
    // Index 176's string is OPEN: "shorter than 4 characters or non-ASCII"
    // (Sec26.28). A name that could be it, with no known match before 176,
    // might resolve to 176 instead of `index`.
    bool candidate = name.size() < 4;
    for (unsigned char c : name) candidate = candidate || c >= 0x80;
    out.couldBeIndex176 = candidate && (out.index < 0 || out.index > 176);
    return out;
}

bool EngineState::coopIsActive() const {
    // 0x00867830 (Sec3.1, CONFIRMED - disassembly), conditions in order.
    if (!coopSession_.present.get()) return false;                                        // 1. no session
    if (!coopSession_.localIsHost.get() && !coopSession_.clientGate.get()) return false; // 2. host test first
    if (coopSession_.memberCount.get() < 2u) return false;                                // 3. unsigned, >= 2
    return coopSession_.otherMembersPassSlotCheck.get();                                  // 4. member walk
}

bool EngineState::coopLocalIsHost() const {
    // 0x008440a0 (Sec8.27): non-null session and +0x5c == +0x58.
    return coopSession_.present.get() && coopSession_.localIsHost.get();
}

bool EngineState::coopLocalIsClient() const {
    // 0x007bfbc0 (Sec10.2): null session -> false; else +0x5c != +0x58.
    return coopSession_.present.get() && !coopSession_.localIsHost.get();
}

uint32_t EngineState::multiply33XorHashBucket(const std::string& name, uint32_t bucketCount) {
    // spec-texture-format.md Sec8.2, transcribed exactly: "fold each
    // character to lowercase, then hash = (hash * 0x21) XOR character, and
    // finally take hash % bucket_count." Empirically vector-validated,
    // spec-lua-api-behaviour.md Sec5.3 (5/5 real I/O vectors matched).
    uint32_t hash = 0;
    for (unsigned char c : name) {
        unsigned char lower = static_cast<unsigned char>(std::tolower(c));
        hash = (hash * 0x21u) ^ lower;
    }
    if (bucketCount == 0) return 0; // guard only - the real function is never called this pass with bucketCount==0 (game_peg_load_with_cb always passes the fixed literal 9000)
    return hash % bucketCount;
}

bool EngineState::multiply33XorHashBucketUnambiguous(const std::string& name, uint32_t bucketCount) {
    uint32_t hash = 0;
    for (unsigned char c : name) {
        if (c >= 0x80) return false;
        unsigned char lower = static_cast<unsigned char>(std::tolower(c));
        hash = (hash * 0x21u) ^ lower;
    }
    if (bucketCount != 0 && (bucketCount & (bucketCount - 1)) == 0) return true;
    return hash < 0x80000000u;
}

namespace {
std::string lowercased(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
} // namespace

std::string EngineState::zsceneTableKey(const std::string& name) {
    return lowercased(name);
}

// zscenePrep / zsceneIsLoaded and the per-frame zscene / cutscene driver
// live in src/lua_cutscene.cpp (Sec26.25, 2026-10-01 nnlt text).

bool EngineState::vintIsStdRes() const {
    // 0x00e1a150 (Sec26.26, CONFIRMED): first / second in double precision.
    const double first = static_cast<double>(vintRecordFirst_.get());
    const double second = static_cast<double>(vintRecordSecond_.get());
    double q;
    if (second != 0.0) {
        q = first / second;
    } else {
        // IEEE division by zero (the x87 default, exceptions masked):
        // +-infinity, or NaN for 0/0. Spelled out so no sanitizer sees a
        // division by zero.
        q = first == 0.0 ? std::numeric_limits<double>::quiet_NaN()
                         : (first > 0.0 ? std::numeric_limits<double>::infinity()
                                        : -std::numeric_limits<double>::infinity());
    }
    if (q < 1.5) return true; // the double at 0x012a2d30; false for NaN
    return vintDisplayMode_.get() == 2;
}

// =========================================================================
// pause-map stag mode / district control (spec-lua-api-behaviour.md Sec31)
// - batch 2026-10-02.
// =========================================================================

double EngineState::pauseMapZoneControlFraction(uint32_t zoneHandle) const {
    double total = 0.0;
    double owned = 0.0;
    auto it = zoneMembersByZone_.find(zoneHandle);
    if (it != zoneMembersByZone_.end()) {
        for (const auto& m : it->second) {
            total += static_cast<double>(m.weight);
            if (m.owned) owned += static_cast<double>(m.weight);
        }
    }
    // CONFIRMED (Sec31.2): total <= 0 (including a NaN total, which fails
    // every ordered comparison) pushes the constant 1.0 instead of dividing.
    if (!(total > 0.0)) return 1.0;
    return owned / total;
}

// pause_map_set_gps (Sec31.3). CONFIRMED: while stag mode (0x0229a317, the
// SAME global game_autosave's own gate reads - Sec27.12/Sec31.5) is on, a
// valid hovered zone (non-null, +0x44 non-null, +0x54 > 0) is copied into
// the selected-zone global and the Lua hook `pause_map_stag_completion`
// fires through the established hook dispatch trio (0x00e0cef0/0x00e0ca80/
// 0x00e0cd00). That real per-call dispatch already has its own general,
// existence-gated per-script firing pass elsewhere in this project
// (hook_registry.cpp's `pause_map_stag_completion` entry, Group 1) - rather
// than invoking the Lua callback a SECOND time through a separate path
// here, this method counts that the real gate fired
// (pauseMapStagCompletionHookFiredCount_), which is itself the real,
// CONFIRMED condition under which the engine's own dispatch would run.
// Outside stag mode, or with no valid hovered zone: CONFIRMED "it does
// nothing" to 0x0229a2ac - the real non-stag GPS-route behavior is not
// traced by this section and is not modeled (this project's own stated
// simplification, not a claim the real function does nothing at all).
void EngineState::pauseMapSetGpsStagBranch() {
    if (!autosave_.fourthFlagNonzero.get()) return; // OPEN until set; not in stag mode -> real GPS-route behavior, not modeled here
    const PauseMapHoveredZone& h = pauseMapHoveredZone_;
    if (h.zoneHandle != 0 && h.hasParentDistrict && h.fieldPlus0x54 > 0) {
        pauseMapSelectedZone_ = h.zoneHandle;
        ++pauseMapStagCompletionHookFiredCount_;
    }
}

// pause_map_stag_takeover_do_reward (Sec31.6). CONFIRMED: claims every
// member of the selected zone not yet owned (sets bit 0x02 of +0x3a - and,
// per Sec31.4, bit 0x01 too, though nothing in this batch's own scope reads
// that second bit, so only the Lua-visible "owned" bit is modeled); clears
// stag mode; requests an autosave (no longer suppressed, since clearing
// stag mode lifts game_autosave's own 0x0229a317 gate). The real reward-
// summary display (0x007ef9a0, HYPOTHESIS) and its own feeder reads (local
// player cash/respect, the parent district's control fraction via
// 0x0084ce80) have no further Lua-visible consequence among this batch's
// own in-scope names and are not modeled.
//
// Sec31.6's own text: "this function dereferences the selected zone's own
// +0x44 without a null check - it must only be called after a valid
// selection has been made." With no zone selected (pauseMapSelectedZone_ ==
// 0), the real engine's behavior is undefined (a null-pointer dereference);
// this host safely refuses instead of reproducing that crash, matching the
// project's own "flag a real hazard, don't reproduce it" precedent for the
// three crash-shaped paths ranking tranche 04 itself flags (Sec32.8).
void EngineState::pauseMapStagTakeover() {
    if (pauseMapSelectedZone_ == 0) return; // real engine: undefined-behavior null-deref on +0x44; this host refuses safely instead
    auto it = zoneMembersByZone_.find(pauseMapSelectedZone_);
    if (it != zoneMembersByZone_.end()) {
        for (auto& m : it->second) {
            if (!m.owned) {
                m.owned = true;
                ++pauseMapTakeoverClaimedCount_;
            }
        }
    }
    autosave_.fourthFlagNonzero.set(false); // CONFIRMED: clears stag mode
    ++pauseMapTakeoverAutosaveRequestCount_; // CONFIRMED: requests an autosave; the real save itself (0x00b94ff0) is OPEN per spec, same precedent as game_autosave's own triggeredCount
}

// =========================================================================
// Ranking tranche 04 (spec-lua-api-behaviour.md Sec32) - batch 2026-10-02.
// =========================================================================

void EngineState::resetSpawnRegionMaxSpawnDist() {
    spawnRegionMaxSpawnDistSquared_ = std::numeric_limits<float>::max(); // CONFIRMED "reset" value, FLT_MAX
}

// set_time_of_day (Sec32.1 forward-jump arithmetic, corrected by Sec48.4's
// own real post-condition - see GameClock's own header doc comment).
// CONFIRMED: computes an hour delta forced non-negative (adds 24 if it
// would go negative - "the clock only ever advances"), combines it with
// the raw minute delta into a total seconds figure - kept as
// lastAdvanceSeconds, test-observable. The real engine then snaps the
// clock to the nearest time-of-day key (Sec48.4), which this project
// cannot compute (the key list is OPEN loaded data) - so hour/minute are
// forgotten rather than set to newHour/newMinute, honestly reflecting that
// the real post-call value is unknown, not fabricated as "whatever was
// requested."
void EngineState::setTimeOfDay(int64_t newHour, int64_t newMinute) {
    int64_t curHour = gameClock_.hour.get();     // OPEN until set (Sec48.2: known from construction in single player)
    int64_t curMinute = gameClock_.minute.get(); // OPEN until set
    int64_t hourDelta = newHour - curHour;
    if (hourDelta < 0) hourDelta += 24; // CONFIRMED: forced forward
    int64_t totalMinutes = hourDelta * 60 + (newMinute - curMinute);
    gameClock_.lastAdvanceSeconds = totalMinutes * 60;
    ++gameClock_.advanceRequestCount;
    gameClock_.second = 0; // CONFIRMED: the post-jump key-snap zeroes the seconds (Sec48.4)
    gameClock_.hour.forget();   // Sec48.4: the real post-snap value is OPEN, not newHour
    gameClock_.minute.forget(); // Sec48.4: the real post-snap value is OPEN, not newMinute
}

// Per-frame clock advance (Sec48.3, CONFIRMED mechanism - see GameClock's
// own header doc comment for the CHOSEN call cadence and the stated
// day-rollover simplification). A no-op while hour/minute are unknown
// (e.g. after setTimeOfDay forgot them above) - there is no known base to
// add a delta to.
void EngineState::gameClockAdvanceFrame(double realSeconds) {
    gameClock_.frameAdvanceSecondsAccumulated += realSeconds;
    if (!gameClock_.hour.known() || !gameClock_.minute.known()) return;
    int64_t secondsOfDay = gameClock_.hour.get() * 3600 + gameClock_.minute.get() * 60 + gameClock_.second;
    double gameSeconds = realSeconds * GameClock::kTimeScale;
    secondsOfDay = (secondsOfDay + static_cast<int64_t>(gameSeconds)) % 86400;
    if (secondsOfDay < 0) secondsOfDay += 86400; // defensive: realSeconds is never negative in scope
    gameClock_.hour.set(static_cast<int32_t>(secondsOfDay / 3600));
    gameClock_.minute.set(static_cast<int32_t>((secondsOfDay % 3600) / 60));
    gameClock_.second = static_cast<int32_t>(secondsOfDay % 60);
}

// satellite_weapon_mode_exit (Sec32.1). CONFIRMED: "with authority and the
// controller active, tears down camera/HUD/audio state locally"; without
// authority, forwards a request instead (this project's own no-networking-
// layer convention treats every call as locally authoritative, same as
// set_ignore_ai_flag/set_current_hit_points above - only the
// controller-active gate is modeled). The caller (lua_spec_confirmed_
// stubs.cpp's own stub_satellite_weapon_mode_exit) is where the 0x009df3d0
// correction is actually applied: `remote` is only ever passed true when
// playerRig().coopPlayerPresent is true.
void EngineState::satelliteWeaponExit(bool remote) {
    if (!satelliteWeapon_.active.get()) return; // OPEN until set
    if (remote) ++satelliteWeapon_.remoteExitCount;
    else ++satelliteWeapon_.localExitCount;
}

void EngineState::setQteSlotForTesting(int index, bool active, std::string owningPlayerName,
                                        std::vector<std::string> participants) {
    if (index < 0 || index > 1) return; // test/setup-only - the real record array is exactly 2 slots (Sec32.3)
    qteSlots_[index] = QteSlot{active, std::move(owningPlayerName), std::move(participants)};
}

// qte_human_is_used (Sec32.3). CONFIRMED: true if the queried character
// either IS the active slot's own owning player, or matches any of its
// participant id-pair fields; false otherwise (including no active slot at
// all). See QteSlot's own doc comment for this project's name-keyed
// simplification of the real local/remote selection mechanism.
bool EngineState::qteHumanIsUsed(const std::string& name) const {
    for (const auto& slot : qteSlots_) {
        if (!slot.active) continue;
        if (slot.owningPlayerName == name) return true;
        for (const auto& p : slot.participantNames) {
            if (p == name) return true;
        }
    }
    return false;
}

std::vector<EngineState::OpenSlotStatus> EngineState::openSlotInventory() const {
    std::vector<OpenSlotStatus> out;
    auto value = [&](const char* area, const auto& v) {
        out.push_back({area, v.global(), v.spec(), "value", v.known(), v.known() ? 1u : 0u});
    };
    auto map = [&](const char* area, const auto& m) {
        out.push_back({area, m.what(), m.spec(), "per-name map", false, m.knownCount()});
    };
    value("co-op", coopSession_.present);
    value("co-op", coopSession_.localIsHost);
    value("co-op", coopSession_.clientGate);
    value("co-op", coopSession_.memberCount);
    value("co-op", coopSession_.otherMembersPassSlotCheck);
    value("co-op", coopJoinType_);
    // Player runtime batch (2026-10-03): the one global OpenValue singleton
    // it adds (the per-character +0x1d99 bits live on CharacterState).
    value("player", playerControlsLockHandle_);
    map("tutorial", tutorialState_);
    value("vehicle-store", vehicleStoreActive_);
    map("zscene", zsceneLoadable_);
    value("zscene", zsceneSkipAllCutscenes_);
    value("zscene", zsceneCurrent_);
    value("zscene", zscenePending_);
    value("zscene", zsceneStateCode_);
    value("zscene", zsceneAutoSelectNearest_);
    value("zscene", zsceneRequeueOnReset_);
    map("zscene", zsceneHandleClass_);
    value("zscene", zsceneSoundtrackActive_);
    value("zscene", zsceneSoundtrackStartMs_);
    value("zscene", zsceneSoundtrackEnded_);
    value("zscene", zsceneNearestWorldObjectScene_);
    out.push_back({"zscene", "scene table 0x0153b294 (cutscene.xtbl names + <name>.cte_xtbl fields)",
                   "spec-lua-api-behaviour.md Sec26.25", "table", zsceneTableInstalled_, zsceneTable_.size()});
    value("cutscene", cutsceneManager_.present);
    value("cutscene", cutsceneManager_.sceneKey);
    value("cutscene", cutsceneManager_.field8);
    value("cutscene", cutsceneManager_.chainTarget);
    value("cutscene", cutscenePreloadMounted_);
    value("cutscene", cutsceneChainTarget_);
    value("cutscene", cutscenePlayerChecksPass_);
    value("cutscene", cutsceneLoadStamp_);
    value("cutscene", cutscenePlayingByte_);
    value("cutscene", cutsceneInProgressByte_);
    value("fade", screenFade_.state);
    value("fade", screenFade_.target);
    value("fade", screenFade_.flag);
    value("fade", screenFade_.documentLoaded);
    value("fade", screenFade_.logoAt);
    value("fade", screenFade_.holdLogoUntil);
    value("fade", screenFade_.imagesAt);
    value("fade", screenFade_.holdImagesUntil);
    value("fade", screenFade_.autoSaveStamp);
    value("fade", screenFade_.autoSaveCounter);
    value("fade", screenFade_.useLoadImages);
    value("fade", screenFade_.lastBroadcastWasOut);
    value("fade", screenFade_.modeStackTop);
    value("fade", screenFade_.cutsceneState);
    value("vint", vintRecordFirst_);
    value("vint", vintRecordSecond_);
    value("vint", vintDisplayMode_);
    value("vint", vintGlobalWidth_);
    value("vint", vintGlobalHeight_);
    value("vint", vintLayoutIndex_);
    value("vint", vintSafeFrameA_);
    value("vint", vintSafeFrameB_);
    value("vint", vintSafeFrameScale1_);
    value("vint", vintSafeFrameScale2_);
    value("other", hasLocalPlayer_);
    value("other", currentDefaultDocHandle_);
    map("other", objectResolves_);
    map("vint", vintTimeIndexByDoc_);
    size_t knownBits = 0;
    for (uint32_t m = missionFlagsWord_.knownMask(); m != 0; m &= m - 1) ++knownBits;
    out.push_back({"other", missionFlagsWord_.global(), missionFlagsWord_.spec(), "bit word",
                   missionFlagsWord_.knownMask() == 0xFFFFFFFFu, knownBits});

    // --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27/Sec28 ---
    value("batch2728", catMouseMinigame_.present);
    value("batch2728", catMouseMinigame_.fieldIc1IsOne);
    map("batch2728", missionComplete_);
    value("batch2728", completionScreen_.present);
    value("batch2728", completionScreen_.flagClusterSet);
    value("batch2728", completionScreen_.derefByteAtLeastOne);
    value("batch2728", completionScreen_.everyPlayerAtThreshold1);
    value("batch2728", completionScreen_.recordEnabled);
    value("batch2728", vcustCamera_.targetPresent);
    value("batch2728", vcustCamera_.targetValid);
    value("batch2728", vcustCamera_.targetAlive);
    map("batch2728", garagePreview_.vehicleTypeAtIndex);
    value("batch2728", garagePreview_.previewCache);
    map("batch2728", dialogForceClose_.slotMatchesId);
    map("batch2728", dialogForceClose_.timerArmedNotExpired);
    map("batch2728", dialogForceClose_.alreadyClosing);
    map("batch2728", dialogForceClose_.hasResultCallback);
    map("batch2728", dialogForceClose_.callbackNameBlank);
    map("batch2728", dialogForceClose_.fullyRemoved);
    value("batch2728", autosave_.suppressFlag);
    value("batch2728", autosave_.secondFlagSet);
    value("batch2728", autosave_.missionActive);
    value("batch2728", autosave_.thirdGateBlocks);
    value("batch2728", autosave_.fourthFlagNonzero);
    value("batch2728", playerSlots_.count);
    map("batch2728", playerSlots_.sendInviteOk);
    map("batch2728", playerSlots_.canSendInvite);
    map("batch2728", playerSlots_.joinFriendInProgressOk);
    value("batch2728", coopFriendlyFireRaw_);
    value("batch2728", steamInterfaceAvailable_);
    value("batch2728", inProgressType_.activeMissionPresent);
    value("batch2728", inProgressType_.activeMissionType);
    value("batch2728", inProgressType_.activityPresent);
    value("batch2728", inProgressType_.activityType);
    map("batch2728", helicopterFlyTo_.qualifies);
    map("batch2728", helicopterFlyTo_.applyGate);
    value("batch2728", guardianAngel_.modeIsThree);
    value("batch2728", guardianAngel_.objectPresent);
    map("batch2728", groupNextNpcName_);
    map("batch2728", groupFirstNpcName_);
    map("batch2728", humansInTriggerCount_);
    map("batch2728", vehicleInAirByCharacter_);
    value("batch2728", effectFinisher_.gamepadMode);
    map("batch2728", effectFinisher_.effectIndexValid);
    value("batch2728", effectFinisher_.successReturn);
    value("batch2728", playerRig_.localPlayer1Gender);
    value("batch2728", playerRig_.coopPlayerPresent);
    value("batch2728", playerRig_.coopPlayerGender);
    map("batch2728", stronghold_.stillLocked);
    map("batch2728", continuousExplosion_.definitionResolves);
    value("batch2728", continuousExplosion_.active);
    map("batch2728", characterGender_);
    value("batch2728", cellphoneAnimSuppressed_);
    value("batch2728", bossBattleMatt_.cheatSlot);
    value("batch2728", bossBattleMatt_.retryCounter);

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec31/Sec32 ---
    // (CharacterState's own per-character OpenValue fields - lifeState here,
    // like stateEnum/attackerThreatRef/maxHitPoints/currentHitPoints above -
    // are not centrally inventoried, matching this function's own existing
    // precedent of only listing EngineState-level slots.)
    value("batch3132", pauseMapTutorialMode_);
    value("batch3132", storeInterfaceActive_);
    value("batch3132", gameClock_.hour);
    value("batch3132", gameClock_.minute);
    value("batch3132", satelliteWeapon_.active);
    value("batch3132", helicopterFireDispatcherResult_);

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
    // ("ranking tranches 06/07/08") --- vcustPreview_.targetLive and
    // storePreviewGuards_.{gangPreviewAssetOrFallbackResolved,
    // galleryListObjectResolved} are CHOSEN plain bools (not OpenValue -
    // see their own doc comments), so they are not listed here, same
    // convention as every other CHOSEN plain-default field in this file
    // (e.g. pauseMenuSeenDisplayCalScreen_ above).
    value("ranking0608", saveSystemUi_.slotCount);
    map("ranking0608", pcuCategoryTable_.kindByIndex);
    map("ranking0608", pcuCatalogOutfits_.flagsByIndex);

    // --- Batch 2026-10-02 (resumed session), spec-lua-api-behaviour.md
    // Sec44/Sec45/Sec46 (ranking tranches 12-14). Per-character/per-vehicle
    // OpenValue fields (CharacterState::hiddenFlag, VehicleState::
    // tireDurability/tireDamageMultiplier/forceFlags1d7a/lightsForceFlags/
    // tireIndicatorObjectDisabled/seat0Occupied, CharacterState::
    // forceFlags1c98/flagsE4, etc.) are NOT centrally inventoried here,
    // matching this function's own existing precedent (see the
    // "batch3132" comment above) of only listing EngineState-level slots.
    value("batch444546", ambientGangSpawnEnabled_);
    value("batch444546", cellCameraEnabled_);
    value("batch444546", ambientCopSpawnEnabled_);
    value("batch444546", actionNodesShouldntFlee_);
    value("batch444546", actionNodesRestrictSpawning_);

    // --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
    // tranche 16") --- shopPurchase_.triggerResolves and
    // strongholdPurchaseUpgrade_.strongholdResolves are CHOSEN plain bools
    // (not OpenValue - see their own doc comments), so they are not listed
    // here, same convention as vcustPreview_.targetLive above.
    map("ranking16", spawnOverride_.categoryNameResolves);
    return out;
}

} // namespace sr3luahost
