/* DIAGNOSTIC-ONLY C bridge (2026-10-09, 488/493 codebook-count
   investigation task) - compiled as plain C, NOT C++, deliberately.

   tools/wwise_vorbis_codebook_kraft_probe.cpp (its own top comment has the
   full task context) needs to reach into `vorbis_info::codec_setup`'s real
   internal layout (`codec_setup_info`, third_party/libvorbis/lib/
   codec_internal.h - a vendored, PRIVATE libvorbis header, read directly
   for exactly the purpose this project's own existing comments already
   establish elsewhere, e.g. wwise_setup_rebuild.h's own header note) to
   isolate which per-record codebook POSITION fails vorbis_book_init_decode,
   using the REAL, already-libvorbis-unpacked static_codebook for that
   position (not this project's own transcode output).

   codec_internal.h cannot be #included from any C++ translation unit in
   this project: it transitively pulls in backends.h, whose own
   `vorbis_func_residue` struct (a real, unmodified part of this vendored
   library, third_party/libvorbis/lib/backends.h line ~94) uses `class` as
   an ordinary field name - legal C (this file's own vendored .c sources
   already compile it that way), a reserved keyword in C++. Rather than
   touch the vendored library (never done on this branch) or compile the
   whole probe as C, this one small file is the ONLY translation unit that
   includes codec_internal.h, compiled as C, exposing a tiny int/long-only
   C-linkage ABI the C++ probe calls instead - the same "duplicate, do not
   modify" boundary this branch's other probes already keep with
   production code, applied here to the vendored library's own internal
   headers instead. */

#include <string.h>

#include <vorbis/codec.h>

#include "codebook.h"
#include "codec_internal.h"

int sr3_probe_codec_setup_books(vorbis_info* vi) {
    codec_setup_info* ci = (codec_setup_info*)vi->codec_setup;
    return ci->books;
}

/* Runs the REAL vorbis_book_init_decode() on codec_setup_info->book_param[index]
   (the already-libvorbis-unpacked static_codebook for that position, filled by
   info.c's own _vorbis_unpack_books when the rebuilt setup header was accepted)
   and reports pass/fail plus dim/entries/usedEntries - the exact per-position
   isolation vorbis_synthesis_init()'s own all-or-nothing gate does not give.
   Returns: 0 on success (Kraft check / full init passed for this one codebook),
   libvorbis's own vorbis_book_init_decode() return code (nonzero) on a real
   failure, -2 if index is out of range, -3 if book_param[index] is NULL. */
int sr3_probe_book_init_decode(vorbis_info* vi, int index, long* dimOut, long* entriesOut, int* usedEntriesOut) {
    codec_setup_info* ci = (codec_setup_info*)vi->codec_setup;
    static_codebook* sc;
    codebook c;
    int rc;
    int used, i;

    if (index < 0 || index >= ci->books) return -2;
    sc = ci->book_param[index];
    if (!sc) return -3;

    if (dimOut) *dimOut = sc->dim;
    if (entriesOut) *entriesOut = sc->entries;
    used = 0;
    for (i = 0; i < sc->entries; ++i)
        if (sc->lengthlist[i] > 0) ++used;
    if (usedEntriesOut) *usedEntriesOut = used;

    memset(&c, 0, sizeof(c));
    rc = vorbis_book_init_decode(&c, sc);
    if (rc == 0) vorbis_book_clear(&c);
    return rc;
}
