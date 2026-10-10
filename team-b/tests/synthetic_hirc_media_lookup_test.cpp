// Synthetic tests for the HIRC walker and Sound(kind 2) source-descriptor
// parser (sr3audio/hirc.h), covering spec-audio-runtime.md Sec8 (the HIRC
// chunk/item framing and the real per-kind dispatch table), Sec9.7 (the
// real kind 2/3/4 class identities), and Sec9.8 (the Sound object's own
// on-disk source descriptor and its worked example against real shipped
// data).
//
// FIXTURES ARE SYNTHETIC, HAND-BUILT DIRECTLY FROM THE SPEC'S OWN
// DOCUMENTED BYTE LAYOUT - this project has no real `.bnk_pc`/`_media.bnk_pc`
// fixture anywhere in its tree (tests/golden/ holds unrelated mesh/scene
// fixtures only; checked directly before writing this file) and its
// existing audio tests (tests/synthetic_media_bank_test.cpp) already
// establish the project's own standing convention of building fixtures
// straight from spec prose rather than from a real shipped archive - this
// file follows that same convention, not a new one.
//
// THE WORKED-EXAMPLE TEST (testResolveSoundIdToMedia_WorkedExample) is NOT
// invented: every id in it is quoted verbatim from spec-audio-runtime.md
// Sec9.8's own real worked example against real shipped data (bank pair
// `sh04.bnk_pc`/`sh04_media.bnk_pc`, cross-reference id `0xd7fbe8e8`) -
// Sound item id `0x297d8b3b`, decoded source/media id (R3) `0x3920cac1`,
// resolving to media record index 3 whose own `tag` is `0x3920cac1`. Only
// the surrounding byte LAYOUT (chunk sizes, the other fields' values, the
// other 3 records' offsets) is hand-built, per this file's own fixture
// policy above, since the real file bytes are not available to reconstruct
// byte-for-byte.

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3audio/hirc.h"
#include "sr3audio/media_bank.h"
#include "sr3audio/wwise_bank_id.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                   \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const std::exception&) { threw = true; }                     \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    size_t at = b.size();
    b.resize(at + 4);
    putU32At(b, at, v);
}

void appendBytes(std::vector<uint8_t>& b, const std::vector<uint8_t>& bytes) {
    b.insert(b.end(), bytes.begin(), bytes.end());
}

sr3audio::ByteView view(const std::vector<uint8_t>& b) {
    return sr3audio::ByteView(b.data(), b.size());
}

// --- Generic {FourCC, u32 size, payload} chunk framing (spec-audio-format.md
// Sec3's public chunk shape) ---
void appendChunk(std::vector<uint8_t>& b, const char tag[4], const std::vector<uint8_t>& payload) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(tag[i]));
    appendU32(b, static_cast<uint32_t>(payload.size()));
    appendBytes(b, payload);
}

// --- One HIRC item: {u8 kind, u32 length, u32 id, bodyBytes} where
// length == 4 + bodyBytes.size() (spec-audio-runtime.md Sec8.2.1/Sec8.2.3).
void appendHircItem(std::vector<uint8_t>& b, uint8_t kind, uint32_t id,
                    const std::vector<uint8_t>& bodyBytes) {
    b.push_back(kind);
    appendU32(b, static_cast<uint32_t>(4 + bodyBytes.size()));
    appendU32(b, id);
    appendBytes(b, bodyBytes);
}

// --- A kind-2 Sound item's own body: 4 skipped (OPEN) bytes, then the
// shared source-descriptor byte stream (Sec9.8): R1,R2,R3,R4,[R5,R6],B1,
// [R8,R9,blob].
std::vector<uint8_t> buildSoundBody(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4,
                                    uint8_t b1, bool withPrefetch, uint32_t r5, uint32_t r6,
                                    bool withBlob, uint32_t r8BlobSize, uint32_t r9Companion,
                                    const std::vector<uint8_t>& blobBytes) {
    std::vector<uint8_t> body;
    appendU32(body, 0xCCCCCCCCu); // the 4 skipped (OPEN-role) bytes - arbitrary marker value
    appendU32(body, r1);
    appendU32(body, r2);
    appendU32(body, r3);
    appendU32(body, r4);
    if (withPrefetch) {
        appendU32(body, r5);
        appendU32(body, r6);
    }
    body.push_back(b1);
    if (withBlob) {
        appendU32(body, r8BlobSize);
        appendU32(body, r9Companion);
        appendBytes(body, blobBytes);
    }
    return body;
}

