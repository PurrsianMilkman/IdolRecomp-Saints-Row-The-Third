// Synthetic tests for the save-data readers (spec-save-format.md), built the way the
// container-format tests are: construct buffers from scratch using ONLY layout facts
// stated in the spec text, then feed them through the real readers.
//
// What this proves and what it does not: it proves the parsers are consistent with the
// spec text as I read it, and - because every fixture writer below is written separately
// from the reader (a stateful bit writer here, a position table in the library, a bitwise
// CRC here, a table CRC there) - that two independent readings of the same spec sentence
// agree. It does NOT prove the spec is right; that is what the real-file harnesses in
// tools/validation/ (validate_save_crc, validate_save_summary, validate_save_snapshot,
// validate_profile, validate_save_activity_names) are for.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3save/save_directory.h"
#include "sr3save/save_profile.h"
#include "sr3save/save_snapshot.h"

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

template <typename F>
bool throwsFormatError(F&& f) {
    try {
        f();
    } catch (const sr3save::FormatError&) {
        return true;
    }
    return false;
}

void put32(std::vector<uint8_t>& blob, size_t off, uint32_t v) {
    blob[off + 0] = static_cast<uint8_t>(v & 0xFF);
    blob[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    blob[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    blob[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putF32(std::vector<uint8_t>& blob, size_t off, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(blob, off, v);
}

void putAsciiUtf16LE(std::vector<uint8_t>& blob, size_t off, const std::string& s) {
    size_t i = 0;
    for (char c : s) {
        blob[off + i * 2] = static_cast<uint8_t>(c);
        blob[off + i * 2 + 1] = 0;
        ++i;
    }
    blob[off + i * 2] = 0;
    blob[off + i * 2 + 1] = 0;
}

// ---- independent bitwise CRC-32 (reflected 0xEDB88320, init 0, no final XOR) ----
uint32_t refCrc(const uint8_t* p, size_t n, uint32_t crc = 0) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc;
}
uint32_t refCrcZeros(size_t n, uint32_t crc) {
    std::vector<uint8_t> z(n, 0);
    return refCrc(z.data(), n, crc);
}

// ---- independent STATEFUL bit writer following Sec8.1's prose: LSB first; for an n-bit
// field the n div 8 whole low bytes are stored byte-aligned at the next unused byte and the
// remaining n mod 8 low bits continue the partially filled byte. ----
struct BitWriter {
    std::array<uint8_t, 24> buf{};
    int nextUnused = 0;   // index of the next never-touched byte
    int partial = -1;     // partially filled byte, -1 = none
    int partialBits = 0;
    void write(uint32_t value, int n) {
        int whole = n / 8, rem = n % 8;
        for (int i = 0; i < whole; ++i) buf[static_cast<size_t>(nextUnused++)] = static_cast<uint8_t>(value >> (8 * i));
        uint32_t rest = whole >= 4 ? 0u : (value >> (8 * whole));
        for (int b = 0; b < rem; ++b) {
            if (partial < 0 || partialBits == 8) {
                partial = nextUnused++;
                partialBits = 0;
            }
            buf[static_cast<size_t>(partial)] = static_cast<uint8_t>(buf[static_cast<size_t>(partial)] | (((rest >> b) & 1u) << partialBits));
            ++partialBits;
        }
    }
};

std::array<uint8_t, 24> writeSummaryWithBitWriter(const sr3save::SlotSummary& s) {
    BitWriter w;
    w.write(s.reserved1, 6);
    w.write(s.liveCountA_Open, 7);
    w.write(s.flaggedRecordCount, 9);
    w.write(s.liveCounterB_Open, 5);
    w.write(s.playMinutes, 17);
    w.write(s.difficulty, 2);
    w.write(s.cheatsUsed ? 1 : 0, 1);
    w.write(s.autosave ? 1 : 0, 1);
    w.write(s.completionPercent, 7);
    w.write(s.formatVersion, 8);
    w.write(s.locationLabelId, 7);
    w.write(s.reserved12, 5);
    w.write(s.month, 4);
    w.write(s.day, 5);
    w.write(s.year2, 7);
    w.write(s.hour, 5);
    w.write(s.minute, 6);
    w.write(s.second, 6);
    w.write(static_cast<uint32_t>(s.cashDiv100), 32);
    w.write(s.level, 6);
    w.write(s.regionRecordCount, 9);
    w.write(s.pointsInLevel, 32);
    return w.buf;
}

sr3save::SlotSummary sampleSummary(uint32_t salt) {
    sr3save::SlotSummary s;
    s.liveCountA_Open = 20 + salt % 5;
    s.flaggedRecordCount = 132 + salt;
    s.liveCounterB_Open = 5 + salt % 3;
    s.playMinutes = 1882 + salt;
    s.difficulty = 2;
    s.cheatsUsed = (salt & 1) != 0;
    s.autosave = salt == 0;
    s.completionPercent = 55;
    s.formatVersion = 94;
    s.locationLabelId = 35;
    s.month = 7;
    s.day = 27;
    s.year2 = 26;
    s.hour = 22;
    s.minute = 4;
    s.second = 51;
    s.cashDiv100 = 517227;
    s.level = 38;
    s.regionRecordCount = 154;
    s.pointsInLevel = 6332 + salt;
    return s;
}

std::vector<uint8_t> buildDirectory(const std::vector<int>& slots, bool fixHash = true) {
    std::vector<uint8_t> blob(sr3save::kSaveDirectorySize, 0);
    put32(blob, 0x004, static_cast<uint32_t>(slots.size()));
    uint32_t salt = 0;
    for (int slot : slots) {
        size_t off = sr3save::kSlotTableOffset + static_cast<size_t>(slot) * sr3save::kSlotRecordStride;
        blob[off] = 1;
        auto bytes = writeSummaryWithBitWriter(sampleSummary(salt++));
        for (size_t b = 0; b < 24; ++b) blob[off + 1 + b] = bytes[b];
    }
    if (fixHash) put32(blob, 0, refCrc(blob.data() + 8, 600));
    return blob;
}

// ---- synthetic snapshot ------------------------------------------------------
struct SnapSpec {
    std::string zone = "sr3_city";
    std::string time = "22:04:51";
    std::string date = "07.27.26";
    uint32_t version = 94;
    uint32_t playSeconds = 39688;
    uint32_t regionCount = 5;
    uint32_t difficulty = 2;
    int32_t cash = 51722765;
    uint32_t respectTotal = 73628, respectInLevel = 533, level = 22;
    uint8_t cheats = 0;
    uint32_t completion = 55, label = 35;
};

std::vector<uint8_t> buildSnapshot(const SnapSpec& sp, bool fixHash = true) {
    std::vector<uint8_t> b(sr3save::kSaveSnapshotSize, 0);
    put32(b, 0x008, sp.version);
    put32(b, 0x00C, 1);
    put32(b, 0x010, 0x000F0F6E);
    for (size_t i = 0; i < sp.zone.size(); ++i) b[0x014 + i] = static_cast<uint8_t>(sp.zone[i]);
    put32(b, 0x058, 0x11223344);
    put32(b, 0x05C, 0x55667788);
    putAsciiUtf16LE(b, 0x060, sp.time);
    putAsciiUtf16LE(b, 0x072, sp.date);
    put32(b, 0x084, sp.label);
    put32(b, 0x088, sp.completion);
    putF32(b, 0x09C, static_cast<float>(sp.playSeconds) + 0.75f);
    put32(b, 0x0A0, sp.playSeconds);
    b[0x0C8] = sp.cheats;
    // cheat ids: count at 0x0CC, ids from 0x0D0
    put32(b, 0x0CC, 2);
    put32(b, 0x0D0, 0x24EAF2FF);
    put32(b, 0x0D4, 0x32F5A33C);
    put32(b, 0x3F0, 2);
    put32(b, 0x3F8, 3);
    // collectibles: count 3 at 0x3FC, records from 0x400 (handle lo, hi, group, 0)
    put32(b, 0x3FC, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        put32(b, 0x400 + 16 * i, 0xAAA00000 + i);
        put32(b, 0x404 + 16 * i, 0xBBB00000 + i);
        put32(b, 0x408 + 16 * i, i / 2); // groups 0,0,1
    }
    put32(b, 0x2B64, sp.difficulty);
    // activity table: byte at +0, 64 x 12 records from +4, count at +0x304
    put32(b, 0x2B90 + 0x304, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        size_t o = 0x2B94 + 12 * i;
        put32(b, o, sr3save::nameHash(i == 0 ? "_A_ES_NE_02" : (i == 1 ? "_a_sn_ne_02" : "dlc1_a_es_01")));
        put32(b, o + 4, i == 2 ? 0 : 1);
        b[o + 8] = i == 2 ? 0 : 1;
        b[o + 9] = 0;
    }
    put32(b, 0x2E98, 0x0000000F);
    put32(b, 0x2E9C, 0x00000000);
    // statistics 68..216 at 0x3340: (value, denominator-stat id); id 93 = 533 shots, id 94 = 367 hits (aux 93)
    put32(b, 0x3340 + 8 * (93 - 68), 533);
    put32(b, 0x3340 + 8 * (94 - 68), 367);
    put32(b, 0x3340 + 8 * (94 - 68) + 4, 93);
    putF32(b, 0x3340 + 8 * (69 - 68), 26832.4f); // "cash earned per day" is a float stat
    put32(b, 0x3340 + 8 * (216 - 68), 7);
    put32(b, 0x4418, sp.respectTotal);
    put32(b, 0x441C, sp.respectInLevel);
    put32(b, 0x4420, sp.level);
    put32(b, 0x4A70, static_cast<uint32_t>(sp.cash));
    // regions: count at 0x113B8, records from 0x113C0 (handle lo, hi, flag byte at +8)
    put32(b, 0x113B8, sp.regionCount);
    for (uint32_t i = 0; i < sp.regionCount && i < 156; ++i) {
        size_t o = 0x113C0 + 16 * i;
        put32(b, o, 0xC0000000 + i);
        put32(b, o + 4, 0x000080E4);
        b[o + 8] = (i % 2 == 0) ? 1 : 0; // controlled on even records
    }
    put32(b, 0x11D88, 9);
    if (fixHash) {
        uint32_t crc = refCrc(b.data() + 4, 0x1A8E8 - 4);
        crc = refCrcZeros(0x118, crc);
        put32(b, 0, crc);
    }
    return b;
}

// ---- synthetic profile, built from the "Write" recipe of Sec7.7 ----------------
std::vector<uint8_t> buildProfile() {
    std::vector<uint8_t> b(sr3save::kProfileSize, 0);
    // stat stream: one record per serialisable id, ascending, [len][payload]; id 45 empty
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < 217; ++i) {
        bool omitted = (i >= 18 && i <= 41) || i == 43 || i == 50 || i == 51 || (i >= 58 && i <= 61) || i == 66 || i == 67 ||
                       i == 69 || i == 126 || i == 150 || i == 163 || i == 164;
        if (!omitted) ids.push_back(i);
    }
    size_t pos = 0;
    for (uint32_t id : ids) {
        if (id == 45) {
            b[pos++] = 0;
        } else {
            b[pos++] = 4;
            put32(b, pos, 1000 + id);
            pos += 4;
        }
    }
    put32(b, 0x3000, static_cast<uint32_t>(pos)); // used0
    put32(b, 0x3004, 0);
    put32(b, 0x3008, 0x400);
    put32(b, 0x300C, 0x07042000); // stale runtime pointer, must be ignored
    b[0x3018] = 1;
    b[0x3019] = 1;
    b[0x301A] = 0;
    put32(b, 0x301C, 5);
    put32(b, 0x3020, 0xA4);
    put32(b, 0x3024, 0x22);
    put32(b, 0x2000, 0x10);
    put32(b, 0x2004, 5);
    b[0x2008] = 0x18;
    putF32(b, 0x2010, 1.0f);
    putF32(b, 0x2014, 1.0f);
    putF32(b, 0x2020, 0.5f);
    putF32(b, 0x2028, 1.25f);
    for (size_t k = 0; k < 7; ++k) putF32(b, 0x202C + 4 * k, 0.8f);
    b[0x2050] = 1;
    putF32(b, 0x2054, 0.2397f);
    putF32(b, 0x2058, 0.2397f);
    put32(b, 0x2068, 1);
    put32(b, 0x206C, 1);
    for (uint32_t i = 0; i < 164; ++i) {
        size_t o = 0x3028 + 20 * i;
        put32(b, o, i);
        put32(b, o + 4, 0x11);
        put32(b, o + 8, 0x1F);
        put32(b, o + 12, 0xFFFFFFFFu);
        put32(b, o + 16, 0xFFFFFFFFu);
    }
    for (uint32_t i = 0; i < 34; ++i) {
        size_t o = 0x3CF8 + 40 * i;
        put32(b, o, i);
        put32(b, o + 4, 0x11);
        for (size_t k = 1; k < 4; ++k) put32(b, o + 4 + 4 * k, 0);
        for (size_t k = 0; k < 4; ++k) put32(b, o + 20 + 4 * k, 0xFFFFFFFFu);
        put32(b, o + 36, 0);
    }
    return b;
}

} // namespace

int main() {
    using namespace sr3save;

    // ================= CRC (Sec6.1) =================
    {
        const auto& t = crcTable();
        CHECK(t[0] == 0);
        CHECK(t[1] == 0x77073096u);
        CHECK(t[2] == 0xEE0E612Cu);
        std::vector<uint8_t> rnd(3000);
        uint32_t s = 99;
        for (auto& x : rnd) { s = s * 1103515245u + 12345u; x = static_cast<uint8_t>(s >> 16); }
        CHECK(crcUpdate(0, rnd.data(), rnd.size()) == refCrc(rnd.data(), rnd.size()));
        // 600 zero bytes hash to 0 (an empty directory)
        std::vector<uint8_t> z(600, 0);
        CHECK(crcUpdate(0, z.data(), z.size()) == 0);
        // zero prefix is invisible to an init-0 CRC (why a shifted start in [4,8] cannot be detected)
        std::vector<uint8_t> a = {1, 2, 3, 4, 5}, a0 = {0, 0, 0, 1, 2, 3, 4, 5};
        CHECK(crcUpdate(0, a.data(), a.size()) == crcUpdate(0, a0.data(), a0.size()));
        // and it is NOT the zlib CRC: known zlib CRC of "123456789" is 0xCBF43926
        const char* nine = "123456789";
        uint32_t init0 = crcUpdate(0, reinterpret_cast<const uint8_t*>(nine), 9);
        CHECK(init0 != 0xCBF43926u);
        CHECK((~crcUpdate(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>(nine), 9)) == 0xCBF43926u);
        // name hash: lower-cased, spec-quoted values
        CHECK(nameHash("npc_questionmark") == 0xA10725AAu);
        CHECK(nameHash("generic") == 0x0B237EADu);
        CHECK(nameHash("GENERIC") == nameHash("generic"));
    }

    // ================= savedir.sr3d_pc =================
    {
        // layout: the spec table and the writer rule agree on all 22 fields, 187 bits, no bit twice
        const auto& table = slotSummaryBitPositionsFromSpecTable();
        auto rule = slotSummaryBitPositionsFromWriterRule();
        size_t bits = 0;
        int agree = 0;
        for (size_t f = 0; f < table.size(); ++f) {
            bits += table[f].size();
            if (table[f] == rule[f]) ++agree;
        }
        CHECK(agree == 22);
        CHECK(bits == 187);

        // the library encoder (positions from the rule) equals an independent stateful writer
        for (uint32_t salt = 0; salt < 4; ++salt) {
            sr3save::SlotSummary s = sampleSummary(salt);
            CHECK(encodeSlotSummary(s) == writeSummaryWithBitWriter(s));
            CHECK(decodeSlotSummary(writeSummaryWithBitWriter(s).data()) == s);
        }
        // extremes: every field at its maximum width value survives the round trip
        {
            sr3save::SlotSummary m;
            m.liveCountA_Open = 127;
            m.flaggedRecordCount = 511;
            m.liveCounterB_Open = 31;
            m.playMinutes = 131071;
            m.difficulty = 3;
            m.cheatsUsed = true;
            m.autosave = true;
            m.completionPercent = 127;
            m.formatVersion = 255;
            m.locationLabelId = 127;
            m.month = 15;
            m.day = 31;
            m.year2 = 127;
            m.hour = 31;
            m.minute = 63;
            m.second = 63;
            m.cashDiv100 = -20000000;
            m.level = 63;
            m.regionRecordCount = 511;
            m.pointsInLevel = 0xFFFFFFFFu;
            CHECK(decodeSlotSummary(writeSummaryWithBitWriter(m).data()) == m);
            CHECK(decodeSlotSummary(encodeSlotSummary(m).data()) == m);
        }
        // a naive contiguous packing is NOT the layout: it would put field #3 at bits 13..21
        CHECK((!(table[2][0] == BitPos{1, 5} && table[2][1] == BitPos{1, 6})));

        std::vector<uint8_t> blob = buildDirectory({0, 3});
        SaveDirectory dir = SaveDirectory::parse(ByteView(blob.data(), blob.size()));
        CHECK(dir.activeSlotCount() == 2);
        CHECK(dir.hashValid());
        CHECK(dir.hashStored() == SaveDirectory::computeHash(ByteView(blob.data(), blob.size())));
        std::vector<int> occ = dir.occupiedSlotIndices();
        CHECK(occ.size() == 2 && occ[0] == 0 && occ[1] == 3);
        CHECK(dir.slots()[0].active && dir.slots()[3].active && !dir.slots()[1].active);
        CHECK(dir.slots()[0].summary == sampleSummary(0));
        CHECK(dir.slots()[3].summary == sampleSummary(1));
        CHECK(dir.slots()[0].summary.autosave && !dir.slots()[3].summary.autosave);
        CHECK(dir.slots()[1].summary == SlotSummary{}); // an inactive record is all zero

        // wrong size rejected (Sec2.1)
        std::vector<uint8_t> shortBlob(kSaveDirectorySize - 1, 0);
        CHECK(throwsFormatError([&] { SaveDirectory::parse(ByteView(shortBlob.data(), shortBlob.size())); }));

        // active-count mismatch rejected (error 5), hash fixed up so only the count is wrong
        {
            std::vector<uint8_t> bad = buildDirectory({0, 3}, false);
            put32(bad, 0x004, 5);
            put32(bad, 0, refCrc(bad.data() + 8, 600));
            CHECK(throwsFormatError([&] { SaveDirectory::parse(ByteView(bad.data(), bad.size())); }));
        }
        // hash mismatch: rejected under Enforce (error 4), reported under Ignore
        {
            std::vector<uint8_t> bad = blob;
            bad[8 + 25 * 3 + 5] ^= 0x01; // flip one bit inside slot 3's summary; stored hash now stale
            CHECK(throwsFormatError([&] { SaveDirectory::parse(ByteView(bad.data(), bad.size())); }));
            SaveDirectory lenient = SaveDirectory::parse(ByteView(bad.data(), bad.size()), SaveDirectory::HashPolicy::Ignore);
            CHECK(!lenient.hashValid());
        }
        // the hash does NOT cover the count at 0x004: changing only the count leaves the hash valid,
        // but the recount check then rejects it - so the hash and the count check are independent gates
        {
            std::vector<uint8_t> bad = blob;
            put32(bad, 0x004, 7);
            CHECK(SaveDirectory::computeHash(ByteView(bad.data(), bad.size())) == dir.hashStored());
            CHECK(throwsFormatError([&] { SaveDirectory::parse(ByteView(bad.data(), bad.size())); }));
        }
        // an empty directory (600 zero bytes) hashes to 0 and parses with 0 active slots
        {
            std::vector<uint8_t> empty(kSaveDirectorySize, 0);
            SaveDirectory e = SaveDirectory::parse(ByteView(empty.data(), empty.size()));
            CHECK(e.hashValid() && e.activeSlotCount() == 0 && e.occupiedSlotIndices().empty());
        }
    }

    // ================= sr3save_NN.sr3s_pc =================
    {
        SnapSpec sp;
        std::vector<uint8_t> blob = buildSnapshot(sp);
        SaveSnapshot snap = SaveSnapshot::parse(ByteView(blob.data(), blob.size()));

        CHECK(snap.hashValid());
        CHECK(snap.hashStored() == computeSnapshotHash(ByteView(blob.data(), blob.size())));
        CHECK(snap.versionSupported() && snap.formatVersion() == 94 && snap.secondaryVersion() == 1);
        CHECK(snap.buildStamp() == 0x000F0F6E);
        CHECK(snap.levelName() == "sr3_city");
        CHECK(snap.objectHandleLo() == 0x11223344 && snap.objectHandleHi() == 0x55667788);
        CHECK(snap.saveTime() == "22:04:51" && snap.saveDate() == "07.27.26");
        CHECK(snap.timestamp().valid && snap.timestamp().month == 7 && snap.timestamp().day == 27 &&
              snap.timestamp().year2 == 26 && snap.timestamp().hour == 22 && snap.timestamp().minute == 4 &&
              snap.timestamp().second == 51);
        // sort key: year factor is 32,140,800 (Sec6.5 correction), NOT 31,536,000
        CHECK(snap.saveSortKey() == 51 + 60LL * 4 + 3600LL * 22 + 86400LL * 27 + 2678400LL * 7 + 32140800LL * 26);
        CHECK(snap.saveSortKey() != 51 + 60LL * 4 + 3600LL * 22 + 86400LL * 27 + 2678400LL * 7 + 31536000LL * 26);
        CHECK(snap.playTimeSeconds() == 39688 && snap.playTimeMinutes() == 661); // Sec6.2 worked value
        CHECK(snap.playTimeTicksRaw() == 39688);                                  // legacy alias
        CHECK(static_cast<uint32_t>(snap.playClockFloat()) == snap.playTimeSeconds()); // truncation, not rounding
        CHECK(snap.labelIndex() == 35 && snap.completionPercent() == 55);
        CHECK(!snap.cheatsUsed());
        CHECK(snap.difficultyIndex() == 2 && std::string(snap.difficultyName()) == "hardcore");
        CHECK(snap.barnstormsFound() == 2 && snap.stuntJumpsFound() == 3);
        CHECK(snap.respectTotal() == 73628 && snap.respectInLevel() == 533 && snap.playerLevel() == 22);
        CHECK(snap.respectTotal() - snap.respectInLevel() == 73095); // the Sec6.5 check value
        CHECK(snap.cashRaw() == 51722765 && snap.cashDiv100() == 517227);

        // collectibles
        CHECK(snap.collectibleCount() == 3 && snap.collectiblesInBounds() && snap.collectibles().size() == 3);
        CHECK(snap.collectibles()[0].group == 0 && snap.collectibles()[2].group == 1);
        CHECK(snap.collectibles()[1].handleLo == 0xAAA00001 && snap.collectibles()[1].handleHi == 0xBBB00001);

        // regions: the array starts at 0x113C0, flag is bit 0 of the byte at +8
        CHECK(snap.regionRecordCount() == 5 && snap.regionRecords().size() == 5);
        CHECK(snap.regionRecords()[0].handleLo == 0xC0000000 && snap.regionRecords()[0].handleHi == 0x80E4);
        CHECK(snap.controlledRegionCount() == 3); // records 0, 2, 4
        CHECK(snap.regionRecords()[0].controlled && !snap.regionRecords()[1].controlled);
        static_assert(kOffsetRegionArray == 0x113C0 && kOffsetActivityArray == 0x113C0, "corrected array base (Sec6.5)");
        CHECK(snap.activityRecordCount() == 5 && snap.activityRecordsRaw().size() == 5 * 16 && snap.activityRecordsRaw()[8] == 1);

        // activity table
        CHECK(snap.activityTableCount() == 3 && snap.activityRecords().size() == 3);
        CHECK(snap.activityRecords()[0].nameHash == nameHash("_a_es_ne_02")); // lower-casing folds "_A_ES_NE_02"
        CHECK(snap.activityRecords()[0].levelsCompleted == 1 && snap.activityRecords()[0].completedMask == 1);
        CHECK(snap.activityRecords()[2].levelsCompleted == 0);
        CHECK(snap.activityMarkerFlagsRaw() == 0xF);


        // statistics (Sec12.6.1)
        CHECK(snap.statistic(67) == nullptr && snap.statistic(217) == nullptr);
        CHECK(snap.statistic(93) && snap.statistic(93)->asInt() == 533 && snap.statistic(93)->denominatorStatId == 0);
        CHECK(snap.statistic(94) && snap.statistic(94)->asInt() == 367 && snap.statistic(94)->denominatorStatId == 93);
        CHECK(snap.statistic(69) && snap.statistic(69)->asFloat() == 26832.4f);
        CHECK(snap.statistic(216) && snap.statistic(216)->asInt() == 7);
        // cheats
        CHECK(snap.activeCheatCount() == 2 && snap.activeCheatIds().size() == 2);
        CHECK(snap.activeCheatIds()[0] == nameHash("dlc_car_mass") && snap.activeCheatIds()[1] == nameHash("hohoho"));

        // derived directory summary (Sec8.2 rows), round-tripped through the directory encoder
        SlotSummary d = snap.deriveDirectorySummary(true);
        CHECK(d.flaggedRecordCount == 3 && d.playMinutes == 661 && d.difficulty == 2 && !d.cheatsUsed && d.autosave);
        CHECK(d.completionPercent == 55 && d.formatVersion == 94 && d.locationLabelId == 35);
        CHECK(d.month == 7 && d.day == 27 && d.year2 == 26 && d.hour == 22 && d.minute == 4 && d.second == 51);
        CHECK(d.cashDiv100 == 517227 && d.level == 22 && d.regionRecordCount == 5 && d.pointsInLevel == 533);
        CHECK(decodeSlotSummary(encodeSlotSummary(d).data()) == d);

        // legacy behaviour kept: a wildly wrong region count degrades to "no records", not a throw
        {
            std::vector<uint8_t> bad = buildSnapshot(sp);
            put32(bad, 0x113B8, 0xFFFFFFFFu);
            SaveSnapshot s2 = SaveSnapshot::parse(ByteView(bad.data(), bad.size()));
            CHECK(s2.regionRecordCount() == 0xFFFFFFFFu && !s2.regionRecordsInBounds() && s2.regionRecords().empty() &&
                  s2.activityRecordsRaw().empty());
            CHECK(s2.levelName() == "sr3_city"); // everything else still parses
        }
        // count 157 does not fit before 0x11D80 (room for 156)
        {
            std::vector<uint8_t> bad = buildSnapshot(sp);
            put32(bad, 0x113B8, 157);
            CHECK(SaveSnapshot::parse(ByteView(bad.data(), bad.size())).regionRecords().empty());
            put32(bad, 0x113B8, 156);
            CHECK(SaveSnapshot::parse(ByteView(bad.data(), bad.size())).regionRecords().size() == 156);
        }

        // hash: stale hash is reported, NOT rejected (the game never verifies it on load, Sec11.1)
        {
            std::vector<uint8_t> bad = buildSnapshot(sp);
            bad[0x5000] ^= 0x01;
            SaveSnapshot s2 = SaveSnapshot::parse(ByteView(bad.data(), bad.size()));
            CHECK(!s2.hashValid() && s2.versionSupported());
        }
        // the zero tail matters: hashing only the file bytes gives a different value
        {
            uint32_t withoutTail = refCrc(blob.data() + 4, 0x1A8E8 - 4);
            CHECK(withoutTail != snap.hashStored());
        }
        // a wrong version is reported, not thrown (the game silently skips applying it)
        {
            SnapSpec s2 = sp;
            s2.version = 93;
            std::vector<uint8_t> bad = buildSnapshot(s2);
            SaveSnapshot v = SaveSnapshot::parse(ByteView(bad.data(), bad.size()));
            CHECK(!v.versionSupported() && v.formatVersion() == 93 && v.hashValid());
        }
        // size check
        {
            std::vector<uint8_t> tooLong(kSaveSnapshotSize + 1, 0);
            CHECK(throwsFormatError([&] { SaveSnapshot::parse(ByteView(tooLong.data(), tooLong.size())); }));
        }
        // level name with no terminator inside its 68-byte room (0x014..0x057) is rejected
        {
            std::vector<uint8_t> bad = buildSnapshot(sp);
            for (size_t i = 0; i < kLevelNameMaxBytes; ++i) bad[kOffsetLevelName + i] = 0x41;
            static_assert(kLevelNameMaxBytes == 68, "level-name room 0x014..0x057");
            CHECK(throwsFormatError([&] { SaveSnapshot::parse(ByteView(bad.data(), bad.size())); }));
        }
        // difficulty naming
        {
            SnapSpec s2 = sp;
            s2.difficulty = 0;
            std::vector<uint8_t> b0 = buildSnapshot(s2);
            CHECK(std::string(SaveSnapshot::parse(ByteView(b0.data(), b0.size())).difficultyName()) == "casual");
            s2.difficulty = 1;
            std::vector<uint8_t> b1 = buildSnapshot(s2);
            CHECK(std::string(SaveSnapshot::parse(ByteView(b1.data(), b1.size())).difficultyName()) == "normal");
            s2.difficulty = 9;
            std::vector<uint8_t> b9 = buildSnapshot(s2);
            CHECK(std::string(SaveSnapshot::parse(ByteView(b9.data(), b9.size())).difficultyName()) == "unknown");
        }
        // directory built from the derived summary of this snapshot
        {
            std::vector<uint8_t> dirBlob(kSaveDirectorySize, 0);
            put32(dirBlob, 0x004, 1);
            dirBlob[8] = 1;
            auto enc = encodeSlotSummary(snap.deriveDirectorySummary(true));
            for (size_t k = 0; k < 24; ++k) dirBlob[9 + k] = enc[k];
            put32(dirBlob, 0, refCrc(dirBlob.data() + 8, 600));
            SaveDirectory dd = SaveDirectory::parse(ByteView(dirBlob.data(), dirBlob.size()));
            CHECK(dd.slots()[0].summary == snap.deriveDirectorySummary(true));
        }
    }

    // ================= sr3def_profile =================
    {
        std::vector<uint8_t> blob = buildProfile();
        SaveProfile p = SaveProfile::parse(ByteView(blob.data(), blob.size()));
        CHECK(profileSerialisedStatIds().size() == 179);
        CHECK(p.header().used0 == 891 && p.header().used1 == 0);
        CHECK(p.chunk0Exact() && p.chunk1Exact() && p.recordCountMatchesIds() && p.statsConsistent());
        CHECK(p.chunk0Walk().spans.size() == 179);
        CHECK(p.statRecords().size() == 179);
        const ProfileStatRecord* r0 = p.findStat(0);
        CHECK(r0 && r0->length == 4 && r0->rawDword == 1000);
        const ProfileStatRecord* r45 = p.findStat(45);
        CHECK(r45 && r45->length == 0 && !r45->hasDword && r45->fileOffset == 0x64);
        const ProfileStatRecord* r44 = p.findStat(44);
        CHECK(r44 && r44->rawDword == 1044);
        CHECK(p.findStat(18) == nullptr && p.findStat(43) == nullptr && p.findStat(69) == nullptr); // omitted ids
        CHECK(p.settingsMagicOk() && p.optionsApplied() && p.tablesApplied());
        CHECK(p.header().settingsPointerRaw == 0x07042000); // exposed raw, documented as meaningless
        CHECK(p.options().vibration() && p.options().crouchSetting() && !p.options().invertLookY());
        CHECK(p.options().gamma == 0.5f && p.options().brightness == 1.25f && p.options().audioVolume[2] == 0.8f);
        CHECK(p.options().pauseOnFocusLoss() && !p.options().mouseInvertY());
        CHECK(p.sessionSettingRaw() == 1 && p.friendlyFire() == 1);
        CHECK(p.tableAIndexMatches() == 164 && p.tableALoaderAccepts() == 164);
        CHECK(p.tableBIndexMatches() == 34 && p.tableBRangesOk() == 34);
        CHECK(p.reservedRegionAllZero());
        static_assert(kProfileTableBOffset + kProfileTableBCount * kProfileTableBRecordSize == kProfileSize, "tables tile to 0x4248");

        // Sec4's fixed 5-byte stride reading is wrong: it holds for exactly 20 records
        {
            size_t n = 0, pos = 0;
            while (blob[pos] == 4) { ++n; pos += 5; }
            CHECK(n == 20 && pos == 0x64);
        }
        // controls: the full gate must FAIL on wrong inputs
        auto fullGate = [](const SaveProfile& q) {
            return q.chunk0Exact() && q.chunk1Exact() && q.recordCountMatchesIds() && q.statsConsistent();
        };
        {
            std::vector<uint8_t> bad = blob;
            bad[0x64] = 1; // zero-length record's length byte 0 -> 1
            CHECK(!fullGate(SaveProfile::parse(ByteView(bad.data(), bad.size()))));
        }
        {
            std::vector<uint8_t> bad = blob;
            put32(bad, 0x3000, 892); // used0 + 1: a trailing zero byte parses as a 180th, empty record
            SaveProfile q = SaveProfile::parse(ByteView(bad.data(), bad.size()));
            CHECK(q.chunk0Exact());              // exact consumption alone does NOT catch it ...
            CHECK(!q.recordCountMatchesIds());   // ... the 179-record gate does
            CHECK(!fullGate(q));
        }
        {
            std::vector<uint8_t> bad = blob;
            put32(bad, 0x3000, 890);
            CHECK(!fullGate(SaveProfile::parse(ByteView(bad.data(), bad.size()))));
        }
        {
            std::vector<uint8_t> bad = blob;
            bad[5] = 3; // a mid-stream record with the wrong length
            CHECK(!fullGate(SaveProfile::parse(ByteView(bad.data(), bad.size()))));
        }
        {
            std::vector<uint8_t> shortBlob(blob.begin(), blob.end() - 1);
            CHECK(throwsFormatError([&] { SaveProfile::parse(ByteView(shortBlob.data(), shortBlob.size())); }));
        }
        // header gates: table version 4 -> tables not applied; magic wrong -> reported
        {
            std::vector<uint8_t> bad = blob;
            put32(bad, 0x301C, 4);
            CHECK(!SaveProfile::parse(ByteView(bad.data(), bad.size())).tablesApplied());
            std::vector<uint8_t> bad2 = blob;
            put32(bad2, 0x2000, 0x11);
            CHECK(!SaveProfile::parse(ByteView(bad2.data(), bad2.size())).settingsMagicOk());
        }
        // chunk 1 in use: records continue in the second chunk and still count as one stream
        {
            std::vector<uint8_t> two = blob;
            // move the last 3 records (ids 214,215,216 -> 3 x 5 bytes) into chunk 1
            size_t moved = 15;
            for (size_t i = 0; i < moved; ++i) {
                two[0x1000 + i] = two[891 - moved + i];
                two[891 - moved + i] = 0;
            }
            put32(two, 0x3000, static_cast<uint32_t>(891 - moved));
            put32(two, 0x3004, static_cast<uint32_t>(moved));
            SaveProfile q = SaveProfile::parse(ByteView(two.data(), two.size()));
            CHECK(q.chunk0Exact() && q.chunk1Exact() && q.recordCountMatchesIds() && q.statsConsistent());
            CHECK(q.chunk1Walk().spans.size() == 3 && q.findStat(216) && q.findStat(216)->rawDword == 1216);
        }
        // a chunk claiming more than 0xFFF bytes is refused
        {
            std::vector<uint8_t> bad = blob;
            put32(bad, 0x3000, 0x1000);
            CHECK(!SaveProfile::parse(ByteView(bad.data(), bad.size())).chunk0Exact());
        }
    }

    if (g_failures == 0) {
        std::cout << "All synthetic save-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
