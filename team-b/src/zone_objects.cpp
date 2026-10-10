#include "sr3zone/zone_objects.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace sr3zone {

namespace {

// Manual little-endian reads, bounds NOT checked by themselves - every call
// site below either (a) is inside locate()'s own scan loop, which already
// proved `pos + 8 <= size` before reading, or (b) follows an explicit bounds
// check against cznContent.size() in parseAt(). Matches zone_geometry.cpp's
// own rawReadU32LE, kept as a separate copy rather than shared, per this
// project's existing per-file convention for this exact helper.
uint32_t rawReadU32LE(const uint8_t* data, size_t pos) {
    return static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

uint16_t rawReadU16LE(const uint8_t* data, size_t pos) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[pos]) |
                                  (static_cast<uint16_t>(data[pos + 1]) << 8));
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Best-effort decode of one record's variable property tail - see
// ObjectRecord's own "tail-interior decode" comment in zone_objects.h for
// exactly what is and is not confirmed here, and in particular for the
// 4-byte entry-stride finding this function implements: spec-zone-data-
// format.md Sec11.7 confirms each entry's own 8-byte header shape
// (kind/size/id) but says nothing about how consecutive entries pack: this
// reader's own whole-population real-data run (tools/zone_objects_
// population.cpp, 2026-10-07, sr3_city_0.vpp_pc: 2,068 `.czn_pc` files,
// 603 with a located Objects section, 156,101 records) found the LITERAL
// `8 + size` stride tiles its own record's tail length exactly in only
// 58,146 / 156,101 (37.2%), while `alignUp(8 + size, 4)` tiles EXACTLY in
// 156,101 / 156,101 (100%, zero overruns, zero leftover bytes) - this is
// Team B's own empirical finding, not a restatement of anything Sec11.7
// states, and is reported to the orchestrator for Team A to fold into
// spec-zone-data-format.md directly rather than silently assumed to
// already be covered by it. Never throws: an overrun simply stops the
// walk early and sets tailOverrun (now expected to be a 0-count on real
// data, a deviation would itself be a finding worth surfacing).
void decodeTailEntries(ObjectRecord& rec) {
    const uint8_t* tail = rec.rawTail.data();
    const size_t tailLen = rec.rawTail.size();
    size_t pos = 0;
    while (pos + 8 <= tailLen) {
        uint16_t kind = rawReadU16LE(tail, pos + 0);
        uint16_t size = rawReadU16LE(tail, pos + 2);
        uint32_t id = rawReadU32LE(tail, pos + 4);
        size_t entryTotal = alignUp(8 + static_cast<size_t>(size), 4);
        if (pos + entryTotal > tailLen) {
            rec.tailOverrun = true;
            break;
        }
        ObjectPropertyEntry e;
        e.kind = kind;
        e.size = size;
        e.id = id;
        e.data.assign(tail + pos + 8, tail + pos + 8 + size); // padding bytes (if any) are not part of `data`
        rec.tailEntries.push_back(std::move(e));
        pos += entryTotal;
    }
    rec.tailBytesDecoded = pos;
    rec.tailExactFit = !rec.tailOverrun && (pos == tailLen);
}

// Sec11.7: "the record head's +0x18 'name offset' field is the byte
// offset, within the tail, where this specific property's data (a
// NUL-terminated string) begins." Read directly off rec.rawTail,
// independent of decodeTailEntries() above - this does not need the tail's
// generic entry list to be understood at all. Left unset (OPEN per-record,
// not thrown) if nameOffsetInTail is out of range or no NUL terminator is
// found before the tail's own end.
void decodeNameText(ObjectRecord& rec) {
    if (!rec.hasName) return;
    if (rec.nameOffsetInTail >= rec.rawTail.size()) return; // malformed - leave nameText empty
    const char* start = reinterpret_cast<const char*>(rec.rawTail.data()) + rec.nameOffsetInTail;
    size_t maxLen = rec.rawTail.size() - rec.nameOffsetInTail;
    size_t len = 0;
    while (len < maxLen && start[len] != '\0') ++len;
    rec.nameText = std::string(start, len); // truncated (no NUL found) is still reported, not rejected
}

} // namespace