// --- `_media.bnk_pc` wrapper builder, restated here (not shared with
// tests/synthetic_media_bank_test.cpp on purpose - same "build fixtures
// from spec prose, never from the reader" discipline that file documents).
struct SynthMediaRecord {
    uint32_t offset = 0;
    uint32_t extra = 0;
    uint32_t size = 0;
    uint32_t tag = 0;
};

std::vector<uint8_t> buildMediaBank(uint32_t crossReferenceId, const std::vector<SynthMediaRecord>& recs,
                                    size_t totalBytes) {
    std::vector<uint8_t> b(totalBytes, 0x00);
    const uint8_t magic[8] = {0x56, 0x57, 0x53, 0x42, 0x50, 0x43, 0x20, 0x20};
    for (size_t i = 0; i < 8; ++i) b[i] = magic[i];
    const uint8_t constant[8] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
    for (size_t i = 0; i < 8; ++i) b[0x08 + i] = constant[i];
    putU32At(b, 0x10, crossReferenceId);
    putU32At(b, 0x14, 0);
    putU32At(b, 0x18, static_cast<uint32_t>(recs.size()));
    putU32At(b, 0x1C, 0);
    for (size_t i = 0; i < recs.size(); ++i) {
        size_t at = 0x20 + i * 16;
        putU32At(b, at + 0x00, recs[i].offset);
        putU32At(b, at + 0x04, recs[i].extra);
        putU32At(b, at + 0x08, recs[i].size);
        putU32At(b, at + 0x0C, recs[i].tag);
    }
    return b;
}

// ============================================================
// Part A: findHircChunk() - the generic chunk-list walk.
// ============================================================

void testFindHircChunkLocatesAfterOtherChunks() {
    std::vector<uint8_t> bank;
    appendChunk(bank, "BKHD", {0x35, 0, 0, 0, 0xEA, 0x82, 0xE1, 0x40}); // 8-byte BKHD payload (genVersion, soundBankId)
    appendChunk(bank, "STID", {0x01, 0x02, 0x03, 0x04}); // an intervening chunk, must be SKIPPED, not interpreted
    std::vector<uint8_t> hircPayload = {0, 0, 0, 0}; // numItems = 0, a minimal but valid HIRC payload
    appendChunk(bank, "HIRC", hircPayload);
    appendChunk(bank, "DATA", {0xAA}); // a trailing chunk after HIRC - must not be required to find it

    std::optional<sr3audio::ByteView> found = sr3audio::findHircChunk(view(bank));
    CHECK(found.has_value());
    CHECK(found->size() == 4u);
    CHECK(found->readU32LE(0) == 0u);
}

void testFindHircChunkReturnsNulloptWhenAbsent() {
    std::vector<uint8_t> bank;
    appendChunk(bank, "BKHD", {0x35, 0, 0, 0, 0xEA, 0x82, 0xE1, 0x40});
    appendChunk(bank, "STID", {0x01});
    std::optional<sr3audio::ByteView> found = sr3audio::findHircChunk(view(bank));
    CHECK(!found.has_value());
}

void testFindHircChunkRejectsOverrunDeclaredSize() {
    std::vector<uint8_t> bank;
    // A chunk declaring a payload size larger than the buffer actually has.
    for (int i = 0; i < 4; ++i) bank.push_back(static_cast<uint8_t>(std::string("BKHD")[i]));
    appendU32(bank, 1000); // declared size, way past the real buffer
    bank.push_back(0x00);
    CHECK_THROWS(sr3audio::findHircChunk(view(bank)));
}

// ============================================================
// Part B: walkHircItems() - the per-item {kind,length,id,body} walk.
// ============================================================

void testWalkHircItemsZeroItems() {
    std::vector<uint8_t> payload = {0, 0, 0, 0}; // numItems = 0
    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(payload));
    CHECK(items.empty());
}

