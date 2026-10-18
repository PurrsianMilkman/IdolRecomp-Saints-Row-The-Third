// sr3radio content loader: the 12 per-station cue xtbls -> RadioSystem content table + stations.
// Source: spec-audio-runtime.md Sec14.6.1 (roster), 14.6.2 (loader 0x0055C9F0 / 0x0055C950),
// 14.6.5 item 1 (load order), 14.6.6 (file schema). Pure data transform, no I/O of its own.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3radio/radio_sim.h"

namespace sr3radio {

// Wwise GetIDFromString: FNV-1 32-bit over the lower-cased name (spec 14.6.1).
uint32_t wwiseHash(const std::string& name);

// Returns the bytes of a `.xtbl` by file name (case-insensitive match is the provider's job).
using XtblProvider = std::function<bool(const std::string& name, std::vector<uint8_t>& bytes)>;

struct RadioLoadReport {
    size_t contentEntries = 0;     // retail: 915
    size_t stationFilesLoaded = 0;
    size_t songRecords = 0;        // retail cap 299
    std::vector<std::string> warnings;
};

// Builds `sys.stations` (index 0 = Mix Tape, then the radio_stations.xtbl rows in order) and
// `sys.content` (stride-20-table equivalent, cap 925). Applies commercial_events.xtbl (30 slots, new
// game flags {1,2,6,27} set) and commercials.xtbl (Length/enable/disable overrides). Does NOT seed the
// RNG or build queues (call sys.rng.reseed + sys.worldStart()). Returns false with `err` on a missing
// mandatory file (radio_stations.xtbl or a station file).
bool loadRadioContent(const XtblProvider& provider, RadioSystem& sys, RadioLoadReport& report, std::string& err);

}  // namespace sr3radio