uint32_t ClassHashBucket(uint32_t classHash, uint32_t modulus) {
    // spec-zone-data-format.md Sec11.5, `0x00dab2b0`: a Thomas-Wang-style
    // 32-bit integer avalanche mix, confirmed by instruction shape -
    //   shift-15/not/add, shift-10/xor, multiply-by-9 (`lea ecx,[ecx+ecx*8]`),
    //   shift-6/xor, shift-11/not/add, shift-16/xor
    // - then the remainder of dividing by `modulus` (an `idiv`). CONFIRMED
    // - disassembly, the function's full 24-instruction body.
    uint32_t a = classHash;
    a = a + (~(a << 15));
    a = a ^ (a >> 10);
    a = a + (a << 3); // + (a<<3) == a*9 overall, matching the disassembly's lea-based *9
    a = a ^ (a >> 6);
    a = a + (~(a << 11));
    a = a ^ (a >> 16);
    if (modulus == 0) return 0; // guard only; the real code always calls this with 0x1fe (510)
    return a % modulus;
}

// --- Sec11.7/Sec11.9 item 4 correction, 2026-10-07: 21 of 40 real
// class-name hashes named by literal string, see zone_objects.h's own
// NamedClassHash/KnownClassHashes/ClassNameForHash comments for exactly
// which 21 and why not all 40. Hashes computed via sr3save::nameHash(),
// never hand-transcribed. ---
const std::vector<NamedClassHash>& KnownClassHashes() {
    static const std::vector<NamedClassHash> table = {
        // name, populationCount (-1 == Sec11.7 names the class but gives no
        // count for it)
        {"general_mover", 114129},
        {"level_light", 52449},
        {"action_node_group", 33989},
        {"object_tree", 18775},
        {"effect_object", 17307},
        {"script_npc", 6788},
        {"Navpoint", 5982},
        {"occluder", 4076},
        {"cover_node", 12956},
        {"cutscene_slate_obj", 111},
        {"Trigger_Volume", 1656},
        {"Script_Group", -1},
        {"object_rig", -1},
        {"Script_Mover", -1},
        {"Script_Vehicle", -1},
        {"scripted_path", -1},
        {"door", -1},
        {"script_item", -1},
        {"light_list", -1},
        {"spawn_region", -1},
        {"interior_volume", -1},
    };
    return table;
}

std::optional<std::string> ClassNameForHash(uint32_t classHash) {
    for (const auto& entry : KnownClassHashes()) {
        if (sr3save::nameHash(entry.name) == classHash) return std::string(entry.name);
    }
    return std::nullopt;
}

// --- Sec11.7 correction, 2026-10-07: ctg_object_name's real on-disk id
// (0x355ef946, kCtgObjectNamePropertyId) and the new script_npc_*/
// script_vehicle_* spawn-override property family, Sec11.9 item 5. ---

const ObjectPropertyEntry* FindTailEntryById(const ObjectRecord& rec, uint32_t propertyId) {
    for (const auto& e : rec.tailEntries) {
        if (e.id == propertyId) return &e;
    }
    return nullptr;
}

std::optional<uint64_t> DecodePropertyAsUnsignedInt(const ObjectPropertyEntry& entry) {
    const size_t n = entry.data.size();
    if (n != 1 && n != 2 && n != 4 && n != 8) return std::nullopt; // not a plain machine-integer width
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        v |= static_cast<uint64_t>(entry.data[i]) << (8 * i); // little-endian, matches every other
                                                                // multi-byte field this reader decodes
    }
    return v;
}

std::string DecodePropertyAsText(const ObjectPropertyEntry& entry) {
    size_t len = entry.data.size();
    if (len > 0 && entry.data[len - 1] == 0x00) --len; // drop exactly one trailing NUL, if present
    return std::string(reinterpret_cast<const char*>(entry.data.data()), len);
}