void testWalkHircItemsReadsKindIdAndBody() {
    std::vector<uint8_t> payload;
    appendU32(payload, 2); // numItems
    appendHircItem(payload, sr3audio::kHircKindEvent, 0x10000001u, {});
    appendHircItem(payload, sr3audio::kHircKindAction, 0x10000002u, {0xAB, 0xCD});

    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(payload));
    CHECK(items.size() == 2u);
    CHECK(items[0].kind == sr3audio::kHircKindEvent);
    CHECK(items[0].id == 0x10000001u);
    CHECK(items[0].body.size() == 0u);
    CHECK(items[1].kind == sr3audio::kHircKindAction);
    CHECK(items[1].id == 0x10000002u);
    CHECK(items[1].body.size() == 2u);
    CHECK(items[1].body.at(0) == 0xAB);
    CHECK(items[1].body.at(1) == 0xCD);
}

void testWalkHircItemsAdvancesPastUnrecognisedKind() {
    // Sec8.2.2: the dispatch loop advances past an item's declared size
    // regardless of whether the kind is one this file understands - an
    // out-of-[1,19]-range kind (0, here) must not block the walk from
    // reaching the item after it.
    std::vector<uint8_t> payload;
    appendU32(payload, 2);
    appendHircItem(payload, 0, 0x20000001u, {0x01, 0x02, 0x03});
    appendHircItem(payload, sr3audio::kHircKindEvent, 0x20000002u, {});

    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(payload));
    CHECK(items.size() == 2u);
    CHECK(items[0].kind == 0);
    CHECK(items[1].kind == sr3audio::kHircKindEvent);
    CHECK(items[1].id == 0x20000002u);
}

void testWalkHircItemsRejectsLengthTooShortForId() {
    std::vector<uint8_t> payload;
    appendU32(payload, 1);
    payload.push_back(sr3audio::kHircKindEvent);
    appendU32(payload, 2); // length 2 - too short to hold even the 4-byte id
    payload.push_back(0x00);
    payload.push_back(0x00);
    CHECK_THROWS(sr3audio::walkHircItems(view(payload)));
}

void testWalkHircItemsRejectsItemRunningPastChunkEnd() {
    std::vector<uint8_t> payload;
    appendU32(payload, 1);
    payload.push_back(sr3audio::kHircKindEvent);
    appendU32(payload, 100); // declares far more body than is actually present
    appendU32(payload, 0x30000001u);
    CHECK_THROWS(sr3audio::walkHircItems(view(payload)));
}

void testWalkHircItemsRejectsTruncatedHeader() {
    std::vector<uint8_t> payload;
    appendU32(payload, 1);
    payload.push_back(sr3audio::kHircKindEvent); // declares 1 item, but not even its length field fits
    CHECK_THROWS(sr3audio::walkHircItems(view(payload)));
}

void testWalkHircItemsRejectsTooSmallForOwnCount() {
    std::vector<uint8_t> payload = {0x01, 0x00}; // not even 4 bytes for numItems
    CHECK_THROWS(sr3audio::walkHircItems(view(payload)));
}

// ============================================================
// Part C: kind-range helpers (Sec8.2.1/Sec8.6).
// ============================================================

void testUnregisteredFallbackRangeBoundaries() {
    CHECK(!sr3audio::isHircKindInUnregisteredFallbackRange(9));
    CHECK(sr3audio::isHircKindInUnregisteredFallbackRange(10));
    CHECK(sr3audio::isHircKindInUnregisteredFallbackRange(11));
    CHECK(sr3audio::isHircKindInUnregisteredFallbackRange(12));
    CHECK(sr3audio::isHircKindInUnregisteredFallbackRange(13));
    CHECK(!sr3audio::isHircKindInUnregisteredFallbackRange(14));
    CHECK(!sr3audio::isHircKindInUnregisteredFallbackRange(2)); // the dominant Sound kind is NOT in this range
    CHECK(!sr3audio::isHircKindInUnregisteredFallbackRange(3));
}

// ============================================================
// Part D: SoundSourceDescriptor parsing (Sec9.8).
// ============================================================

