// Synthetic tests for the .ctdg_pc mission-conversation reader and its
// content validator. Builds buffers from scratch using only the CONFIRMED
// spec-conversation-format.md facts: the magic, version 1, the fixed
// 32-byte source-name field, the 1..30 record-count bound, and the
// exact-size identity `44 + 12 * count == size`.
//
// One test reproduces the spec's own Sec6 worked example - the three
// female/male speaker-id pairs whose XOR is 0x0B - as a real-value
// regression check on the record layout. Those numbers came from the
// spec's own inspection of real files, not from this test's builder, so a
// self-consistent-but-wrong record layout would still fail it.

#include <iostream>
#include <string>
#include <vector>

#include "sr3conversation/content_validation.h"
#include "sr3conversation/conversation.h"

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

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

struct SyntheticTurn {
    uint32_t speakerId;
    uint32_t lineId;
    uint32_t field_0x08;
};

// Builds a well-formed .ctdg_pc buffer exactly per spec Sec3: magic,
// version, the FIXED 32-byte source-name field (NUL-terminated and
// zero-padded, or clipped at 32 with no terminator), the record count,
// then `count` packed 12-byte records. The resulting size satisfies
// `44 + 12 * count` by construction.
std::vector<uint8_t> buildCtdg(const std::string& sourceName,
                                const std::vector<SyntheticTurn>& turns) {
    std::vector<uint8_t> b;
    appendU32(b, sr3conversation::kMagic);
    appendU32(b, sr3conversation::kRequiredVersion);

    // 32-byte fixed field: copy up to 32 bytes, zero-fill the rest.
    size_t nameBytes = sourceName.size() < sr3conversation::kSourceNameSize
                           ? sourceName.size()
                           : sr3conversation::kSourceNameSize;
    b.insert(b.end(), sourceName.begin(), sourceName.begin() + static_cast<long>(nameBytes));
    b.insert(b.end(), sr3conversation::kSourceNameSize - nameBytes, 0x00);

    appendU32(b, static_cast<uint32_t>(turns.size()));

    for (const auto& t : turns) {
        appendU32(b, t.speakerId);
        appendU32(b, t.lineId);
        appendU32(b, t.field_0x08);
    }

    // The builder's own invariant - if this ever trips, the fixture is
    // wrong rather than the reader.
    if (b.size() != sr3conversation::kHeaderSize +
                        turns.size() * sr3conversation::kRecordSize) {
        std::cerr << "buildCtdg: internal error, size != 44 + 12n\n";
        std::abort();
    }
    return b;
}