uint32_t ScriptNpcHpPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_npc_hp");
    return id;
}
uint32_t ScriptNpcFlagsPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_npc_flags");
    return id;
}
uint32_t ScriptNpcTeamPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_npc_team");
    return id;
}
uint32_t ScriptVehicleHpPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_vehicle_hp");
    return id;
}
uint32_t ScriptVehicleFlagsPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_vehicle_flags");
    return id;
}
uint32_t ScriptVehicleMpTeamPropertyId() {
    static const uint32_t id = sr3save::nameHash("script_vehicle_mp_team");
    return id;
}
uint32_t ScriptVehicleTypeNamePropertyId() {
    static const uint32_t id = sr3save::nameHash("script_vehicle_type_name");
    return id;
}
uint32_t ScriptVehicleVariantNamePropertyId() {
    static const uint32_t id = sr3save::nameHash("script_vehicle_variant_name");
    return id;
}

std::optional<uint64_t> ScriptNpcHp(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptNpcHpPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsUnsignedInt(*e);
}
std::optional<std::string> ScriptNpcFlags(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptNpcFlagsPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<std::string> ScriptNpcTeam(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptNpcTeamPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<uint64_t> ScriptVehicleHp(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptVehicleHpPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsUnsignedInt(*e);
}
std::optional<std::string> ScriptVehicleFlags(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptVehicleFlagsPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<std::string> ScriptVehicleMpTeam(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptVehicleMpTeamPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<std::string> ScriptVehicleTypeName(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptVehicleTypeNamePropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<std::string> ScriptVehicleVariantName(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, ScriptVehicleVariantNamePropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}

// --- Sec11.7's position/transform property and Navpoint's navpoint_flags,
// added 2026-10-08 for the Navpoint/cover_node movement-target-selection
// unit (spec-ai-runtime.md Sec15) - see zone_objects.h's own comment. ---

std::optional<Position3f> DecodePropertyAsPosition(const ObjectPropertyEntry& entry) {
    const size_t n = entry.data.size();
    if (n != 12 && n != 28) return std::nullopt; // kind=2 (XYZ only) or kind=3 (XYZ+quat), Sec11.7
    Position3f p;
    std::memcpy(&p.x, entry.data.data() + 0, 4);
    std::memcpy(&p.y, entry.data.data() + 4, 4);
    std::memcpy(&p.z, entry.data.data() + 8, 4);
    // The trailing 16 bytes of a kind=3 entry (a 4-float quaternion) are
    // deliberately not decoded - see this function's own header comment.
    return p;
}

std::optional<Position3f> ObjectPosition(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, kPositionPropertyId);
    if (!e) return std::nullopt;
    return DecodePropertyAsPosition(*e);
}

uint32_t NavpointFlagsPropertyId() {
    static const uint32_t id = sr3save::nameHash("navpoint_flags");
    return id;
}
std::optional<uint64_t> NavpointFlags(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, NavpointFlagsPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsUnsignedInt(*e);
}

// --- Trigger_Volume on-disk shape/extent/radius (spec-lua-api-behaviour.md
// Sec1.14 "On-disk gaps - RESOLVED 2026-10-10" / spec-zone-data-format.md
// Sec11.7 "A third class resolved, 2026-10-10"), added 2026-10-10. ---

uint32_t TriggerShapeTypePropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_shape_type");
    return id;
}
uint32_t TriggerShapeParam1PropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_shape_param1");
    return id;
}
uint32_t TriggerShapeParam2PropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_shape_param2");
    return id;
}
uint32_t TriggerFlagsPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_flags");
    return id;
}
uint32_t TriggerActionPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_action");
    return id;
}
uint32_t TriggerActionParamPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_action_param");
    return id;
}
uint32_t TriggerActionParam2PropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_action_param_2");
    return id;
}
uint32_t TriggerUseMessagePropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_use_message");
    return id;
}
uint32_t TriggerMaxFiresPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_max_fires");
    return id;
}
uint32_t TriggerDelayPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_delay");
    return id;
}
uint32_t TriggerSyncPropertyId() {
    static const uint32_t id = sr3save::nameHash("trigger_sync");
    return id;
}

std::optional<float> DecodePropertyAsFloat(const ObjectPropertyEntry& entry) {
    if (entry.data.size() != 4) return std::nullopt; // not a plain float32 width
    float v;
    std::memcpy(&v, entry.data.data(), 4);
    return v;
}