void testClassifySoundSourcePlugin() {
    sr3audio::SoundSourceDescriptor d;
    d.pluginOrCodecTypeId = 0x00000000u;
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kNone);
    d.pluginOrCodecTypeId = 0x00000001u;
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kPlainCodec);
    d.pluginOrCodecTypeId = 0x00000012u; // low nibble 2
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kEmbeddedParamBlob);
    d.pluginOrCodecTypeId = 0x00000025u; // low nibble 5
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kEmbeddedParamBlob);
    d.pluginOrCodecTypeId = 0x00000003u; // low nibble 3 - rejected
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kRejected);
    d.pluginOrCodecTypeId = 0x0000000Cu; // low nibble 0xC - rejected
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kRejected);
}

void testParseSoundSourceDescriptor_StreamingNoPrefetch() {
    // (R1&0xF)==1, R2==1: plain streaming codec/source, R5/R6 skipped.
    std::vector<uint8_t> bytes;
    appendU32(bytes, 0x00000001u); // R1
    appendU32(bytes, 0x00000001u); // R2 == 1 -> no prefetch fields
    appendU32(bytes, 0x3920CAC1u); // R3 - the spec's own worked-example source id
    appendU32(bytes, 0x00001234u); // R4
    bytes.push_back(0x07);         // B1

    sr3audio::SoundSourceDescriptor d = sr3audio::parseSoundSourceDescriptor(view(bytes));
    CHECK(d.pluginOrCodecTypeId == 0x00000001u);
    CHECK(d.streamTypeSelector == 1u);
    CHECK(d.sourceMediaId == 0x3920CAC1u);
    CHECK(d.sizeOrCompanionId == 0x00001234u);
    CHECK(!d.hasPrefetchFields);
    CHECK(d.sourceFlags == 0x07);
    CHECK(!d.hasEmbeddedParamBlob);
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kPlainCodec);
}

void testParseSoundSourceDescriptor_ResidentWithPrefetch() {
    // (R1&0xF)==1, R2==0 (resident) and R2==2 (streamed-with-prefetch) both
    // read R5/R6 - Sec9.8: "R2==0/R2==2 both read it".
    for (uint32_t r2 : {0u, 2u}) {
        std::vector<uint8_t> bytes;
        appendU32(bytes, 0x00000001u); // R1
        appendU32(bytes, r2);          // R2
        appendU32(bytes, 0x11111111u); // R3
        appendU32(bytes, 0x22222222u); // R4
        appendU32(bytes, 0x33333333u); // R5
        appendU32(bytes, 0x44444444u); // R6
        bytes.push_back(0x09);         // B1

        sr3audio::SoundSourceDescriptor d = sr3audio::parseSoundSourceDescriptor(view(bytes));
        CHECK(d.hasPrefetchFields);
        CHECK(d.extraField1 == 0x33333333u);
        CHECK(d.extraField2 == 0x44444444u);
        CHECK(d.sourceFlags == 0x09);
    }
}

void testParseSoundSourceDescriptor_EmbeddedParamBlobSkipsBlobExactly() {
    std::vector<uint8_t> bytes;
    appendU32(bytes, 0x00000002u); // R1, low nibble 2 -> embedded param blob
    appendU32(bytes, 0x00000001u); // R2 == 1 -> no prefetch fields
    appendU32(bytes, 0x55555555u); // R3
    appendU32(bytes, 0x66666666u); // R4
    bytes.push_back(0x00);         // B1
    appendU32(bytes, 6);           // R8: blob size
    appendU32(bytes, 0x77777777u); // R9: companion
    appendBytes(bytes, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}); // exactly 6 blob bytes, nothing more

    sr3audio::SoundSourceDescriptor d = sr3audio::parseSoundSourceDescriptor(view(bytes));
    CHECK(d.hasEmbeddedParamBlob);
    CHECK(d.paramBlobSize == 6u);
    CHECK(d.paramBlobCompanion == 0x77777777u);
    CHECK(sr3audio::classifySoundSourcePlugin(d) == sr3audio::SoundSourcePluginKind::kEmbeddedParamBlob);
}

