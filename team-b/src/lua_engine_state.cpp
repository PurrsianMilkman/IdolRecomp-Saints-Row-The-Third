#include "sr3luahost/engine_state.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>

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
    return c;
}

bool EngineState::hasCharacter(const std::string& name) const {
    return characters_.count(name) != 0;
}

VehicleState& EngineState::getOrCreateVehicle(const std::string& name) {
    return vehicles_[name]; // same default-construct-on-first-reference convention as getOrCreateCharacter above
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

} // namespace

uint32_t EngineState::loadVintDocument(const std::string& docName, const sr3vintdoc::Document& doc,
                                       const std::string& activeResolution) {
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
        for (const auto& p : n.effectiveProperties(activeResolution)) {
            std::vector<VintTaggedValue> values = fileValueToTagged(p, doc);
            if (values.empty()) continue;
            vintProperties_[h][p.nameHash] = std::move(values);
            ++ld.propertyCount;
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

void EngineState::setVintProperty(uint32_t handle, const std::string& propertyName, std::vector<VintTaggedValue> values) {
    if (!vdoObjects_.count(handle)) return; // CONFIRMED (Sec20.1): an unresolvable handle simply fails to resolve later - silent no-op
    vintProperties_[handle][sr3save::nameHash(propertyName)] = std::move(values);
}

const std::vector<VintTaggedValue>* EngineState::findVintProperty(uint32_t handle, const std::string& propertyName) const {
    auto objIt = vintProperties_.find(handle);
    if (objIt == vintProperties_.end()) return nullptr; // CONFIRMED (Sec20.2): bad handle / never-set -> zero Lua values
    auto propIt = objIt->second.find(sr3save::nameHash(propertyName));
    if (propIt == objIt->second.end()) return nullptr; // CONFIRMED: property-name miss -> zero Lua values
    return &propIt->second;
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