std::optional<Position3f> DecodePropertyAsVec3(const ObjectPropertyEntry& entry) {
    if (entry.data.size() != 12) return std::nullopt; // plain X/Y/Z float32 triple only
    Position3f p;
    std::memcpy(&p.x, entry.data.data() + 0, 4);
    std::memcpy(&p.y, entry.data.data() + 4, 4);
    std::memcpy(&p.z, entry.data.data() + 8, 4);
    return p;
}

std::optional<std::string> TriggerShapeType(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, TriggerShapeTypePropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
const ObjectPropertyEntry* TriggerShapeParam1(const ObjectRecord& rec) {
    return FindTailEntryById(rec, TriggerShapeParam1PropertyId());
}
const ObjectPropertyEntry* TriggerShapeParam2(const ObjectRecord& rec) {
    return FindTailEntryById(rec, TriggerShapeParam2PropertyId());
}
std::optional<std::string> TriggerActionParam(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, TriggerActionParamPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsText(*e);
}
std::optional<uint64_t> TriggerMaxFires(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, TriggerMaxFiresPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsUnsignedInt(*e);
}
std::optional<uint64_t> TriggerDelay(const ObjectRecord& rec) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, TriggerDelayPropertyId());
    if (!e) return std::nullopt;
    return DecodePropertyAsUnsignedInt(*e);
}

// Empirical mapping (2026-10-10, tools/zone_object_resolver_probe.cpp's own
// trigger-shape population scan against real sr3_city_0.vpp_pc data: 1,277
// real Trigger_Volume records, 0 missing trigger_shape_type, 0 unmapped).
// Exactly three distinct on-disk strings were observed: "sphere" (118),
// "cylinder" (254), and "bounding box" (905 - not the bare word "box", which
// is why this is a SUBSTRING test rather than an exact match). A
// case-insensitive compare is used defensively - Sec1.14 does not state the
// real lookup's own case sensitivity, so this reader is deliberately lenient
// rather than silently rejecting a same-spelling-different-case real value.
std::optional<int> TriggerShapeCodeForTypeString(const std::string& shapeTypeText) {
    std::string lower = shapeTypeText;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("box") != std::string::npos) return 0;
    if (lower.find("sphere") != std::string::npos) return 1;
    if (lower.find("cylinder") != std::string::npos) return 2;
    return std::nullopt;
}

