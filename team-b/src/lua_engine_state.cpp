#include "sr3luahost/engine_state.h"

#include <cctype>
#include <cstdio>
#include <limits>

namespace sr3luahost {

CharacterState& EngineState::getOrCreateCharacter(const std::string& name) {
    return characters_[name]; // operator[] default-constructs on first reference - the "a name not yet seen defaults to ..." behavior every CharacterState field doc-comments explain individually.
}

bool EngineState::hasCharacter(const std::string& name) const {
    return characters_.count(name) != 0;
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
    // answer depends on it. With no objects registered at all, "not found"
    // holds for every document, so it is not read.
    if (vdoObjects_.empty()) return 0;
    uint32_t targetDoc = hasDoc ? docHandle : currentDefaultDocHandle_.get();
    for (const auto& [h, obj] : vdoObjects_) {
        if (obj.docHandle == targetDoc && obj.nameHash == targetHash) return h;
    }
    return 0;
}

int EngineState::tutorialAdvanceCount(int index) const {
    auto it = tutorialAdvanceCounts_.find(index);
    return it == tutorialAdvanceCounts_.end() ? 0 : it->second;
}

void EngineState::recordTutorialAdvance(int index) {
    ++tutorialAdvanceCounts_[index];
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
    return out;
}

} // namespace sr3luahost