void testParseSoundSourceDescriptor_EmbeddedParamBlobTruncatedThrows() {
    std::vector<uint8_t> bytes;
    appendU32(bytes, 0x00000005u); // R1, low nibble 5 -> embedded param blob
    appendU32(bytes, 0x00000001u); // R2 == 1
    appendU32(bytes, 0x55555555u);
    appendU32(bytes, 0x66666666u);
    bytes.push_back(0x00);
    appendU32(bytes, 6);           // declares a 6-byte blob
    appendU32(bytes, 0x77777777u);
    appendBytes(bytes, {0x01, 0x02, 0x03}); // only 3 of the declared 6 blob bytes present

    CHECK_THROWS(sr3audio::parseSoundSourceDescriptor(view(bytes)));
}

void testParseSoundSourceDescriptor_TruncatedBeforeB1Throws() {
    std::vector<uint8_t> bytes;
    appendU32(bytes, 0x00000001u);
    appendU32(bytes, 0x00000001u); // R2==1, no prefetch
    appendU32(bytes, 0x55555555u);
    appendU32(bytes, 0x66666666u);
    // B1 byte missing entirely.
    CHECK_THROWS(sr3audio::parseSoundSourceDescriptor(view(bytes)));
}

// ============================================================
// Part E: parseSoundHircItem() - the id-skip + descriptor parse together.
// ============================================================

void testParseSoundHircItemSkipsFourBytesPastId() {
    std::vector<uint8_t> body = buildSoundBody(/*r1=*/1, /*r2=*/1, /*r3=*/0xABCDEF01u,
                                               /*r4=*/0x12345678u, /*b1=*/0x05,
                                               /*withPrefetch=*/false, 0, 0,
                                               /*withBlob=*/false, 0, 0, {});
    sr3audio::HircItem item;
    item.kind = sr3audio::kHircKindSound;
    item.id = 0x99999999u;
    item.body = view(body);

    sr3audio::SoundSourceDescriptor d = sr3audio::parseSoundHircItem(item);
    CHECK(d.sourceMediaId == 0xABCDEF01u);
    CHECK(d.sizeOrCompanionId == 0x12345678u);
}

void testParseSoundHircItemRejectsWrongKind() {
    sr3audio::HircItem item;
    item.kind = sr3audio::kHircKindAction;
    item.id = 1;
    std::vector<uint8_t> body(8, 0x00);
    item.body = view(body);
    CHECK_THROWS(sr3audio::parseSoundHircItem(item));
}

void testParseSoundHircItemRejectsTooShortBody() {
    sr3audio::HircItem item;
    item.kind = sr3audio::kHircKindSound;
    item.id = 1;
    std::vector<uint8_t> body(3, 0x00); // < 4 skipped bytes
    item.body = view(body);
    CHECK_THROWS(sr3audio::parseSoundHircItem(item));
}

// ============================================================
// Part F: resolveSoundIdToMedia() - the full id -> media lookup, including
// THE WORKED EXAMPLE (spec-audio-runtime.md Sec9.8, 2026-10-09).
// ============================================================