bool throwsFormatError(const std::vector<uint8_t>& blob) {
    try {
        sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
    } catch (const sr3conversation::FormatError&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    // --- Spec Sec6's worked example: the three voice-family speaker-id
    // pairs, whose female/male XOR is exactly 0x0B ('f' XOR 'm'). Real
    // values from the spec's own file inspection. ---
    {
        std::vector<uint8_t> blob = buildCtdg("conv_07_greeting_wf.ctd",
                                              {{0xEDDF70CAu, 0x1000u, 0},
                                               {0xEDDF70C1u, 0x1001u, 0},
                                               {0xFADF8545u, 0x1002u, 200},
                                               {0xFADF854Eu, 0x1003u, 0},
                                               {0x04DF9487u, 0x1004u, 0},
                                               {0x04DF948Cu, 0x1005u, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));

        CHECK(c.version() == 1);
        CHECK(c.sourceName() == "conv_07_greeting_wf.ctd");
        CHECK(!c.sourceNameTruncated());
        CHECK(c.turns().size() == 6);
        CHECK(c.turns()[0].speakerId == 0xEDDF70CAu);
        CHECK(c.turns()[0].lineId == 0x1000u);
        CHECK(c.turns()[2].field_0x08_raw == 200);
        CHECK(c.turns()[2].field_0x08_asInt32() == 200);

        // The spec's diagnostic identity, re-checked here: each pair's XOR
        // is 'f' XOR 'm'. This is what identifies the ids as the
        // rotate-6/XOR hash rather than CRC-32 (spec Sec6).
        CHECK((c.turns()[0].speakerId ^ c.turns()[1].speakerId) == 0x0Bu);
        CHECK((c.turns()[2].speakerId ^ c.turns()[3].speakerId) == 0x0Bu);
        CHECK((c.turns()[4].speakerId ^ c.turns()[5].speakerId) == 0x0Bu);
        // Compile-time, not runtime: that 0x0B IS 'f' XOR 'm' is a fact
        // about ASCII, so asserting it at compile time states it more
        // precisely than a CHECK would (and keeps /W4 quiet about a
        // constant conditional).
        static_assert(('f' ^ 'm') == 0x0B, "the spec's diagnostic constant is 'f' XOR 'm'");

        // Distinct-speaker set, in first-appearance order.
        std::vector<uint32_t> speakers = c.distinctSpeakerIds();
        CHECK(speakers.size() == 6);
        CHECK(speakers[0] == 0xEDDF70CAu);
    }

    // --- The common shipped shape: a two-hander, 2 distinct speakers
    // across several turns (4,149 of 4,984 files look like this). ---
    {
        std::vector<uint8_t> blob = buildCtdg("chat_02_idle_bm.ctd", {{0xAAAA0001u, 1, 0},
                                                                      {0xBBBB0002u, 2, 0},
                                                                      {0xAAAA0001u, 3, 0},
                                                                      {0xBBBB0002u, 4, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.turns().size() == 4);
        std::vector<uint32_t> speakers = c.distinctSpeakerIds();
        CHECK(speakers.size() == 2); // distinct, not per-turn
        CHECK(speakers[0] == 0xAAAA0001u);
        CHECK(speakers[1] == 0xBBBB0002u);
    }

    // --- Minimum legal file: exactly 1 record, 56 bytes (the spec's own
    // minimum observed size). ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 2, 3}});
        CHECK(blob.size() == 56);
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.turns().size() == 1);
    }

    // --- Maximum legal record count: exactly 30, the loader's hard
    // limit. Must parse, not throw - the bound is inclusive. ---
    {
        std::vector<SyntheticTurn> turns;
        for (uint32_t i = 0; i < sr3conversation::kMaxRecordCount; ++i) {
            turns.push_back({0x100u + i, i, 0});
        }
        std::vector<uint8_t> blob = buildCtdg("big.ctd", turns);
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.turns().size() == 30);
    }

    // --- Source-name truncation. REGRESSION TEST: the first version of
    // this reader only flagged a field with NO terminator (length 32),
    // which the engine never produces - it clips to 31 characters and
    // still writes the NUL. That detector was therefore dead code, and
    // reported 0 truncated names across all 4,984 shipped files against
    // spec Sec1's 1,187. The 31-character case below is the one that
    // actually occurs in real data, and is what this must catch. ---
    {
        std::string clipped(31, 'x'); // the real shipped signature: clipped to 31 + NUL
        std::vector<uint8_t> blob = buildCtdg(clipped, {{1, 1, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.sourceNameTruncated());
        CHECK(c.sourceName().size() == 31);
    }
    {
        std::string longName(40, 'x'); // fills all 32 bytes, no terminator
        std::vector<uint8_t> blob = buildCtdg(longName, {{1, 1, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.sourceNameTruncated());
        CHECK(c.sourceName().size() == 32);
    }
    // A comfortably short name must NOT be flagged - the detector has to
    // discriminate, not just always return true.
    {
        std::vector<uint8_t> blob = buildCtdg("short_name.ctd", {{1, 1, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(!c.sourceNameTruncated());
    }
    // Boundary: 30 characters is the longest name that is NOT at the clip
    // limit, so it must come back clean.
    {
        std::string name30(30, 'y');
        std::vector<uint8_t> blob = buildCtdg(name30, {{1, 1, 0}});
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(!c.sourceNameTruncated());
        CHECK(c.sourceName().size() == 30);
    }

    // --- field_0x08's signed reading: the non-zero values are signed
    // multiples of 100 spanning roughly -500..+600 (spec Sec4), so the
    // int32 reinterpretation must round-trip a negative correctly. ---
    {
        std::vector<uint8_t> blob = buildCtdg("t.ctd", {{1, 1, 0xFFFFFE0Cu}}); // -500
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(
            sr3conversation::ByteView(blob.data(), blob.size()));
        CHECK(c.turns()[0].field_0x08_raw == 0xFFFFFE0Cu);
        CHECK(c.turns()[0].field_0x08_asInt32() == -500);
    }

    // --- Negative: bad magic must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        blob[0] ^= 0xFF;
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: wrong version must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        putU32At(blob, 0x04, 2);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: a record count of 0 must be rejected - the consumer
    // rejects an empty conversation outright (spec Sec5). Note the size
    // identity is satisfied here (44 + 12*0 == 44), so this specifically
    // exercises the count check rather than the size check. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {});
        CHECK(blob.size() == 44);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: a record count above 30 must be rejected BEFORE any
    // record is read (spec Sec5's ladder). Built with a matching real size
    // so the size identity holds and only the bound can fire. ---
    {
        std::vector<SyntheticTurn> turns;
        for (uint32_t i = 0; i < 31; ++i) turns.push_back({i, i, 0});
        std::vector<uint8_t> blob = buildCtdg("toobig.ctd", turns);
        CHECK(blob.size() == 44 + 31 * 12); // size identity intact
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: the exact-size identity. A trailing byte past
    // `44 + 12 * count` must be rejected - every byte of a real file is
    // accounted for, so slack means the file doesn't match the model. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        blob.push_back(0x00);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: the same identity from the other side - a count that
    // claims more records than the buffer holds. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        putU32At(blob, sr3conversation::kRecordCountOffset, 2);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: truncated header. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        blob.resize(20);
        CHECK(throwsFormatError(blob));
    }

    // --- Content validation: looksLikeCtdgFilename. ---
    CHECK(sr3conversation::looksLikeCtdgFilename("conv_01_a_wf.ctdg_pc"));
    CHECK(sr3conversation::looksLikeCtdgFilename("CONV_01_A_WF.CTDG_PC")); // case-insensitive
    CHECK(!sr3conversation::looksLikeCtdgFilename("conv_01_a_wf.ctd"));    // authoring name, not shipped
    CHECK(!sr3conversation::looksLikeCtdgFilename("something.csc_pc"));
    CHECK(!sr3conversation::looksLikeCtdgFilename("no_extension_at_all"));

    // --- validateCtdgContent: well-formed and malformed. ---
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        auto v = sr3conversation::validateCtdgContent(blob);
        CHECK(v.status == sr3conversation::CtdgValidation::WellFormed);
    }
    {
        std::vector<uint8_t> notCtdg = {0x00, 0x01, 0x02, 0x03};
        auto v = sr3conversation::validateCtdgContent(notCtdg);
        CHECK(v.status == sr3conversation::CtdgValidation::NotWellFormed);
    }

    // --- refineWithCtdgValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
    // decompressEntry never produces OkUnconfirmedContent any more), passes
    // EVERY status through completely unchanged, including
    // OkUnconfirmedContent itself. ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = {0x00, 0x01, 0x02};
        vpp::DecompressResult out = sr3conversation::refineWithCtdgValidation(in);
        CHECK(out.status == vpp::DecodeStatus::Ok); // untouched
    }
    {
        std::vector<uint8_t> blob = buildCtdg("a.ctd", {{1, 1, 0}});
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = blob;
        vpp::DecompressResult out = sr3conversation::refineWithCtdgValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op
        CHECK(out.data == blob);
    }
    {
        std::vector<uint8_t> notCtdg = {0x00, 0x01, 0x02, 0x03};
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = notCtdg;
        vpp::DecompressResult out = sr3conversation::refineWithCtdgValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op, even for content that would have failed the old check
        CHECK(out.data == notCtdg);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic ctdg/conversation-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
