// See include/sr3audioruntime/wwise_setup_rebuild.h for the full scope note
// (every field-width/order claim's source, and the honestly-OPEN items).

#include "sr3audioruntime/wwise_setup_rebuild.h"

#include "sr3audioruntime/wwise_codebook_library.h"

#include <ogg/ogg.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3audioruntime {
namespace {

// The public Vorbis I spec's own "ilog()" (section 9.2.1: "number of bits
// required to store a positive number in binary, always rounding up" -
// equivalently ret such that 2^(ret-1) <= v < 2^ret, ilog(0)=0). Reimplemented
// directly rather than reused from third_party/libvorbis/lib/sharedbook.c's
// (internal, non-exported) ov_ilog() - same published formula either way.
int ilog(uint32_t v) {
    int ret = 0;
    while (v) {
        ++ret;
        v >>= 1;
    }
    return ret;
}

// Population count - used to size a residue partition's second-stage
// codebook-index list (third_party/libvorbis/lib/res0.c's own `icount`,
// same formula, reimplemented for the same reason as ilog() above).
int icount(unsigned int v) {
    int ret = 0;
    while (v) {
        ret += static_cast<int>(v & 1u);
        v >>= 1;
    }
    return ret;
}

// third_party/libvorbis/lib/sharedbook.c's `_book_maptype1_quantvals` -
// the public Vorbis I spec's codebook "lookup type 1" value-count formula
// (spec section 9.2.5), reimplemented (not linked - that symbol is an
// internal, non-exported part of the vendored static library) to compute
// how many quantized values a maptype-1 codebook's value list holds, using
// ONLY the dim/entries this project's own transcoder already read off the
// real bitstream.
long quantvals1(long dim, long entries) {
    if (entries < 1 || dim <= 0) return 0;
    long vals = static_cast<long>(std::floor(std::pow(static_cast<double>(entries), 1.0 / static_cast<double>(dim))));
    if (vals < 1) vals = 1;
    // Same integer-verification loop as the vendored original - don't trust
    // floating point alone when bitstream sync is at stake. Bounded to a
    // generous iteration cap since this project's own dim/entries come from
    // a real bitstream read (14-bit entries, 4-bit dim at most), not
    // attacker-controlled input, but a cap keeps a malformed/corrupt real
    // file from ever spinning forever.
    for (int guard = 0; guard < 100000; ++guard) {
        long acc = 1, acc1 = 1;
        int i;
        for (i = 0; i < dim; ++i) {
            if (entries / vals < acc) break;
            acc *= vals;
            if (LONG_MAX / (vals + 1) < acc1)
                acc1 = LONG_MAX;
            else
                acc1 *= (vals + 1);
        }
        if (i >= dim && acc <= entries && acc1 > entries) return vals;
        if (i < dim || acc > entries) {
            --vals;
            if (vals < 1) return 1;
        } else {
            ++vals;
        }
    }
    return vals;
}

long bitsRemaining(const oggpack_buffer& b) {
    return static_cast<long>(b.storage) * 8 - (static_cast<long>(b.endbyte) * 8 + static_cast<long>(b.endbit));
}

// Reads `bits` (0..32) from `in`, pre-checked against the real remaining
// bit count rather than trusting oggpack_read()'s own -1 sentinel, which is
// genuinely ambiguous for a full 32-bit read (a legitimate bit pattern with
// the sign bit set reads back as -1 on a system where `long` is 32 bits,
// e.g. Windows LLP64 - this project's own build target). Returns -1 only
// for a real "not enough bits left" condition.
long readChecked(oggpack_buffer& in, int bits) {
    if (bits < 0 || bits > 32) return -1;
    if (bitsRemaining(in) < bits) return -1;
    return oggpack_read(&in, bits);
}

bool failIf(bool cond, const char* what, std::string& err) {
    if (cond) err = what;
    return cond;
}

// --- per-codebook transcode (Sec11.11.3a + public Vorbis I codebook format,
// third_party/libvorbis/lib/codebook.c's vorbis_staticbook_unpack) ---
//
// Field layout per Sec11.11.3a's MAJOR CORRECTION: this is parsed from ONE
// codebook's own ISOLATED static buffer (a slice of
// kWwiseCodebookLibraryBlob, resolved by the 10-bit index
// rebuildVorbisSetupHeader() reads off the main packet's shared cursor -
// never from the main packet directly; see this file's own header comment's
// top-of-file note). Within that isolated buffer:
//
//   dim (4 bits), entries (14 bits), ordered (1 bit), length-field-width
//   (3 bits, +1 = 1..8) - read in order, unconditionally, for every real
//   codebook body. (An earlier bring-up pass special-cased dim==0 as a
//   "trivial/empty slot to skip" - that was an artifact of reading the
//   NEXT codebook's own 10-bit index as if it were this field, back when
//   codebooks were incorrectly assumed to be read inline from the main
//   packet. It does not apply once each codebook has its own isolated
//   buffer and is REMOVED here, not preserved.)
//
//   unordered (ordered==0) case, PINNED EXACTLY (2026-10-08 UPDATE, closes
//   out the residual below): immediately after `width`, a mandatory 1-bit
//   sparse/dense selector (real address `0x00fa7dfa`), read unconditionally
//   for every ordered==0 codebook regardless of whether it turns out sparse
//   or dense. Then, per entry (`entries` times): if sparse!=0, a 1-bit
//   `present` flag, and only if present a `width`-bit length (+1 bias,
//   stored; 0/absent entries keep lengths[i]==0, the pre-existing "unused
//   entry" convention); if sparse==0 (dense proper), an unconditional
//   `width`-bit length (+1 bias) every entry, no present flag at all - this
//   is exactly what this loop did BEFORE this fix, now correctly gated
//   behind the sparse selector instead of running unconditionally for every
//   ordered==0 codebook.
//
//   RESOLVED (was GENUINELY OPEN through the earlier 2026-10-08 bring-up
//   pass): the prior implementation never read the sparse/dense selector
//   bit at all and went straight into the (then-unconditional) per-entry
//   dense loop - that single missing bit shifted every subsequent read in
//   the codebook left by one, corrupting the length list and everything
//   after it (has-lookup flag, lookup-table fields) for the rest of that
//   codebook, which is exactly why the per-codebook bit budget never closed
//   for the library's 592 `ordered==0` codebooks. Verified character-by-
//   character against 3 real codebooks (library slots 7/61/5, one
//   genuinely dense with sparse=0), each closing its exact on-disk byte
//   length with zero slack - see spec-audio-runtime.md Sec11.11.3a's
//   2026-10-08 UPDATE and `scratchpad/wwise_vorbis_setup_header_bitexact_
//   2026-10-08.md` Part 5 (Team A) for the full worked bit traces.
//
//   has-lookup flag (named `maptype` in CodebookParseInfo for continuity
//   with the standard field it maps to): a single 1 bit, NOT 2 and NOT 4.
//   (Two earlier bring-up passes each read a different wrong width here -
//   2 bits, "confirmed" by a cross-check that actually decomposed the NEXT
//   codebook's own inline index bits; then 4 bits, standard Vorbis's own
//   width, once codebooks were correctly isolated per-buffer but before
//   this field's own real width was separately pinned. Both superseded by
//   spec-audio-runtime.md Sec11.11.3a's 2026-10-08 UPDATE, direct
//   disassembly at `0x00fa7dfa`'s neighborhood plus real libvorbis
//   acceptance of a 3-codebook minimal setup header built under this exact
//   1-bit reading.) There is no standard lookup_type==2 shape in this
//   implementation at all - the flag is 0 (no lookup) or 1 (has lookup,
//   always the lookup_type==1 shape/formula), stored into `info->maptype`
//   as that 0/1 value directly, not a literal 4-bit field value.
bool transcodeOneCodebook(oggpack_buffer& in, oggpack_buffer& out, std::string& err, CodebookParseInfo* info = nullptr) {
    const long dim = readChecked(in, 4);
    if (failIf(dim < 0, "EOF reading codebook dimension", err)) return false;

    const long entries = readChecked(in, 14);
    const long ordered = readChecked(in, 1);
    if (failIf(entries < 0 || ordered < 0,
               "EOF reading codebook entries/ordered", err))
        return false;
    if (info) {
        info->dim = dim;
        info->entries = entries;
        info->ordered = ordered;
    }

    // UPDATE 2026-10-09 (488/493 Kraft-inequality investigation, slot-160
    // divergence pass): the 3-bit "length-field-width" field is NOT read
    // unconditionally here before the ordered/unordered branch split - that
    // was this function's own prior bug. Cross-checked against the PUBLIC
    // vgmstream `src/coding/vorbis_custom_utils_wwise.c`'s own
    // `ww2ogg_codebook_library_rebuild()` (itself a public, independent
    // reimplementation of hcs's ww2ogg, read via WebFetch for conceptual
    // comparison only, per this project's own agrees-with-public-format
    // convention - not copied verbatim, re-derived from understanding): that
    // function reads `codeword_length_length` (3 bits) ONLY inside its own
    // unordered (`else`) branch, immediately before the 1-bit sparse
    // selector - never in the ordered branch, which instead reads its seed
    // length as a FIXED 5 bits (`bl_get(iw, 5, &initial_length)`), no width
    // field at all. This project's own prior code read a 3-bit `widthRaw`
    // unconditionally right after `ordered`, then used `widthRaw+1` as the
    // ordered branch's own seed-length field WIDTH - silently stealing 3
    // bits that belong to the ordered branch's run-length-code stream and
    // substituting a WRONG, too-narrow seed-length field in their place.
    // Confirmed independently (not just "matches public source"): re-parsing
    // library slot 160 (lengths=[1,1,1,1], Kraft sum 2.0, the slot this
    // whole investigation is root-caused to) under the corrected grammar
    // below gives lengths=[2,2,2,2], Kraft sum EXACTLY 1.0 (full double
    // precision) - a perfectly saturated, complete Huffman tree, not just
    // "closer to legal." Slot 16 (one of the other 6 originally-failing
    // slots, entries=49, hand-traced the same way) also lands on Kraft sum
    // EXACTLY 1.0 under this corrected grammar. Both are exact-saturation
    // results, not approximate improvements - strong, convergent,
    // mechanistically-explained evidence this is a pure field-width/
    // placement bug in this function's OWN bitstream grammar, independent of
    // what the library bytes actually contain (the raw bytes are untouched;
    // only how this function carves them into fields changed). Full
    // comparison: results/verify_vorbis_488_codebook_divergence_
    // 20261009.md.
    std::vector<long> lengths(static_cast<size_t>(entries), 0);
    if (ordered) {
        // Fixed 5-bit seed length - no length-field-width field exists in
        // this branch at all (see UPDATE note above).
        long curLen = readChecked(in, 5);
        if (failIf(curLen < 0, "EOF reading ordered codebook's seed length", err)) return false;
        curLen += 1;
        long i = 0;
        while (i < entries) {
            const int bits = ilog(static_cast<uint32_t>(entries - i));
            const long num = bits > 0 ? readChecked(in, bits) : 0;
            if (failIf(num < 0, "EOF reading ordered codebook's run count", err)) return false;
            for (long j = 0; j < num && i < entries; ++j, ++i) lengths[static_cast<size_t>(i)] = curLen;
            ++curLen;
        }
    } else {
        // Sec11.11.3a 2026-10-08 UPDATE: immediately after `ordered`, for
        // EVERY ordered==0 codebook, regardless of whether it turns out
        // sparse or dense, there is a mandatory 3-bit length-field-width
        // field (`widthRaw`, moved here from the unconditional read above -
        // see this function's own 2026-10-09 UPDATE note), then a mandatory
        // 1-bit sparse/dense selector (real address `0x00fa7dfa`) - both
        // read unconditionally here, before the per-entry loop. A prior
        // implementation attempt skipped the sparse bit entirely and went
        // straight into the per-entry loop, which shifted every subsequent
        // bit read in the codebook left by one and corrupted the length
        // list, the has-lookup flag, and (if set) the lookup-table fields
        // for the rest of that codebook - exactly this project's own
        // previously-reported "doesn't close its own bit budget" symptom.
        // Verified character-by-character against 3 real codebooks (library
        // slots 7/61/5), each closing its exact on-disk byte length with
        // zero slack.
        const long widthRaw = readChecked(in, 3);
        if (failIf(widthRaw < 0, "EOF reading codebook length-field-width", err)) return false;
        const long sparse = readChecked(in, 1);
        if (failIf(sparse < 0, "EOF reading codebook sparse/dense selector", err)) return false;
        // Per-entry length reads in the ordered==0 branch use `widthRaw`
        // DIRECTLY as the bit count (0-7), NOT `widthRaw+1` - that +1 bias
        // belongs only to the ORDERED branch's own (now fixed-5-bit) seed
        // field, never to this one.
        // Bit-exact-verified directly against spec-audio-codebook-
        // library.md's own raw blob for library slots 5 (dense, sparse=0:
        // widthRaw=3 used as exactly 3 bits/entry, not 4 - the has-lookup
        // bit that immediately follows the 25-entry length list lands
        // exactly on the expected value only under this reading) and 7
        // (sparse, sparse=1: widthRaw=2 used as exactly 2 bits/entry, not
        // 3 - same has-lookup-lands-correctly closure), both hand bit-
        // traced from that spec file's own data, not from disassembly.
        const int denseWidth = static_cast<int>(widthRaw);
        for (long i = 0; i < entries; ++i) {
            if (sparse) {
                const long present = readChecked(in, 1);
                if (failIf(present < 0, "EOF reading sparse codebook entry present flag", err)) return false;
                if (present) {
                    const long raw = readChecked(in, denseWidth);
                    if (failIf(raw < 0, "EOF reading sparse codebook entry length", err)) return false;
                    lengths[static_cast<size_t>(i)] = raw + 1;
                }
                // present==0: leave lengths[i]==0 - the "entry unused, no
                // codeword" convention already built into this function
                // (see the `lengths` vector's own doc comment above, and
                // the re-emit loop below that already handles len==0).
            } else {
                // sparse==0 (the dense case proper): unconditional per-
                // entry length read, exactly what this loop already did
                // before this fix, just now correctly gated behind the
                // sparse selector rather than running for every ordered==0
                // codebook regardless of its real sparse/dense shape.
                const long raw = readChecked(in, denseWidth);
                if (failIf(raw < 0, "EOF reading dense codebook entry length", err)) return false;
                lengths[static_cast<size_t>(i)] = raw + 1;
            }
        }
    }

    // Sec11.11.3a 2026-10-08 UPDATE: the "has VQ lookup table" flag is a
    // single 1 bit, not a 2-bit or 4-bit maptype field - directly checked
    // against the disassembly and against real libvorbis acceptance (a
    // complete minimal setup header built from 3 real codebooks and fed to
    // vorbis_synthesis_headerin, linked against this project's own vendored
    // vorbisstatic.lib, came back rc=0 ACCEPTED for id/comment/setup
    // headers). There is no separate standard lookup_type==2 shape in this
    // implementation at all - whenever the flag is set, the one real shape
    // (quantvals entries of value_bits bits each, quantvals via the
    // standard lookup_type==1 formula) applies; `maptype` below is kept as
    // the 0/1 has-lookup value itself (matching standard Vorbis maptype
    // 0/1), not a literal 4-bit field read off the stripped bitstream.
    const long hasLookup = readChecked(in, 1);
    if (failIf(hasLookup < 0, "EOF reading codebook has-lookup flag", err)) return false;
    const long maptype = hasLookup ? 1 : 0;
    if (info) info->maptype = maptype;

    long qmin = 0, qdelta = 0, qquant = 0, qseq = 0;
    std::vector<long> quantlist;
    if (maptype == 1 || maptype == 2) {
        // A 32-bit oggpack_read() can legitimately return a negative
        // `long` (sign bit set in the raw bit pattern) on a platform where
        // long is 32 bits (this project's Windows/LLP64 build target) -
        // checking bitsRemaining BEFORE the read, not the return value's
        // sign, is the only reliable EOF test for a full-width field.
        if (failIf(bitsRemaining(in) < 64, "EOF reading codebook qmin/qdelta", err)) return false;
        qmin = readChecked(in, 32);
        qdelta = readChecked(in, 32);
        const long qquantRaw = readChecked(in, 4);
        qseq = readChecked(in, 1);
        if (failIf(qquantRaw < 0 || qseq < 0, "EOF reading codebook qquant/qseq", err)) return false;
        qquant = qquantRaw + 1;
        const long quantvals = (maptype == 1) ? quantvals1(dim, entries) : entries * dim;
        if (failIf(quantvals < 0, "bad codebook quantvals computation", err)) return false;
        quantlist.resize(static_cast<size_t>(quantvals));
        for (long i = 0; i < quantvals; ++i) {
            if (failIf(bitsRemaining(in) < qquant, "EOF reading codebook quantized value list", err)) return false;
            quantlist[static_cast<size_t>(i)] = readChecked(in, static_cast<int>(qquant));
        }
        if (info) {
            info->valueBits = qquant;
            info->quantvals = quantvals;
        }
    }

    // --- emit a standard-conformant codebook (third_party/libvorbis/lib/
    // codebook.c's vorbis_staticbook_unpack, written in reverse) ---
    oggpack_write(&out, 0x564342, 24);  // standard sync marker - not present in the stripped input, synthesized fresh
    oggpack_write(&out, static_cast<unsigned long>(dim), 16);
    oggpack_write(&out, static_cast<unsigned long>(entries), 24);
    oggpack_write(&out, 0, 1);  // ordered=0: this project always re-emits as unordered...
    oggpack_write(&out, 1, 1);  // ...TAGGED (unused=1): always legal regardless of how many entries this
                                // codebook's own resolved lengths[] actually uses (including "all of them", or
                                // "none" for the all-zero slot-599-shaped degenerate case) - the lengths array is
                                // already fully resolved in memory above regardless of how the stripped input
                                // encoded it (ordered/dense/tagged-sparse), and the standard decoder's downstream
                                // codebook-table build consumes only that resolved lengths array (0 = unused, same
                                // standard convention third_party/libvorbis/lib/codebook.c's own reader uses), not
                                // the bitstream encoding method used to arrive at it.
    for (long i = 0; i < entries; ++i) {
        const long len = lengths[static_cast<size_t>(i)];
        if (len == 0) {
            oggpack_write(&out, 0, 1);  // entry unused - no codeword, no length bits
            continue;
        }
        if (failIf(len < 1 || len > 32, "codeword length out of the standard 5-bit field's legal 1..32 range", err))
            return false;
        oggpack_write(&out, 1, 1);
        oggpack_write(&out, static_cast<unsigned long>(len - 1), 5);
    }
    oggpack_write(&out, static_cast<unsigned long>(maptype), 4);
    if (maptype == 1 || maptype == 2) {
        oggpack_write(&out, static_cast<unsigned long>(qmin), 32);
        oggpack_write(&out, static_cast<unsigned long>(qdelta), 32);
        oggpack_write(&out, static_cast<unsigned long>(qquant - 1), 4);
        oggpack_write(&out, static_cast<unsigned long>(qseq), 1);
        for (long v : quantlist) oggpack_write(&out, static_cast<unsigned long>(v), static_cast<int>(qquant));
    }
    return true;
}

// --- per-floor transcode (floor_type hardcoded; floor1 body is a pure
// bit-for-bit passthrough since nothing in Sec11.11.3a claims its internal
// field widths differ from standard Vorbis I - third_party/libvorbis/lib/
// floor1.c's floor1_unpack, read in full) ---
bool transcodeOneFloor(oggpack_buffer& in, oggpack_buffer& out, std::string& err) {
    // floor_type is never read from the stripped packet (Sec11.11.3a) -
    // hardcoded here to 1 (floor1), the only value any real Vorbis encoder
    // ever emits (floor0/LSP is legacy and not produced by the real
    // reference encoder - third_party/libvorbis/lib/modes/*.h's own
    // floor_all.h only ever references floor1 setups).
    oggpack_write(&out, 1, 16);

    const long partitions = readChecked(in, 5);
    if (failIf(partitions < 0, "floor1: EOF reading partitions", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(partitions), 5);

    std::vector<long> partitionClass(static_cast<size_t>(partitions));
    int maxClass = -1;
    for (long j = 0; j < partitions; ++j) {
        const long pc = readChecked(in, 4);
        if (failIf(pc < 0, "floor1: EOF reading partition_class", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(pc), 4);
        partitionClass[static_cast<size_t>(j)] = pc;
        if (pc > maxClass) maxClass = static_cast<int>(pc);
    }

    std::vector<long> classDim(static_cast<size_t>(maxClass < 0 ? 0 : maxClass + 1), 0);
    for (int j = 0; j <= maxClass; ++j) {
        const long cdimRaw = readChecked(in, 3);
        if (failIf(cdimRaw < 0, "floor1: EOF reading class_dim", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(cdimRaw), 3);
        classDim[static_cast<size_t>(j)] = cdimRaw + 1;

        const long csub = readChecked(in, 2);
        if (failIf(csub < 0, "floor1: EOF reading class_subclasses", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(csub), 2);

        if (csub != 0) {
            const long cbook = readChecked(in, 8);
            if (failIf(cbook < 0, "floor1: EOF reading class_book", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(cbook), 8);
        }
        const long subCount = 1L << csub;
        for (long k = 0; k < subCount; ++k) {
            const long csubbook = readChecked(in, 8);
            if (failIf(csubbook < 0, "floor1: EOF reading class_subbook", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(csubbook), 8);
        }
    }

    const long mult = readChecked(in, 2);
    if (failIf(mult < 0, "floor1: EOF reading mult", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(mult), 2);

    const long rangebits = readChecked(in, 4);
    if (failIf(rangebits < 0, "floor1: EOF reading rangebits", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(rangebits), 4);

    long k = 0, countAcc = 0;
    for (long j = 0; j < partitions; ++j) {
        countAcc += classDim[static_cast<size_t>(partitionClass[static_cast<size_t>(j)])];
        for (; k < countAcc; ++k) {
            const long post = readChecked(in, static_cast<int>(rangebits));
            if (failIf(post < 0, "floor1: EOF reading postlist entry", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(post), static_cast<int>(rangebits));
        }
    }
    return true;
}

// --- per-residue transcode (residue_type READ from the stripped packet as
// a 2-bit field, NOT hardcoded by channel count - see this file's own
// 2026-10-08 UPDATE below for the sourcing/evidence; residue0 body
// otherwise a pure bit-for-bit passthrough - third_party/libvorbis/lib/
// res0.c's res0_unpack, read in full) ---
//
// UPDATE 2026-10-08 (vorb-chunk setup/audio-offset-pair re-hypothesis
// task): this project previously hardcoded residue_type by channel count
// and never read it from the input at all, going straight from the
// residue-count field to `begin`. That was WRONG - empirically narrowed
// down against real data (tools/wwise_setup_packet_bit_trace_probe.cpp's
// hand bit-trace of a real record, ct_angel_01_media.bnk_pc rec@2048: with
// the 2-bit read skipped, `groupbook` came out 80 against only 32 real
// codebooks, and every booklist entry came out >= books too - invalid by
// construction; adding the 2-bit read immediately fixes BOTH, every
// downstream value landing inside [0,32) with a clean, regular cascade
// pattern) and independently corroborated by public-source format
// understanding (agrees-with-public-format, NOT [CONFIRMED] - that label is
// reserved for direct disassembly, which Team B does not do): vgmstream's
// own `src/coding/vorbis_custom_utils_wwise.c` (ISC-style license, read via
// WebFetch for format understanding only, not transcribed), in its
// `ww2ogg_generate_vorbis_setup()`-equivalent rebuild path (the SAME
// WWV_EXTERNAL_CODEBOOKS/WWV_MODIFIED shape `WWVORBIS_V53` - this project's
// own confirmed vorb-chunk-size-0x2a version - uses), reads
// `residue_type` as a bare 2-bit field immediately before `begin`/`end`/
// `grouping`, then re-emits it widened to the standard 16-bit field
// (`bl_get(iw, 2, &residue_type)` -> `bl_put(ow, 16, residue_type)`) - this
// project's own prior guess (hardcode by channel count, never read) is
// superseded by this read.
bool transcodeOneResidue(oggpack_buffer& in, oggpack_buffer& out, std::string& err) {
    const long residueType = readChecked(in, 2);
    if (failIf(residueType < 0, "EOF reading residue_type", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(residueType), 16);

    const long begin = readChecked(in, 24);
    if (failIf(begin < 0, "residue: EOF reading begin", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(begin), 24);

    const long end = readChecked(in, 24);
    if (failIf(end < 0, "residue: EOF reading end", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(end), 24);

    const long groupingRaw = readChecked(in, 24);
    if (failIf(groupingRaw < 0, "residue: EOF reading grouping", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(groupingRaw), 24);

    const long partitionsRaw = readChecked(in, 6);
    if (failIf(partitionsRaw < 0, "residue: EOF reading partitions", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(partitionsRaw), 6);
    const long partitions = partitionsRaw + 1;

    const long groupbook = readChecked(in, 8);
    if (failIf(groupbook < 0, "residue: EOF reading groupbook", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(groupbook), 8);

    std::vector<long> secondstages(static_cast<size_t>(partitions));
    long acc = 0;
    for (long j = 0; j < partitions; ++j) {
        const long cascadeLow = readChecked(in, 3);
        if (failIf(cascadeLow < 0, "residue: EOF reading cascade low bits", err)) return false;
        const long cflag = readChecked(in, 1);
        if (failIf(cflag < 0, "residue: EOF reading cascade flag", err)) return false;
        long cascadeHigh = 0;
        if (cflag) {
            cascadeHigh = readChecked(in, 5);
            if (failIf(cascadeHigh < 0, "residue: EOF reading cascade high bits", err)) return false;
        }
        // Pure bit-for-bit passthrough of the exact fields just read - bit-
        // identical to either of third_party/libvorbis/lib/res0.c's own two
        // res0_pack encode shapes for a cascade < 8 (that file's own
        // "else: oggpack_write(...,4)" branch is bit-for-bit equivalent to
        // writing low-3-bits then a 0 flag bit, since cascade<8 means its
        // 4th bit is already 0) - no need to replicate the encoder's own
        // bit-saving branch choice, only its decoder's read shape.
        oggpack_write(&out, static_cast<unsigned long>(cascadeLow), 3);
        oggpack_write(&out, static_cast<unsigned long>(cflag), 1);
        if (cflag) oggpack_write(&out, static_cast<unsigned long>(cascadeHigh), 5);

        const long cascade = cascadeLow | (cflag ? (cascadeHigh << 3) : 0);
        secondstages[static_cast<size_t>(j)] = cascade;
        acc += icount(static_cast<unsigned int>(cascade));
    }

    for (long j = 0; j < acc; ++j) {
        const long book = readChecked(in, 8);
        if (failIf(book < 0, "residue: EOF reading booklist entry", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(book), 8);
    }
    return true;
}

// --- per-mapping transcode (map_type hardcoded; mapping0 body is a pure
// bit-for-bit passthrough except for the per-submap "time submap" byte,
// assumed ABSENT from the stripped input - see this file's header note -
// third_party/libvorbis/lib/mapping0.c's mapping0_unpack, read in full) ---
bool transcodeOneMapping(oggpack_buffer& in, oggpack_buffer& out, std::string& err, int channels) {
    oggpack_write(&out, 0, 16);  // map_type=0: the only VI_MAPB==1 legal value, never read from the stripped packet

    const long hasSubmaps = readChecked(in, 1);
    if (failIf(hasSubmaps < 0, "mapping: EOF reading has-submaps flag", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(hasSubmaps), 1);
    long submaps = 1;
    if (hasSubmaps) {
        const long submapsRaw = readChecked(in, 4);
        if (failIf(submapsRaw < 0, "mapping: EOF reading submaps count", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(submapsRaw), 4);
        submaps = submapsRaw + 1;
    }

    const long hasCoupling = readChecked(in, 1);
    if (failIf(hasCoupling < 0, "mapping: EOF reading has-coupling flag", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(hasCoupling), 1);
    if (hasCoupling) {
        const long couplingRaw = readChecked(in, 8);
        if (failIf(couplingRaw < 0, "mapping: EOF reading coupling_steps", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(couplingRaw), 8);
        const long couplingSteps = couplingRaw + 1;
        const int cbits = ilog(static_cast<uint32_t>(channels > 0 ? channels - 1 : 0));
        for (long i = 0; i < couplingSteps; ++i) {
            const long mag = cbits > 0 ? readChecked(in, cbits) : 0;
            if (failIf(mag < 0, "mapping: EOF reading coupling_mag", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(mag), cbits);
            const long ang = cbits > 0 ? readChecked(in, cbits) : 0;
            if (failIf(ang < 0, "mapping: EOF reading coupling_ang", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(ang), cbits);
        }
    }

    const long reserved = readChecked(in, 2);
    if (failIf(reserved < 0, "mapping: EOF reading reserved bits", err)) return false;
    oggpack_write(&out, static_cast<unsigned long>(reserved), 2);

    if (submaps > 1) {
        for (int ch = 0; ch < channels; ++ch) {
            const long mux = readChecked(in, 4);
            if (failIf(mux < 0, "mapping: EOF reading chmuxlist entry", err)) return false;
            oggpack_write(&out, static_cast<unsigned long>(mux), 4);
        }
    }

    for (long i = 0; i < submaps; ++i) {
        // Per-submap "time submap" byte: UPDATE 2026-10-08 (vorb-chunk
        // setup/audio-offset-pair re-hypothesis task) - this project
        // PREVIOUSLY assumed this byte was ABSENT from the stripped input
        // and only synthesized it fresh on output. That was WRONG -
        // real-data evidence (tools/wwise_setup_packet_bit_trace_probe.cpp,
        // same record as transcodeOneResidue()'s own UPDATE note above):
        // with this byte NOT read, every submap's floorsub/residuesub came
        // out 0 for EVERY mapping (floor 1/residue 1 - the far more
        // elaborate config this project's own floor1/residue0 passthrough
        // had already correctly decoded - never referenced by any mapping
        // at all, an implausible shape for real encoder output), one
        // mode's own mapping index came out out-of-range, and the whole
        // packet closed with 20 (or 39) leftover bits - well past the
        // <=7-bit slack a byte-aligned length prefix should leave. Reading
        // this byte (and discarding it, same as the standard decoder does -
        // mapping0.c's own "/* time submap unused */" comment - never
        // validated downstream by any real Vorbis decoder) immediately
        // fixes ALL of that: floorsub/residuesub spread across the full
        // valid range, every mode's mapping index becomes valid, and the
        // packet closes with 4 leftover bits. Independently corroborated
        // (agrees-with-public-format, NOT [CONFIRMED] - that label is
        // reserved for direct disassembly) by vgmstream's own
        // `src/coding/vorbis_custom_utils_wwise.c` (read via WebFetch for
        // format understanding only, not transcribed): its own rebuild path
        // documents, per submap, "time_config (8 bits, passed through as
        // an unused placeholder), floor_number (8 bits), and
        // residue_number (8 bits)" - i.e. the byte genuinely IS present in
        // the real stripped input, read and discarded (not reconstructed
        // from nothing), superseding this project's prior "absent" guess.
        const long timeConfig = readChecked(in, 8);
        if (failIf(timeConfig < 0, "mapping: EOF reading time-submap placeholder byte", err)) return false;
        oggpack_write(&out, 0, 8);  // standard decoder never validates this byte's value either way

        const long floorsub = readChecked(in, 8);
        if (failIf(floorsub < 0, "mapping: EOF reading floorsubmap", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(floorsub), 8);

        const long residuesub = readChecked(in, 8);
        if (failIf(residuesub < 0, "mapping: EOF reading residuesubmap", err)) return false;
        oggpack_write(&out, static_cast<unsigned long>(residuesub), 8);
    }
    return true;
}

}  // namespace

SetupRebuildResult rebuildVorbisSetupHeader(const uint8_t* strippedSetup, size_t strippedSetupLen, int channels) {
    SetupRebuildResult result;
    if (strippedSetup == nullptr || strippedSetupLen == 0 || channels <= 0) {
        result.diagnostic = "invalid input (null/empty stripped setup packet, or non-positive channel count)";
        return result;
    }
    if (!isWwiseCodebookLibraryDataAvailable()) {
        // Public build without the excluded compiled-in codebook data (see
        // include/sr3audioruntime/wwise_codebook_library.h) - every codebook
        // slot below would resolve against a zeroed placeholder, so report
        // this honestly instead of decoding garbage.
        result.diagnostic =
            "wwise codebook library data not loaded (public build without the excluded compiled-in "
            "codebook data - see isWwiseCodebookLibraryDataAvailable())";
        return result;
    }

    oggpack_buffer in;
    // oggpack_readinit's signature takes a non-const pointer, but it is
    // documented read-only (it never writes through `buf` - only
    // oggpack_writeinit's buffer is ever mutated); const_cast is safe here.
    oggpack_readinit(&in, const_cast<unsigned char*>(strippedSetup), static_cast<int>(strippedSetupLen));

    oggpack_buffer out;
    oggpack_writeinit(&out);

    std::string err;
    auto fail = [&](const std::string& where) {
        result.diagnostic = where + ": " + err;
        oggpack_writeclear(&out);
        return result;
    };

    const long booksRaw = readChecked(in, 8);
    if (booksRaw < 0) {
        result.diagnostic = "truncated stripped setup packet: EOF reading codebook count";
        oggpack_writeclear(&out);
        return result;
    }
    oggpack_write(&out, static_cast<unsigned long>(booksRaw), 8);
    const long books = booksRaw + 1;
    for (long i = 0; i < books; ++i) {
        // Sec11.11.3a's MAJOR CORRECTION: the main packet's shared cursor
        // holds only a bare 10-bit INDEX per codebook slot - nothing else
        // at this position. Resolve it against the compiled-in 599-entry
        // static library and parse that codebook from its OWN isolated
        // buffer (see this file's header comment's top-of-file note).
        const long index = readChecked(in, 10);
        if (index < 0) {
            err = "EOF reading codebook library index";
            return fail("codebook " + std::to_string(i) + "/" + std::to_string(books));
        }
        if (index >= kWwiseCodebookLibrarySize) {
            // Slot 599 (the library's own NULL terminator) and 600-1023
            // (confirmed spurious - spec-audio-codebook-library.md's own
            // Provenance section) are never a real codebook selection.
            err = "codebook library index " + std::to_string(index) + " out of range [0," +
                  std::to_string(kWwiseCodebookLibrarySize) + ")";
            return fail("codebook " + std::to_string(i) + "/" + std::to_string(books));
        }
        const WwiseCodebookLibraryEntry& entry = kWwiseCodebookLibraryIndex[index];
        oggpack_buffer cbIn;
        oggpack_readinit(&cbIn, const_cast<unsigned char*>(kWwiseCodebookLibraryBlob + entry.blobOffset),
                          static_cast<int>(entry.byteLen));
        if (!transcodeOneCodebook(cbIn, out, err)) {
            return fail("codebook " + std::to_string(i) + "/" + std::to_string(books) + " (library slot " +
                         std::to_string(index) + ")");
        }
    }
    result.codebookCount = static_cast<int>(books);

    // Time-domain-transform placeholder section: CONFIRMED entirely absent
    // from the stripped input (spec-audio-runtime.md Sec11.11.3a) -
    // synthesized fresh here at its one standard-legal value (a single
    // entry, type 0 - VI_TIMEB==1 means 0 is the only legal value), never
    // read from `in`.
    oggpack_write(&out, 0, 6);   // times = 0+1 = 1
    oggpack_write(&out, 0, 16);  // time_type[0] = 0

    const long floorsRaw = readChecked(in, 6);
    if (floorsRaw < 0) return fail("floors count");
    oggpack_write(&out, static_cast<unsigned long>(floorsRaw), 6);
    const long floors = floorsRaw + 1;
    for (long i = 0; i < floors; ++i) {
        if (!transcodeOneFloor(in, out, err)) return fail("floor " + std::to_string(i) + "/" + std::to_string(floors));
    }
    result.floorCount = static_cast<int>(floors);

    const long residuesRaw = readChecked(in, 6);
    if (residuesRaw < 0) return fail("residues count");
    oggpack_write(&out, static_cast<unsigned long>(residuesRaw), 6);
    const long residues = residuesRaw + 1;
    for (long i = 0; i < residues; ++i) {
        // residue_type IS read from the stripped packet, as a bare 2-bit
        // field - see transcodeOneResidue()'s own 2026-10-08 UPDATE for the
        // real-data evidence and public-source (vgmstream) corroboration
        // that superseded this project's prior "hardcode by channel count,
        // never read" guess.
        if (!transcodeOneResidue(in, out, err))
            return fail("residue " + std::to_string(i) + "/" + std::to_string(residues));
    }
    result.residueCount = static_cast<int>(residues);

    const long mapsRaw = readChecked(in, 6);
    if (mapsRaw < 0) return fail("mappings count");
    oggpack_write(&out, static_cast<unsigned long>(mapsRaw), 6);
    const long maps = mapsRaw + 1;
    for (long i = 0; i < maps; ++i) {
        if (!transcodeOneMapping(in, out, err, channels))
            return fail("mapping " + std::to_string(i) + "/" + std::to_string(maps));
    }
    result.mappingCount = static_cast<int>(maps);

    const long modesRaw = readChecked(in, 6);
    if (modesRaw < 0) return fail("modes count");
    oggpack_write(&out, static_cast<unsigned long>(modesRaw), 6);
    const long modes = modesRaw + 1;
    result.modeBlockflags.reserve(static_cast<size_t>(modes));
    for (long i = 0; i < modes; ++i) {
        const long blockflag = readChecked(in, 1);
        if (blockflag < 0) return fail("mode " + std::to_string(i) + "/" + std::to_string(modes) + ": blockflag");
        oggpack_write(&out, static_cast<unsigned long>(blockflag), 1);
        // Recorded in declaration order (index == real Vorbis mode number) -
        // rebuildWwiseAudioPacket() (wwise_setup_rebuild.h, spec-audio-
        // runtime.md Sec11.11.4a) resolves an audio packet's own hardcoded
        // mode-selector bit against this same list.
        result.modeBlockflags.push_back(blockflag != 0);
        // windowtype/transformtype are never read from the stripped packet
        // (Sec11.11.3a: "vestigial... always 0") - hardcoded here, the only
        // VI_WINDOWB==1 legal value for both.
        oggpack_write(&out, 0, 16);  // windowtype
        oggpack_write(&out, 0, 16);  // transformtype
        const long mapping = readChecked(in, 8);
        if (mapping < 0) return fail("mode " + std::to_string(i) + "/" + std::to_string(modes) + ": mapping");
        oggpack_write(&out, static_cast<unsigned long>(mapping), 8);
    }
    result.modeCount = static_cast<int>(modes);

    // Top-level end-of-packet framing bit: standard-required
    // (`_vorbis_unpack_books`'s own final `if(oggpack_read(opb,1)!=1)
    // goto err_out`), a pure sync guard with no informational content -
    // same treatment as the per-codebook sync marker above, synthesized
    // fresh here rather than read from the stripped input (which, per this
    // project's own empirical testing - see real_vorbis_decode_test.cpp -
    // does not carry one; this trusted-reader format has no need for it).
    oggpack_write(&out, 1, 1);

    std::vector<uint8_t> body(oggpack_get_buffer(&out), oggpack_get_buffer(&out) + oggpack_bytes(&out));
    oggpack_writeclear(&out);

    std::vector<uint8_t> full;
    full.reserve(7 + body.size());
    full.push_back(0x05);
    const char sig[6] = {'v', 'o', 'r', 'b', 'i', 's'};
    full.insert(full.end(), sig, sig + 6);
    full.insert(full.end(), body.begin(), body.end());

    result.ok = true;
    result.standardSetupPacket = std::move(full);
    result.diagnostic = "rebuilt OK: " + std::to_string(books) + " codebooks, " + std::to_string(floors) +
                         " floors, " + std::to_string(residues) + " residues, " + std::to_string(maps) +
                         " mappings, " + std::to_string(modes) + " modes";
    return result;
}

AudioModeBitResolution resolveWwiseAudioModeBit(const uint8_t* packet, size_t packetLen,
                                                 const std::vector<bool>& modeBlockflags) {
    AudioModeBitResolution result;
    if (packet == nullptr || packetLen == 0) {
        result.diagnostic = "empty/null raw Wwise audio packet";
        return result;
    }
    if (modeBlockflags.empty()) {
        result.diagnostic = "modeBlockflags is empty (the setup header's own mode list was never parsed, or "
                             "declared zero modes) - cannot resolve the audio packet's mode-selector bit";
        return result;
    }

    oggpack_buffer in;
    oggpack_readinit(&in, const_cast<unsigned char*>(packet), static_cast<int>(packetLen));

    // Sec11.11.4a [CONFIRMED]: the mode/window-type selector is a single
    // HARDCODED literal bit - this exe's own per-packet dispatcher pushes
    // the plain immediate `1` as the bit-read width, never a value computed
    // from the setup header's own declared mode count. No packet-type bit
    // precedes it (Sec11.11.4a: "no earlier bit-read... anywhere in the
    // chain") - this is the packet's very first bit.
    const long modeBit = readChecked(in, 1);
    if (modeBit < 0) {
        result.diagnostic = "EOF reading the single hardcoded mode-selector bit (packetLen=" +
                             std::to_string(packetLen) + " bytes)";
        return result;
    }
    if (static_cast<size_t>(modeBit) >= modeBlockflags.size()) {
        result.diagnostic = "mode-selector bit value " + std::to_string(modeBit) +
                             " addresses a mode beyond the setup header's own modeBlockflags (size=" +
                             std::to_string(modeBlockflags.size()) +
                             ") - Sec11.11.4a's own CONFIRMED 2-live-mode structural cap does not hold for "
                             "this record (not guessed around further - no invented fixes)";
        return result;
    }

    result.ok = true;
    result.modeBit = modeBit;
    result.isLong = modeBlockflags[static_cast<size_t>(modeBit)];
    return result;
}

AudioPacketRebuildResult rebuildWwiseAudioPacket(const uint8_t* packet, size_t packetLen, bool hasPreviousPacket,
                                                  bool previousPacketIsLong, bool hasNextPacket,
                                                  bool nextPacketIsLong, const std::vector<bool>& modeBlockflags) {
    AudioPacketRebuildResult result;

    // Resolves `packet`'s OWN mode bit only - see this function's own
    // header comment (wwise_setup_rebuild.h) and resolveWwiseAudioModeBit()'s
    // own comment for why this is now the ONLY place a mode bit is ever
    // read, and why `hasPreviousPacket`/`hasNextPacket`/`*IsLong` arrive as
    // plain booleans rather than this function reading a neighbor's bytes
    // itself.
    const AudioModeBitResolution modeRes = resolveWwiseAudioModeBit(packet, packetLen, modeBlockflags);
    if (!modeRes.ok) {
        result.diagnostic = modeRes.diagnostic;
        return result;
    }

    oggpack_buffer in;
    oggpack_readinit(&in, const_cast<unsigned char*>(packet), static_cast<int>(packetLen));
    // Discard: the same single bit resolveWwiseAudioModeBit() already read
    // and validated as modeRes.modeBit, above - re-read here only to
    // advance THIS function's own fresh cursor to the payload's first bit
    // before the passthrough-copy loop below.
    readChecked(in, 1);

    oggpack_buffer out;
    oggpack_writeinit(&out);

    // Standard Vorbis I audio packet shape (public spec, section 4.3, and
    // this project's own vendored third_party/libvorbis/lib/synthesis.c,
    // read directly: `vorbis_synthesis()` itself reads exactly 1 bit first
    // and requires it to be 0, `OV_ENOTAUDIO` otherwise, then `vb->lW`/
    // `vb->nW` directly off the next 2 bits for a long-block mode - lines
    // 42-44/63-64 of that file): packet-type bit (0 = audio), THEN the
    // mode-number field (1 bit, per the CONFIRMED finding above), THEN -
    // only for a long-block mode - the previous/next window-flag bits, THEN
    // the rest of the packet's own real residue/floor bits, unchanged. This
    // is what the REAL, unmodified libvorbis this project links against
    // requires to accept the packet - independent of whatever bits Wwise's
    // OWN stripped format omits on the wire.
    oggpack_write(&out, 0, 1);                                        // packet type = audio
    oggpack_write(&out, static_cast<unsigned long>(modeRes.modeBit), 1);  // mode number (1 bit, CONFIRMED width)

    if (modeRes.isLong) {
        // previousWindowFlag = blockflag of the mode the PRECEDING packet
        // selects; nextWindowFlag = blockflag of the mode the FOLLOWING
        // packet selects - the ordinary public Vorbis I encoder convention.
        // Both arrive here as plain booleans already resolved by the
        // caller's own sequential, turn-respecting loop (real_vorbis_
        // decode.cpp) - see this function's own header comment
        // (wwise_setup_rebuild.h) for the full mechanism. `*IsLong` is
        // ignored whenever the corresponding `has*Packet` is false (the
        // stream-boundary convention documented there: no adjacent packet
        // means that window flag is 0).
        const bool prevLong = hasPreviousPacket && previousPacketIsLong;
        const bool nextLong = hasNextPacket && nextPacketIsLong;
        oggpack_write(&out, prevLong ? 1 : 0, 1);
        oggpack_write(&out, nextLong ? 1 : 0, 1);
    }

    // Copy the rest of the ORIGINAL packet's bits (the real residue/floor
    // payload) unchanged, bit-aligned immediately after the inserted prefix
    // above - the only bits Wwise's own format omits are the ones just
    // reconstructed; everything after that single hardcoded mode bit is
    // untouched real Vorbis bitstream content. Chunked at 24 bits per read,
    // not 32 - readChecked()'s own documented caveat (this file's top note,
    // "genuinely ambiguous for a full 32-bit read") only applies at exactly
    // 32 bits; 24 avoids it entirely while still copying in large chunks
    // rather than 1 bit at a time.
    long bitsLeft = static_cast<long>(packetLen) * 8 - 1;  // minus the 1 mode bit already consumed from `in`
    while (bitsLeft > 0) {
        const int chunk = bitsLeft >= 24 ? 24 : static_cast<int>(bitsLeft);
        const long bits = readChecked(in, chunk);
        if (bits < 0) {
            result.diagnostic = "internal error: ran out of bits copying the packet's own real payload "
                                 "(bitsLeft=" +
                                 std::to_string(bitsLeft) + " of a precomputed exact budget - should be unreachable)";
            oggpack_writeclear(&out);
            return result;
        }
        oggpack_write(&out, static_cast<unsigned long>(bits), chunk);
        bitsLeft -= chunk;
    }

    std::vector<uint8_t> body(oggpack_get_buffer(&out), oggpack_get_buffer(&out) + oggpack_bytes(&out));
    oggpack_writeclear(&out);

    result.ok = true;
    result.standardPacket = std::move(body);
    result.diagnostic = "rebuilt OK: modeBit=" + std::to_string(modeRes.modeBit) +
                         " isLong=" + std::to_string(modeRes.isLong) + " origBytes=" + std::to_string(packetLen) +
                         " rebuiltBytes=" + std::to_string(result.standardPacket.size());
    return result;
}

bool parseOneCodebookForCrossCheck(const uint8_t* bytes, size_t len, CodebookParseInfo& info, std::string& err) {
    oggpack_buffer in;
    oggpack_readinit(&in, const_cast<unsigned char*>(bytes), static_cast<int>(len));
    oggpack_buffer out;
    oggpack_writeinit(&out);
    const bool ok = transcodeOneCodebook(in, out, err, &info);
    oggpack_writeclear(&out);
    return ok;
}

// See wwise_setup_rebuild.h's own header comment for the full rationale -
// diagnostic only, never called from the real decode path.
SetupBudgetProbeResult probeSetupPacketBitBudget(const uint8_t* strippedSetup, size_t strippedSetupLen, int channels) {
    SetupBudgetProbeResult result;
    if (strippedSetup == nullptr || strippedSetupLen == 0 || channels <= 0) {
        result.diagnostic = "invalid input (null/empty stripped setup packet, or non-positive channel count)";
        return result;
    }
    if (!isWwiseCodebookLibraryDataAvailable()) {
        // See rebuildVorbisSetupHeader()'s own identical check above.
        result.diagnostic =
            "wwise codebook library data not loaded (public build without the excluded compiled-in "
            "codebook data - see isWwiseCodebookLibraryDataAvailable())";
        return result;
    }

    oggpack_buffer in;
    oggpack_readinit(&in, const_cast<unsigned char*>(strippedSetup), static_cast<int>(strippedSetupLen));
    oggpack_buffer out;  // throwaway - transcodeOneFloor/Residue/Mapping require a write target
    oggpack_writeinit(&out);
    std::string err;

    auto giveUp = [&](const std::string& where) {
        result.ranToEnd = false;
        result.diagnostic = "EOF before reaching the end, at: " + where + (err.empty() ? "" : (" (" + err + ")"));
        oggpack_writeclear(&out);
        return result;
    };

    const long booksRaw = readChecked(in, 8);
    if (booksRaw < 0) return giveUp("codebook count");
    const long books = booksRaw + 1;
    result.books = static_cast<int>(books);
    for (long i = 0; i < books; ++i) {
        const long index = readChecked(in, 10);
        if (index < 0) return giveUp("codebook " + std::to_string(i) + "/" + std::to_string(books) + " index");
        if (index >= kWwiseCodebookLibrarySize) {
            ++result.outOfRangeCount;
            result.outOfRangeIndices.push_back(static_cast<int>(index));
            result.outOfRangePositions.push_back(static_cast<int>(i));
            // Deliberately do NOT parse a body - there is none to parse for
            // an out-of-range slot, and the main cursor never touches a
            // codebook body's bits regardless of whether the index is
            // valid (see this file's own header comment) - skipping costs
            // the main cursor nothing extra.
            continue;
        }
        const WwiseCodebookLibraryEntry& entry = kWwiseCodebookLibraryIndex[index];
        oggpack_buffer cbIn;
        oggpack_readinit(&cbIn, const_cast<unsigned char*>(kWwiseCodebookLibraryBlob + entry.blobOffset),
                          static_cast<int>(entry.byteLen));
        // A body-parse failure here would not affect the main cursor
        // either (separate buffer) - ignored rather than aborting, since
        // this probe's only question is about the MAIN cursor's own bit
        // budget, not the already-proven-airtight body grammar.
        transcodeOneCodebook(cbIn, out, err);
    }

    const long floorsRaw = readChecked(in, 6);
    if (floorsRaw < 0) return giveUp("floors count");
    const long floors = floorsRaw + 1;
    result.floors = static_cast<int>(floors);
    for (long i = 0; i < floors; ++i) {
        if (!transcodeOneFloor(in, out, err)) return giveUp("floor " + std::to_string(i) + "/" + std::to_string(floors));
    }

    const long residuesRaw = readChecked(in, 6);
    if (residuesRaw < 0) return giveUp("residues count");
    const long residues = residuesRaw + 1;
    result.residues = static_cast<int>(residues);
    for (long i = 0; i < residues; ++i) {
        if (!transcodeOneResidue(in, out, err))
            return giveUp("residue " + std::to_string(i) + "/" + std::to_string(residues));
    }

    const long mapsRaw = readChecked(in, 6);
    if (mapsRaw < 0) return giveUp("mappings count");
    const long maps = mapsRaw + 1;
    result.maps = static_cast<int>(maps);
    for (long i = 0; i < maps; ++i) {
        if (!transcodeOneMapping(in, out, err, channels)) return giveUp("mapping " + std::to_string(i) + "/" + std::to_string(maps));
    }

    const long modesRaw = readChecked(in, 6);
    if (modesRaw < 0) return giveUp("modes count");
    const long modes = modesRaw + 1;
    result.modes = static_cast<int>(modes);
    for (long i = 0; i < modes; ++i) {
        const long blockflag = readChecked(in, 1);
        if (blockflag < 0) return giveUp("mode " + std::to_string(i) + "/" + std::to_string(modes) + ": blockflag");
        const long mapping = readChecked(in, 8);
        if (mapping < 0) return giveUp("mode " + std::to_string(i) + "/" + std::to_string(modes) + ": mapping");
    }

    result.ranToEnd = true;
    result.leftoverBits = bitsRemaining(in);
    result.diagnostic = "ran to end cleanly";
    oggpack_writeclear(&out);
    return result;
}

}  // namespace sr3audioruntime