void testResolveSoundIdToMedia_WorkedExample() {
    // --- The companion `_media.bnk_pc` wrapper: 4 chain-valid records,
    // record index 3 carrying the spec's own real `tag` value. ---
    const uint32_t kSpecTag = 0x3920CAC1u;      // spec's own real R3/tag value
    const uint32_t kExpectedOffset = 0x2000u;    // hand-built, but chain-consistent (see below)
    const uint32_t kExpectedSize = 0x1234u;

    std::vector<SynthMediaRecord> recs = {
        {0x800, 0, 0x400, 0x11110000u},
        {0x1000, 0, 0x400, 0x22220000u},
        {0x1800, 0, 0x400, 0x33330000u},
        {kExpectedOffset, 0, kExpectedSize, kSpecTag}, // record index 3 - matches the spec's own tag
    };
    const size_t kTotal = kExpectedOffset + 0x1800; // round_up(0x1234, 0x800) == 0x1800 (chain rule)
    std::vector<uint8_t> mediaBankBytes =
        buildMediaBank(/*crossReferenceId=*/0xD7FBE8E8u /* spec's own worked-example id */, recs, kTotal);

    sr3audio::MediaBank mediaBank = sr3audio::MediaBank::parse(view(mediaBankBytes));
    std::vector<sr3audio::MediaBankRecord> mediaRecords = mediaBank.walk(kTotal);
    CHECK(mediaRecords.size() == 4u);

    // --- The plain bank's own HIRC chunk, with the spec's real Sound item
    // id and its decoded R3 matching kSpecTag exactly. ---
    const uint32_t kSoundId = 0x297D8B3Bu; // spec's own real worked-example Sound item id

    std::vector<uint8_t> hircPayload;
    appendU32(hircPayload, 4); // numItems
    appendHircItem(hircPayload, sr3audio::kHircKindEvent, 0x10000001u, {});
    appendHircItem(hircPayload, sr3audio::kHircKindAction, 0x10000002u, {0x01, 0x02});
    appendHircItem(hircPayload, sr3audio::kHircKindSound, kSoundId,
                   buildSoundBody(/*r1=*/1, /*r2=*/1, /*r3=*/kSpecTag, /*r4=*/0x00001234u,
                                  /*b1=*/0x03, false, 0, 0, false, 0, 0, {}));
    appendHircItem(hircPayload, 19, 0x10000004u, {0xAA, 0xBB, 0xCC});

    std::vector<uint8_t> bank;
    appendChunk(bank, "BKHD", {0x35, 0, 0, 0, 0xE8, 0xE8, 0xFB, 0xD7}); // soundBankId 0xD7FBE8E8, little-endian
    appendChunk(bank, "STID", {0xDE, 0xAD});
    appendChunk(bank, "HIRC", hircPayload);

    // Bonus cross-check reusing the EXISTING cross-reference mechanism
    // (spec-audio-format.md Sec5) - both halves of the spec's own worked
    // example (sh04.bnk_pc / sh04_media.bnk_pc) really do share one id.
    sr3audio::WwiseBankHeaderPrefix prefix = sr3audio::readWwiseBankHeaderPrefix(view(bank));
    CHECK(prefix.soundBankId == mediaBank.crossReferenceId());

    std::optional<sr3audio::ByteView> hircChunk = sr3audio::findHircChunk(view(bank));
    CHECK(hircChunk.has_value());
    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(*hircChunk);
    CHECK(items.size() == 4u);

    // --- The lookup itself: Sound item id -> R3 -> media record by tag. ---
    sr3audio::ResolvedMedia resolved = sr3audio::resolveSoundIdToMedia(items, kSoundId, mediaRecords);
    CHECK(resolved.soundObjectId == kSoundId);
    CHECK(resolved.sourceMediaId == kSpecTag);
    CHECK(resolved.mediaRecordIndex == 3u); // the spec's own "media record idx 3"
    CHECK(resolved.offset == kExpectedOffset);
    CHECK(resolved.size == kExpectedSize);
}

void testResolveSoundIdToMediaRejectsUnknownId() {
    std::vector<uint8_t> hircPayload;
    appendU32(hircPayload, 1);
    appendHircItem(hircPayload, sr3audio::kHircKindSound, 0x12345678u,
                   buildSoundBody(1, 1, 0xAAAAAAAAu, 0, 0, false, 0, 0, false, 0, 0, {}));
    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(hircPayload));

    std::vector<sr3audio::MediaBankRecord> records = {{0x800, 0, 0x400, 0xAAAAAAAAu}};
    CHECK_THROWS(sr3audio::resolveSoundIdToMedia(items, 0x99999999u /* not present */, records));
}

void testResolveSoundIdToMediaRejectsNonPlainCodecPlugin() {
    std::vector<uint8_t> hircPayload;
    appendU32(hircPayload, 1);
    // R1 low nibble 0 -> kNone (empty slot), not a resolvable embedded-media source.
    appendHircItem(hircPayload, sr3audio::kHircKindSound, 0x55555555u,
                   buildSoundBody(0, 1, 0xAAAAAAAAu, 0, 0, false, 0, 0, false, 0, 0, {}));
    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(hircPayload));

    std::vector<sr3audio::MediaBankRecord> records = {{0x800, 0, 0x400, 0xAAAAAAAAu}};
    CHECK_THROWS(sr3audio::resolveSoundIdToMedia(items, 0x55555555u, records));
}