ObjectsSection ZoneObjects::parseAt(ByteView cznContent, size_t payloadOffset) {
    ObjectsSection sec;
    sec.payloadOffset = payloadOffset;

    uint32_t magic = cznContent.readU32LE(payloadOffset + 0x00);
    if (magic != kObjectsPayloadMagic) {
        throw FormatError("bad Objects-section payload magic at offset " +
                          std::to_string(payloadOffset) +
                          ": expected 'FBOW' (spec-zone-data-format.md Sec11.3)");
    }
    sec.version = cznContent.readU32LE(payloadOffset + 0x04);
    if (sec.version != kObjectsPayloadVersion) {
        throw FormatError("Objects-section payload version " + std::to_string(sec.version) +
                          " != 5 at offset " + std::to_string(payloadOffset) +
                          " (spec-zone-data-format.md Sec11.3)");
    }
    sec.recordCount = cznContent.readU32LE(payloadOffset + 0x08);
    sec.directoryEntryCount = cznContent.readU32LE(payloadOffset + 0x0c);

    // --- directory array: align4(payloadOffset + 0x20), then
    // directoryEntryCount x 8 bytes (Sec11.3). ---
    sec.directoryOffset = alignUp(payloadOffset + kObjectsFixedHeaderSize, 4);
    const size_t directoryBytes =
        static_cast<size_t>(sec.directoryEntryCount) * kObjectDirectoryEntrySize;
    if (sec.directoryOffset + directoryBytes > cznContent.size()) {
        throw FormatError("Objects-section directory array (count=" +
                          std::to_string(sec.directoryEntryCount) + ") at offset " +
                          std::to_string(sec.directoryOffset) +
                          " runs past end of buffer (spec-zone-data-format.md Sec11.3)");
    }
    sec.directory.reserve(sec.directoryEntryCount);
    for (uint32_t i = 0; i < sec.directoryEntryCount; ++i) {
        const size_t at = sec.directoryOffset + static_cast<size_t>(i) * kObjectDirectoryEntrySize;
        ObjectDirectoryEntry e;
        e.handleLo = cznContent.readU32LE(at + 0);
        e.handleHi = cznContent.readU32LE(at + 4);
        sec.directory.push_back(e);
    }

    // --- per-object record stream: re-align to 4 after the directory
    // array (Sec11.3: "After skipping count x 8 bytes of directory entries
    // and re-aligning to 4..."), then recordCount records, each
    // 0x1c + its own tailLength bytes, each followed by an align-4 step
    // before the next (Sec11.4: 0x00da9210 AlignTo(4), called after every
    // per-object seek). ---
    sec.recordStreamOffset = alignUp(sec.directoryOffset + directoryBytes, 4);

    size_t cursor = sec.recordStreamOffset;
    sec.records.reserve(sec.recordCount);
    sec.complete = true;
    for (uint32_t i = 0; i < sec.recordCount; ++i) {
        if (cursor + kObjectRecordHeadSize > cznContent.size()) {
            sec.complete = false;
            break;
        }

        ObjectRecord rec;
        rec.recordOffset = cursor;
        rec.handleLo = cznContent.readU32LE(cursor + 0x00);
        rec.handleHi = cznContent.readU32LE(cursor + 0x04);
        rec.ownerHandleLo = cznContent.readU32LE(cursor + 0x08);
        rec.ownerHandleHi = cznContent.readU32LE(cursor + 0x0c);
        rec.classHash = cznContent.readU32LE(cursor + 0x10);
        rec.fieldAt0x14 = cznContent.readU16LE(cursor + 0x14);
        rec.tailLength = cznContent.readU16LE(cursor + 0x16);
        rec.nameOffsetInTail = cznContent.readU16LE(cursor + 0x18);

        rec.classHashBucket = ClassHashBucket(rec.classHash);
        rec.hasName = rec.nameOffsetInTail != 0;
        rec.tailOffset = cursor + kObjectRecordHeadSize;

        const size_t tailEnd = rec.tailOffset + rec.tailLength;
        if (tailEnd > cznContent.size()) {
            sec.complete = false;
            break;
        }
        rec.rawTail.assign(cznContent.data() + rec.tailOffset,
                           cznContent.data() + rec.tailOffset + rec.tailLength);

        decodeNameText(rec);
        decodeTailEntries(rec);

        // Sec11.4's exact formula: recordSize = 0x1c + (word at +0x16).
        const size_t recordSize = kObjectRecordHeadSize + rec.tailLength;
        sec.records.push_back(std::move(rec));
        cursor = alignUp(cursor + recordSize, 4);
    }
    if (sec.records.size() != static_cast<size_t>(sec.recordCount)) sec.complete = false;

    return sec;
}

std::vector<ObjectsSection> ZoneObjects::locate(ByteView cznContent) {
    std::vector<ObjectsSection> out;

    const uint8_t* data = cznContent.data();
    const size_t size = cznContent.size();
    if (data == nullptr || size < 8) return out;

    // 4-aligned scan for the FBOW/version-5 signature - see the file-level
    // comment in zone_objects.h for why this reader locates candidates this
    // way instead of walking the top-level {tag,length} chain.
    for (size_t pos = 0; pos + 8 <= size; pos += 4) {
        if (rawReadU32LE(data, pos) != kObjectsPayloadMagic) continue;
        if (rawReadU32LE(data, pos + 4) != kObjectsPayloadVersion) continue;

        try {
            ObjectsSection sec = ZoneObjects::parseAt(cznContent, pos);
            out.push_back(std::move(sec));
        } catch (const std::exception&) {
            // Coincidental magic+version at this offset, or a declared
            // count that does not fit the buffer - not a real Objects
            // payload at this position. Skip, exactly as ZoneGeometry
            // skips a coincidental version-9 word that fails MeshBlock's
            // own invariants.
        }
    }

    return out;
}

} // namespace sr3zone
