#pragma once

// sr3audioruntime - the audio bring-up task's own minimal, standard
// (Microsoft RIFF/WAVE, PCM, public file-format knowledge - not a Wwise or
// Volition invention, same footing wwise_vorbis_setup.h's own fmt-chunk
// reader already claims) WAV writer, used to render whatever PCM
// real_vorbis_decode.h produced (or, if that empirical test came back
// negative - see that file's own header comment - a clearly-labelled
// fallback round trip) to a real .wav file for the new audio golden. This
// is this project's FIRST audio output of any kind; there is no existing
// writer to extend (cf. src/png_writer.cpp for the project's established
// style this file follows: a single free function, no class, no streaming
// API, exactly the shape writePng() already set for this project's first
// image output).

#include <cstdint>
#include <string>
#include <vector>

namespace sr3audioruntime {

// Writes a standard, canonical 44-byte-header PCM WAV file: one "fmt "
// sub-chunk (16 bytes, PCM format tag 1), one "data" sub-chunk holding
// `pcmInterleaved` verbatim. Returns false (and writes nothing) if the file
// could not be opened for writing; never throws.
//
// Also returns false WITHOUT opening/truncating anything when
// sr3io::isOutputPathSafe() rejects `path` (inside the game install tree,
// an existing non-.wav file, or an existing file over 64 MiB - see
// include/sr3io/output_path_guard.h for the 2026-10-08 archive-clobbering
// incident behind it). The 5-argument overload reports why in `error`; the
// original 4-argument one prints the refusal reason to stderr so existing
// callers are not left with a silent false.
bool writeWavPcm16(const std::string& path, const std::vector<int16_t>& pcmInterleaved, uint32_t sampleRate,
                    uint16_t channels);
bool writeWavPcm16(const std::string& path, const std::vector<int16_t>& pcmInterleaved, uint32_t sampleRate,
                    uint16_t channels, std::string& error);

// Duration in seconds implied by a WAV file's own header fields (data
// sub-chunk size / byte rate) - the "real audio duration should match what
// the spec/soundbank metadata states" check the golden-freezing task asks
// for. Reads only the fields this project's own writeWavPcm16() writes (a
// single fmt + single data sub-chunk, PCM); returns 0.0 and sets `ok` false
// for anything else, same discipline golden_scene_check.cpp's decodeOwnPng
// already uses ("the inverse of one specific writer, not a general reader").
double wavDurationSeconds(const std::string& path, bool& ok);

}  // namespace sr3audioruntime