void testResolveSoundIdToMediaRejectsNoMatchingTag() {
    std::vector<uint8_t> hircPayload;
    appendU32(hircPayload, 1);
    appendHircItem(hircPayload, sr3audio::kHircKindSound, 0x66666666u,
                   buildSoundBody(1, 1, 0xDEADBEEFu, 0, 0, false, 0, 0, false, 0, 0, {}));
    std::vector<sr3audio::HircItem> items = sr3audio::walkHircItems(view(hircPayload));

    // No record anywhere carries tag 0xDEADBEEF.
    std::vector<sr3audio::MediaBankRecord> records = {
        {0x800, 0, 0x400, 0x11111111u},
        {0x1000, 0, 0x400, 0x22222222u},
    };
    CHECK_THROWS(sr3audio::resolveSoundIdToMedia(items, 0x66666666u, records));
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    run("testFindHircChunkLocatesAfterOtherChunks", testFindHircChunkLocatesAfterOtherChunks);
    run("testFindHircChunkReturnsNulloptWhenAbsent", testFindHircChunkReturnsNulloptWhenAbsent);
    run("testFindHircChunkRejectsOverrunDeclaredSize", testFindHircChunkRejectsOverrunDeclaredSize);

    run("testWalkHircItemsZeroItems", testWalkHircItemsZeroItems);
    run("testWalkHircItemsReadsKindIdAndBody", testWalkHircItemsReadsKindIdAndBody);
    run("testWalkHircItemsAdvancesPastUnrecognisedKind", testWalkHircItemsAdvancesPastUnrecognisedKind);
    run("testWalkHircItemsRejectsLengthTooShortForId", testWalkHircItemsRejectsLengthTooShortForId);
    run("testWalkHircItemsRejectsItemRunningPastChunkEnd", testWalkHircItemsRejectsItemRunningPastChunkEnd);
    run("testWalkHircItemsRejectsTruncatedHeader", testWalkHircItemsRejectsTruncatedHeader);
    run("testWalkHircItemsRejectsTooSmallForOwnCount", testWalkHircItemsRejectsTooSmallForOwnCount);

    run("testUnregisteredFallbackRangeBoundaries", testUnregisteredFallbackRangeBoundaries);

    run("testClassifySoundSourcePlugin", testClassifySoundSourcePlugin);
    run("testParseSoundSourceDescriptor_StreamingNoPrefetch", testParseSoundSourceDescriptor_StreamingNoPrefetch);
    run("testParseSoundSourceDescriptor_ResidentWithPrefetch", testParseSoundSourceDescriptor_ResidentWithPrefetch);
    run("testParseSoundSourceDescriptor_EmbeddedParamBlobSkipsBlobExactly",
        testParseSoundSourceDescriptor_EmbeddedParamBlobSkipsBlobExactly);
    run("testParseSoundSourceDescriptor_EmbeddedParamBlobTruncatedThrows",
        testParseSoundSourceDescriptor_EmbeddedParamBlobTruncatedThrows);
    run("testParseSoundSourceDescriptor_TruncatedBeforeB1Throws",
        testParseSoundSourceDescriptor_TruncatedBeforeB1Throws);

    run("testParseSoundHircItemSkipsFourBytesPastId", testParseSoundHircItemSkipsFourBytesPastId);
    run("testParseSoundHircItemRejectsWrongKind", testParseSoundHircItemRejectsWrongKind);
    run("testParseSoundHircItemRejectsTooShortBody", testParseSoundHircItemRejectsTooShortBody);

    run("testResolveSoundIdToMedia_WorkedExample", testResolveSoundIdToMedia_WorkedExample);
    run("testResolveSoundIdToMediaRejectsUnknownId", testResolveSoundIdToMediaRejectsUnknownId);
    run("testResolveSoundIdToMediaRejectsNonPlainCodecPlugin", testResolveSoundIdToMediaRejectsNonPlainCodecPlugin);
    run("testResolveSoundIdToMediaRejectsNoMatchingTag", testResolveSoundIdToMediaRejectsNoMatchingTag);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic HIRC/Sound-media-lookup tests passed.\n";
    return 0;
}
